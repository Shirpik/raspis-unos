#include "data_store.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "config.h"
#include "format_utils.h"
#include "date_utils.h"
#include "runtime_config.h"

namespace timetable {
namespace {

constexpr int kMaxDataVersions = 50;
std::recursive_mutex g_data_file_mutex;

std::string StableUid(const std::string& kind, const std::string& key) {
    // Детерминированный FNV-1a: одинаковая сущность получает одинаковый uid
    // после повторного импорта, даже если числовые id были пересобраны.
    std::uint64_t hash = 1469598103934665603ULL;
    const std::string source = kind + ":" + key;
    for (unsigned char c : source) {
        hash ^= static_cast<std::uint64_t>(c);
        hash *= 1099511628211ULL;
    }
    std::ostringstream out;
    out << kind << "-" << std::hex << std::setw(16) << std::setfill('0') << hash;
    return out.str();
}

std::string VersionTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count() % 1000;
    const std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y%m%d-%H%M%S") << "-"
        << std::setw(3) << std::setfill('0') << millis;
    return out.str();
}

std::filesystem::path VersionsDir() {
    return std::filesystem::path("data") / "history";
}

bool AtomicWriteTextFile(
    const std::filesystem::path& path,
    const std::string& text,
    std::string& error
) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        error = "Не удалось создать каталог для файла данных: " + ec.message();
        return false;
    }

    const std::filesystem::path temp = path.parent_path() /
        (path.filename().string() + ".tmp-" + VersionTimestamp());
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) {
            error = "Не удалось открыть временный файл для записи";
            return false;
        }
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out) {
            error = "Ошибка записи временного файла данных";
            out.close();
            std::filesystem::remove(temp, ec);
            return false;
        }
    }

#ifdef _WIN32
    if (!MoveFileExW(
            temp.c_str(), path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD code = GetLastError();
        std::filesystem::remove(temp, ec);
        error = "Не удалось атомарно заменить файл данных (Windows error " +
            std::to_string(code) + ")";
        return false;
    }
#else
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        std::filesystem::remove(temp, ec);
        error = "Не удалось атомарно заменить файл данных: " + ec.message();
        return false;
    }
#endif
    return true;
}

std::vector<std::string> JsonStringArray(const JsonValue& value) {
    std::vector<std::string> result;
    if (!value.IsArray()) return result;
    for (const JsonValue& item : value.array_value) {
        if (item.IsString() && !item.string_value.empty()) result.push_back(item.string_value);
    }
    return result;
}

JsonValue DefaultWorkDaysJson() {
    JsonValue days = JsonValue::MakeArray();
    for (int day = 1; day <= 7; day++) {
        JsonValue item = JsonValue::MakeObject();
        item.At("day") = JsonValue::MakeNumber(day);
        item.At("enabled") = JsonValue::MakeBool(day != 7);
        item.At("start_slot") = JsonValue::MakeNumber(1);
        item.At("end_slot") = JsonValue::MakeNumber(SLOTS_PER_DAY);
        JsonValue slots = JsonValue::MakeArray();
        if (day != 7) {
            for (int slot = 1; slot <= SLOTS_PER_DAY; slot++)
                slots.array_value.push_back(JsonValue::MakeNumber(slot));
        }
        item.At("slots") = std::move(slots);
        days.array_value.push_back(item);
    }
    return days;
}

void NormalizeWorkSchedule(JsonValue& entity) {
    JsonValue& period = entity.At("work_period");
    if (!period.IsObject()) period = JsonValue::MakeObject();
    if (!period.At("from").IsString()) period.At("from") = JsonValue::MakeString("");
    if (!period.At("to").IsString()) period.At("to") = JsonValue::MakeString("");

    JsonValue normalized = DefaultWorkDaysJson();
    const JsonValue& source = entity.At("work_days");
    if (source.IsArray()) {
        for (const JsonValue& input : source.array_value) {
            if (!input.IsObject()) continue;
            const int day = JsonInt(input, "day", 0);
            if (day < 1 || day > 7) continue;
            JsonValue& output = normalized.array_value[day - 1];
            const bool enabled = JsonBool(input, "enabled", day != 7);
            const int start = std::clamp(JsonInt(input, "start_slot", 1), 1, SLOTS_PER_DAY);
            const int end = std::clamp(JsonInt(input, "end_slot", SLOTS_PER_DAY), start, SLOTS_PER_DAY);
            std::set<int> selected;
            const JsonValue& input_slots = input.At("slots");
            if (input_slots.IsArray()) {
                for (const JsonValue& value : input_slots.array_value) {
                    if (!value.IsNumber()) continue;
                    const int slot = static_cast<int>(std::llround(value.number_value));
                    if (slot >= 1 && slot <= SLOTS_PER_DAY) selected.insert(slot);
                }
            } else if (enabled) {
                for (int slot = start; slot <= end; slot++) selected.insert(slot);
            }
            if (!enabled) selected.clear();
            output.At("enabled") = JsonValue::MakeBool(!selected.empty());
            output.At("start_slot") = JsonValue::MakeNumber(selected.empty() ? start : *selected.begin());
            output.At("end_slot") = JsonValue::MakeNumber(selected.empty() ? end : *selected.rbegin());
            JsonValue exact = JsonValue::MakeArray();
            for (int slot : selected) exact.array_value.push_back(JsonValue::MakeNumber(slot));
            output.At("slots") = std::move(exact);
        }
    }
    entity.At("work_days") = normalized;

    JsonValue& overrides = entity.At("date_slot_overrides");
    if (!overrides.IsArray()) overrides = JsonValue::MakeArray();
}

WorkSchedule ParseWorkSchedule(const JsonValue& entity) {
    WorkSchedule result;
    const JsonValue& period = entity.At("work_period");
    Date from{};
    Date to{};
    if (period.IsObject() &&
        ParseDateIso(JsonString(period, "from", ""), from) &&
        ParseDateIso(JsonString(period, "to", ""), to) && from <= to) {
        result.has_period = true;
        result.from = from;
        result.to = to;
    }
    for (int day = 1; day <= 7; day++) {
        result.days[day - 1].enabled = day != 7;
        result.days[day - 1].start_slot = 1;
        result.days[day - 1].end_slot = SLOTS_PER_DAY;
        if (day != 7) {
            for (int slot = 1; slot <= SLOTS_PER_DAY; slot++)
                result.days[day - 1].slots.insert(slot);
        }
    }
    const JsonValue& days = entity.At("work_days");
    if (days.IsArray()) {
        for (const JsonValue& input : days.array_value) {
            if (!input.IsObject()) continue;
            const int day = JsonInt(input, "day", 0);
            if (day < 1 || day > 7) continue;
            WorkDayWindow& output = result.days[day - 1];
            output.enabled = JsonBool(input, "enabled", day != 7);
            output.start_slot = std::clamp(JsonInt(input, "start_slot", 1), 1, SLOTS_PER_DAY);
            output.end_slot = std::clamp(
                JsonInt(input, "end_slot", SLOTS_PER_DAY), output.start_slot, SLOTS_PER_DAY);
            output.slots.clear();
            const JsonValue& values = input.At("slots");
            if (values.IsArray()) {
                for (const JsonValue& value : values.array_value) {
                    if (!value.IsNumber()) continue;
                    const int slot = static_cast<int>(std::llround(value.number_value));
                    if (slot >= 1 && slot <= SLOTS_PER_DAY) output.slots.insert(slot);
                }
            } else if (output.enabled) {
                for (int slot = output.start_slot; slot <= output.end_slot; slot++)
                    output.slots.insert(slot);
            }
            if (!output.enabled) output.slots.clear();
        }
    }
    const JsonValue& overrides = entity.At("date_slot_overrides");
    if (overrides.IsArray()) {
        for (const JsonValue& item : overrides.array_value) {
            if (!item.IsObject()) continue;
            Date date;
            if (!ParseDateIso(JsonString(item, "date", ""), date)) continue;
            std::set<int> slots;
            const JsonValue& values = item.At("slots");
            if (values.IsArray()) {
                for (const JsonValue& value : values.array_value) {
                    if (!value.IsNumber()) continue;
                    const int slot = static_cast<int>(std::llround(value.number_value));
                    if (slot >= 1 && slot <= SLOTS_PER_DAY) slots.insert(slot);
                }
            }
            result.date_slot_overrides[date] = std::move(slots);
        }
    }
    return result;
}

void AddIssue(
    JsonValue& issues,
    const std::string& severity,
    const std::string& code,
    const std::string& message,
    const std::string& entity_type = "",
    int entity_id = -1
) {
    JsonValue issue = JsonValue::MakeObject();
    issue.At("severity") = JsonValue::MakeString(severity);
    issue.At("code") = JsonValue::MakeString(code);
    issue.At("message") = JsonValue::MakeString(message);
    if (!entity_type.empty()) issue.At("entity_type") = JsonValue::MakeString(entity_type);
    if (entity_id >= 0) issue.At("entity_id") = JsonValue::MakeNumber(entity_id);
    issues.array_value.push_back(issue);
}

bool IsSafeVersionFilename(const std::string& filename) {
    if (filename.empty() || filename.size() > 128) return false;
    if (filename.find("..") != std::string::npos || filename.find('/') != std::string::npos ||
        filename.find('\\') != std::string::npos) return false;
    return filename.rfind("version_", 0) == 0 &&
           filename.size() > 5 && filename.substr(filename.size() - 5) == ".json";
}

void PruneVersions() {
    std::error_code ec;
    const auto dir = VersionsDir();
    if (!std::filesystem::exists(dir, ec)) return;
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    while (static_cast<int>(files.size()) > kMaxDataVersions) {
        std::filesystem::remove(files.front(), ec);
        files.erase(files.begin());
    }
}

JsonValue GroupJson(int id, const std::string& name, int parts) {
    JsonValue v = JsonValue::MakeObject();
    v.At("id") = JsonValue::MakeNumber(id);
    v.At("uid") = JsonValue::MakeString(StableUid("group", name));
    v.At("name") = JsonValue::MakeString(name);
    v.At("parts") = JsonValue::MakeNumber(parts);
    v.At("size") = JsonValue::MakeNumber(0);
    v.At("home_campus") = JsonValue::MakeNumber(-1);
    v.At("work_period") = JsonValue::MakeObject();
    v.At("work_days") = DefaultWorkDaysJson();
    return v;
}

JsonValue TeacherJson(int id, const std::string& name) {
    JsonValue v = JsonValue::MakeObject();
    v.At("id") = JsonValue::MakeNumber(id);
    v.At("uid") = JsonValue::MakeString(StableUid("teacher", name));
    v.At("name") = JsonValue::MakeString(name);
    v.At("work_period") = JsonValue::MakeObject();
    v.At("work_days") = DefaultWorkDaysJson();
    v.At("default_room") = JsonValue::MakeNumber(-1);
    v.At("campus_priority") = JsonValue::MakeArray();
    v.At("allowed_campuses") = JsonValue::MakeArray();
    v.At("room_responsibility") = JsonValue::MakeString("");
    return v;
}

JsonValue RoomTypeJson(int id, const std::string& name, const std::string& description) {
    JsonValue v = JsonValue::MakeObject();
    v.At("id") = JsonValue::MakeNumber(id);
    v.At("uid") = JsonValue::MakeString(StableUid("room-type", std::to_string(id)));
    v.At("name") = JsonValue::MakeString(name);
    v.At("description") = JsonValue::MakeString(description);
    return v;
}

JsonValue LessonJson(
    int id,
    int group,
    int subgroup,
    int teacher,
    int total_slots,
    const std::string& name,
    int subject_id,
    bool is_lab,
    bool is_block,
    const std::vector<int>& allowed_campuses
) {
    JsonValue v = JsonValue::MakeObject();
    v.At("id") = JsonValue::MakeNumber(id);
    v.At("uid") = JsonValue::MakeString(StableUid(
        "lesson", std::to_string(group) + ":" + std::to_string(subgroup) + ":" + name));
    v.At("group") = JsonValue::MakeNumber(group);
    v.At("subgroup") = JsonValue::MakeNumber(subgroup);
    v.At("teacher") = JsonValue::MakeNumber(teacher);
    v.At("total_slots") = JsonValue::MakeNumber(total_slots);
    v.At("name") = JsonValue::MakeString(name);
    v.At("subject_id") = JsonValue::MakeNumber(subject_id);
    v.At("is_lab") = JsonValue::MakeBool(is_lab);
    v.At("is_block") = JsonValue::MakeBool(is_block);
    v.At("is_pp") = JsonValue::MakeBool(false);
    v.At("week_parity") = JsonValue::MakeString("all");
    v.At("fixed_room") = JsonValue::MakeNumber(-1);
    v.At("allow_room_substitution") = JsonValue::MakeBool(true);
    v.At("required_capacity") = JsonValue::MakeNumber(0);
    v.At("required_room_type") = JsonValue::MakeNumber(0);
    v.At("required_equipment") = JsonValue::MakeArray();
    v.At("required_room_purpose") = JsonValue::MakeString("");
    JsonValue campuses = JsonValue::MakeArray();
    for (int campus : allowed_campuses) {
        campuses.array_value.push_back(JsonValue::MakeNumber(campus));
    }
    v.At("allowed_campuses") = campuses;
    return v;
}

JsonValue UnavailableJson(int id, int group, const std::string& from, const std::string& to) {
    JsonValue v = JsonValue::MakeObject();
    v.At("id") = JsonValue::MakeNumber(id);
    v.At("group") = JsonValue::MakeNumber(group);
    v.At("from") = JsonValue::MakeString(from);
    v.At("to") = JsonValue::MakeString(to);
    return v;
}


JsonValue UnavailableJsonWithText(int id, int group, const std::vector<std::string>& dates, const std::string& text, bool all_groups) {
    JsonValue v = JsonValue::MakeObject();
    v.At("id") = JsonValue::MakeNumber(id);
    v.At("all_groups") = JsonValue::MakeBool(all_groups);
    if (!all_groups) {
        v.At("group") = JsonValue::MakeNumber(group);
    }
    JsonValue dates_json = JsonValue::MakeArray();
    for (const std::string& date : dates) {
        dates_json.array_value.push_back(JsonValue::MakeString(date));
    }
    v.At("dates") = dates_json;
    v.At("text") = JsonValue::MakeString(text);
    return v;
}

std::vector<int> TargetGroupsForUnavailable(const JsonValue& item, const std::vector<GroupData>& groups) {
    std::vector<int> result;
    if (JsonBool(item, "all_groups", false)) {
        for (const GroupData& group : groups) {
            result.push_back(group.id);
        }
        return result;
    }

    int group = JsonInt(item, "group", -1);
    if (group >= 0) {
        result.push_back(group);
    }
    return result;
}

std::vector<Date> DatesFromUnavailableItem(const JsonValue& item) {
    std::vector<Date> dates;

    Date single;
    if (ParseDateIso(JsonString(item, "date", ""), single)) {
        dates.push_back(single);
    }

    const JsonValue& dates_json = item.At("dates");
    if (dates_json.IsArray()) {
        for (const JsonValue& value : dates_json.array_value) {
            if (!value.IsString()) continue;
            Date date;
            if (ParseDateIso(value.string_value, date)) {
                dates.push_back(date);
            }
        }
    }

    std::sort(dates.begin(), dates.end());
    dates.erase(std::unique(dates.begin(), dates.end()), dates.end());
    return dates;
}

void AddTextForRange(std::map<Date, std::string>& texts, const Date& from, const Date& to, const std::string& text) {
    if (text.empty()) return;
    Date cur = from;
    while (cur <= to) {
        if (DayOfWeek(cur) != 7) {
            texts[cur] = text;
        }
        cur = NextDay(cur);
    }
}

std::vector<std::string> NamesFromGroups(const std::vector<GroupData>& groups) {
    int max_id = -1;
    for (const auto& group : groups) max_id = std::max(max_id, group.id);
    std::vector<std::string> names(max_id + 1);
    for (const auto& group : groups) {
        if (group.id >= 0) names[group.id] = group.name;
    }
    for (int i = 0; i < static_cast<int>(names.size()); i++) {
        if (names[i].empty()) names[i] = "Группа " + std::to_string(i);
    }
    return names;
}

std::vector<std::string> NamesFromTeachers(const std::vector<TeacherData>& teachers) {
    int max_id = -1;
    for (const auto& teacher : teachers) max_id = std::max(max_id, teacher.id);
    std::vector<std::string> names(max_id + 1);
    for (const auto& teacher : teachers) {
        if (teacher.id >= 0) names[teacher.id] = teacher.name;
    }
    for (int i = 0; i < static_cast<int>(names.size()); i++) {
        if (names[i].empty()) names[i] = "Преподаватель " + std::to_string(i);
    }
    return names;
}

}  // namespace

std::string DataFilePath() {
    return "data/timetable_data.json";
}

JsonValue DefaultDataJson() {
    JsonValue root = JsonValue::MakeObject();

    JsonValue settings = JsonValue::MakeObject();
    settings.At("start_date") = JsonValue::MakeString("2026-01-12");
    settings.At("end_date") = JsonValue::MakeString("2026-06-19");
    settings.At("teacher_period_targets") = JsonValue::MakeArray();
    settings.At("solver_config") = SolverConfigToJson(DefaultSolverConfig());
    root.At("settings") = settings;

    JsonValue groups = JsonValue::MakeArray();
    groups.array_value.push_back(GroupJson(0, "ИСП-3304", 2));
    groups.array_value.push_back(GroupJson(1, "ИСП-3305п", 2));
    root.At("groups") = groups;

    JsonValue teachers = JsonValue::MakeArray();
    teachers.array_value.push_back(TeacherJson(0, "Новосёлова"));
    teachers.array_value.push_back(TeacherJson(1, "Давыдова"));
    teachers.array_value.push_back(TeacherJson(2, "Нуров"));
    teachers.array_value.push_back(TeacherJson(3, "Потапова"));
    teachers.array_value.push_back(TeacherJson(4, "Серянина"));
    teachers.array_value.push_back(TeacherJson(5, "Гобов"));
    teachers.array_value.push_back(TeacherJson(6, "Самцова"));
    teachers.array_value.push_back(TeacherJson(7, "Гарбузов"));
    root.At("teachers") = teachers;

    JsonValue room_types = JsonValue::MakeArray();
    room_types.array_value.push_back(RoomTypeJson(1, "Лекционная аудитория", "Обычные теоретические занятия, доска и посадочные места"));
    room_types.array_value.push_back(RoomTypeJson(2, "Мастерская со станками", "Практические занятия, для которых необходимы станки"));
    room_types.array_value.push_back(RoomTypeJson(3, "Компьютерный класс", "Занятия, для которых необходимы персональные компьютеры"));
    root.At("room_types") = room_types;
    root.At("rooms") = JsonValue::MakeArray();
    root.At("teacher_unavailable") = JsonValue::MakeArray();

    JsonValue unavailable = JsonValue::MakeArray();
    unavailable.array_value.push_back(UnavailableJson(0, 0, "2026-04-30", "2026-06-19"));
    unavailable.array_value.back().At("text") = JsonValue::MakeString("Производственная практика");
    unavailable.array_value.push_back(UnavailableJson(1, 1, "2026-04-30", "2026-06-19"));
    unavailable.array_value.back().At("text") = JsonValue::MakeString("Производственная практика");
    root.At("unavailable") = unavailable;

    JsonValue lessons = JsonValue::MakeArray();
    int id = 0;
    int subj = 0;
    int engId = subj++;

    for (int g = 0; g < 2; g++) {
        int bs = g * PARTS_PER_GROUP;
        lessons.array_value.push_back(LessonJson(id++, g, bs, T_NOVOSELOVA, 13, "Ин. язык", engId, false, false, {LESNAYA}));
        lessons.array_value.push_back(LessonJson(id++, g, bs + 1, T_DAVYDOVA, 13, "Ин. язык", engId, false, false, {LESNAYA}));
    }

    for (int g = 0; g < 2; g++) lessons.array_value.push_back(LessonJson(id++, g, -1, T_NUROV, 14, "Физическая культура", -1, false, false, {LESNAYA, KRIVOUSOVA}));
    for (int g = 0; g < 2; g++) lessons.array_value.push_back(LessonJson(id++, g, -1, T_POTAPOVA, 17, "БЖД", -1, false, false, {LESNAYA, KRIVOUSOVA}));

    lessons.array_value.push_back(LessonJson(id++, 0, -1, T_SERYANINA, 12, "Экономика", -1, false, false, {LESNAYA, KRIVOUSOVA}));
    lessons.array_value.push_back(LessonJson(id++, 1, -1, T_GARBUZOV, 12, "Экономика", -1, false, false, {LESNAYA, KRIVOUSOVA}));

    int pmId = subj++;
    for (int g = 0; g < 2; g++) {
        int bs = g * PARTS_PER_GROUP;
        lessons.array_value.push_back(LessonJson(id++, g, -1, T_GARBUZOV, 8, "МДК.01.01 теория", pmId, false, false, {LESNAYA, KRIVOUSOVA}));
        lessons.array_value.push_back(LessonJson(id++, g, bs, T_GARBUZOV, 23, "МДК.01.01 ЛПЗ", pmId, true, false, {LESNAYA, KRIVOUSOVA}));
        lessons.array_value.push_back(LessonJson(id++, g, bs + 1, T_GARBUZOV, 23, "МДК.01.01 ЛПЗ", pmId, true, false, {LESNAYA, KRIVOUSOVA}));
        lessons.array_value.push_back(LessonJson(id++, g, -1, T_GARBUZOV, 15, "МДК.01.01 КП", -1, false, false, {LESNAYA, KRIVOUSOVA}));
    }

    int dbId = subj++;
    for (int g = 0; g < 2; g++) {
        int bs = g * PARTS_PER_GROUP;
        lessons.array_value.push_back(LessonJson(id++, g, -1, T_SAMTSOVA, 36, "МДК.04.01 теория", dbId, false, false, {LESNAYA, KRIVOUSOVA}));
        lessons.array_value.push_back(LessonJson(id++, g, bs, T_GOBOV, 35, "МДК.04.01 ЛПЗ", dbId, true, false, {LESNAYA, KRIVOUSOVA}));
        lessons.array_value.push_back(LessonJson(id++, g, bs + 1, T_GOBOV, 35, "МДК.04.01 ЛПЗ", dbId, true, false, {LESNAYA, KRIVOUSOVA}));
        lessons.array_value.push_back(LessonJson(id++, g, bs, T_SAMTSOVA, 36, "УП.04", -1, false, true, {LESNAYA, KRIVOUSOVA}));
        lessons.array_value.push_back(LessonJson(id++, g, bs + 1, T_SAMTSOVA, 36, "УП.04", -1, false, true, {LESNAYA, KRIVOUSOVA}));
    }

    int autoId = subj++;
    for (int g = 0; g < 2; g++) {
        int bs = g * PARTS_PER_GROUP;
        lessons.array_value.push_back(LessonJson(id++, g, -1, T_GARBUZOV, 16, "ВМДК.05.01 теория", autoId, false, false, {LESNAYA, KRIVOUSOVA}));
        lessons.array_value.push_back(LessonJson(id++, g, bs, T_GOBOV, 19, "ВМДК.05.01 ЛПЗ", autoId, true, false, {LESNAYA, KRIVOUSOVA}));
        lessons.array_value.push_back(LessonJson(id++, g, bs + 1, T_GOBOV, 19, "ВМДК.05.01 ЛПЗ", autoId, true, false, {LESNAYA, KRIVOUSOVA}));
        lessons.array_value.push_back(LessonJson(id++, g, bs, T_GOBOV, 18, "УП.05", -1, false, true, {LESNAYA, KRIVOUSOVA}));
        lessons.array_value.push_back(LessonJson(id++, g, bs + 1, T_GOBOV, 18, "УП.05", -1, false, true, {LESNAYA, KRIVOUSOVA}));
    }
    root.At("lessons") = lessons;

    return root;
}

void NormalizeDataRoot(JsonValue& root) {
    if (!root.IsObject()) root = JsonValue::MakeObject();

    JsonValue& meta = root.At("meta");
    if (!meta.IsObject()) meta = JsonValue::MakeObject();
    meta.At("schema_version") = JsonValue::MakeNumber(4);

    for (const std::string& key : {
        "groups", "teachers", "rooms", "lessons", "unavailable", "teacher_unavailable",
        "substitutions", "accounting_adjustments", "workload_imports"
    }) {
        if (!root.At(key).IsArray()) root.At(key) = JsonValue::MakeArray();
    }

    JsonValue& room_types = root.At("room_types");
    if (!room_types.IsArray() || room_types.array_value.empty()) {
        room_types = JsonValue::MakeArray();
        room_types.array_value.push_back(RoomTypeJson(1, "Лекционная аудитория", "Обычные теоретические занятия, доска и посадочные места"));
        room_types.array_value.push_back(RoomTypeJson(2, "Мастерская со станками", "Практические занятия, для которых необходимы станки"));
        room_types.array_value.push_back(RoomTypeJson(3, "Компьютерный класс", "Занятия, для которых необходимы персональные компьютеры"));
    }
    for (JsonValue& item : room_types.array_value) {
        if (!item.IsObject()) continue;
        const int id = JsonInt(item, "id", 1);
        if (!item.At("uid").IsString() || item.At("uid").string_value.empty())
            item.At("uid") = JsonValue::MakeString(StableUid("room-type", std::to_string(id)));
        if (!item.At("name").IsString()) item.At("name") = JsonValue::MakeString("Тип " + std::to_string(id));
        if (!item.At("description").IsString()) item.At("description") = JsonValue::MakeString("");
    }

    JsonValue& campuses = root.At("campuses");
    if (!campuses.IsArray() || campuses.array_value.empty()) {
        campuses = JsonValue::MakeArray();
        JsonValue lesnaya = JsonValue::MakeObject();
        lesnaya.At("id") = JsonValue::MakeNumber(LESNAYA);
        lesnaya.At("uid") = JsonValue::MakeString("campus-lesnaya");
        lesnaya.At("name") = JsonValue::MakeString("Лесная");
        campuses.array_value.push_back(lesnaya);
        JsonValue krivousova = JsonValue::MakeObject();
        krivousova.At("id") = JsonValue::MakeNumber(KRIVOUSOVA);
        krivousova.At("uid") = JsonValue::MakeString("campus-krivousova-53");
        krivousova.At("name") = JsonValue::MakeString("Кривоусова, 53");
        campuses.array_value.push_back(krivousova);
    }

    for (JsonValue& item : root.At("groups").array_value) {
        if (!item.IsObject()) continue;
        const std::string name = JsonString(item, "name", "group-" + std::to_string(JsonInt(item, "id", 0)));
        if (!item.At("uid").IsString() || item.At("uid").string_value.empty())
            item.At("uid") = JsonValue::MakeString(StableUid("group", name));
        if (!item.At("parts").IsNumber()) item.At("parts") = JsonValue::MakeNumber(2);
        if (!item.At("size").IsNumber()) item.At("size") = JsonValue::MakeNumber(0);
        if (!item.At("home_campus").IsNumber()) item.At("home_campus") = JsonValue::MakeNumber(-1);
        if (!item.At("curator_teacher").IsNumber()) item.At("curator_teacher") = JsonValue::MakeNumber(-1);
        if (!item.At("class_hour_enabled").IsBool()) item.At("class_hour_enabled") = JsonValue::MakeBool(true);
        if (!item.At("class_hour_campus").IsNumber()) item.At("class_hour_campus") = JsonValue::MakeNumber(-1);
        item.At("class_hour_weekday") = JsonValue::MakeNumber(1);
        item.At("class_hour_slot") = JsonValue::MakeNumber(0);
        item.At("class_hour_from") = JsonValue::MakeString("07:50");
        item.At("class_hour_to") = JsonValue::MakeString("09:15");
        NormalizeWorkSchedule(item);
    }

    for (JsonValue& item : root.At("teachers").array_value) {
        if (!item.IsObject()) continue;
        const std::string name = JsonString(item, "name", "teacher-" + std::to_string(JsonInt(item, "id", 0)));
        if (!item.At("uid").IsString() || item.At("uid").string_value.empty())
            item.At("uid") = JsonValue::MakeString(StableUid("teacher", name));
        NormalizeWorkSchedule(item);
        if (!item.At("max_work_days_per_week").IsNumber())
            item.At("max_work_days_per_week") = JsonValue::MakeNumber(0);
        if (!item.At("max_pairs_per_day").IsNumber())
            item.At("max_pairs_per_day") = JsonValue::MakeNumber(0);
        if (!item.At("default_room").IsNumber()) item.At("default_room") = JsonValue::MakeNumber(-1);
        if (!item.At("campus_priority").IsArray()) item.At("campus_priority") = JsonValue::MakeArray();
        if (!item.At("allowed_campuses").IsArray()) item.At("allowed_campuses") = JsonValue::MakeArray();
        if (!item.At("room_responsibility").IsString()) item.At("room_responsibility") = JsonValue::MakeString("");
        if (!item.At("availability_note").IsString()) item.At("availability_note") = JsonValue::MakeString("");
    }

    for (JsonValue& item : root.At("rooms").array_value) {
        if (!item.IsObject()) continue;
        const std::string name = JsonString(item, "name", "room-" + std::to_string(JsonInt(item, "id", 0)));
        const int campus = JsonInt(item, "campus", LESNAYA);
        if (!item.At("uid").IsString() || item.At("uid").string_value.empty())
            item.At("uid") = JsonValue::MakeString(StableUid("room", std::to_string(campus) + ":" + name));
        if (!item.At("capacity").IsNumber()) item.At("capacity") = JsonValue::MakeNumber(0);
        if (!item.At("room_type").IsNumber()) item.At("room_type") = JsonValue::MakeNumber(0);
        if (!item.At("equipment").IsArray()) item.At("equipment") = JsonValue::MakeArray();
        if (!item.At("active").IsBool()) item.At("active") = JsonValue::MakeBool(true);
        NormalizeWorkSchedule(item);
        std::string access_mode = JsonString(item, "access_mode", "general");
        if (access_mode != "general" && access_mode != "exclusive" && access_mode != "blocked")
            access_mode = "general";
        item.At("access_mode") = JsonValue::MakeString(access_mode);
        if (!item.At("responsible_teacher_ids").IsArray())
            item.At("responsible_teacher_ids") = JsonValue::MakeArray();
        std::string purpose = JsonString(item, "purpose", "");
        if (purpose != "sports_hall") purpose.clear();
        item.At("purpose") = JsonValue::MakeString(purpose);
        if (access_mode == "blocked") item.At("active") = JsonValue::MakeBool(false);
    }

    for (JsonValue& item : root.At("lessons").array_value) {
        if (!item.IsObject()) continue;
        const std::string key = std::to_string(JsonInt(item, "group", 0)) + ":" +
            std::to_string(JsonInt(item, "subgroup", -1)) + ":" +
            JsonString(item, "name", "lesson") + ":" +
            std::to_string(JsonInt(item, "subject_id", -1));
        if (!item.At("uid").IsString() || item.At("uid").string_value.empty())
            item.At("uid") = JsonValue::MakeString(StableUid("lesson", key));
        if (!item.At("week_parity").IsString()) item.At("week_parity") = JsonValue::MakeString("all");
        if (!item.At("fixed_room").IsNumber()) item.At("fixed_room") = JsonValue::MakeNumber(-1);
        if (!item.At("allow_room_substitution").IsBool()) item.At("allow_room_substitution") = JsonValue::MakeBool(true);
        if (!item.At("required_capacity").IsNumber()) item.At("required_capacity") = JsonValue::MakeNumber(0);
        if (!item.At("required_room_type").IsNumber()) item.At("required_room_type") = JsonValue::MakeNumber(0);
        if (!item.At("required_equipment").IsArray()) item.At("required_equipment") = JsonValue::MakeArray();
        std::string required_purpose = JsonString(item, "required_room_purpose", "");
        if (JsonString(item, "name", "") == "Физическая культура") required_purpose = "sports_hall";
        if (required_purpose != "sports_hall") required_purpose.clear();
        item.At("required_room_purpose") = JsonValue::MakeString(required_purpose);
        if (!item.At("total_hours").IsNumber()) {
            const int multiplier = JsonBool(item, "is_block", false) ? 4 : 2;
            item.At("total_hours") = JsonValue::MakeNumber(JsonInt(item, "total_slots", 0) * multiplier);
        }
        if (!item.At("plan_active").IsBool()) item.At("plan_active") = JsonValue::MakeBool(true);
        // План часов остаётся в учёте, даже если конкретную строку временно
        // исключили из ближайшей генерации (например, диспетчер заранее
        // зафиксировал преподавателю другую недельную сетку).
        if (!item.At("generation_active").IsBool()) {
            // Старые импорты содержат служебные строки с нулевой недельной
            // квотой. Они нужны в плане часов, но не должны автоматически
            // попадать в ближайшую генерацию и ломать аудит.
            item.At("generation_active") = JsonValue::MakeBool(
                JsonInt(item, "total_slots", 0) > 0);
        }
    }

    for (const std::string& array_name : {"unavailable", "teacher_unavailable"}) {
        for (JsonValue& item : root.At(array_name).array_value) {
            if (!item.IsObject()) continue;
            const std::string key = std::to_string(JsonInt(item, "group", JsonInt(item, "teacher", -1))) + ":" +
                JsonString(item, "from", JsonString(item, "date", "")) + ":" +
                JsonString(item, "to", "") + ":" + JsonString(item, "text", "");
            if (!item.At("uid").IsString() || item.At("uid").string_value.empty())
                item.At("uid") = JsonValue::MakeString(StableUid(array_name, key));
        }
    }

    for (JsonValue& item : root.At("substitutions").array_value) {
        if (!item.IsObject()) continue;
        const std::string key = JsonString(item, "date", "") + ":" +
            std::to_string(JsonInt(item, "slot", 0)) + ":" +
            std::to_string(JsonInt(item, "lesson_id", -1)) + ":" +
            std::to_string(JsonInt(item, "absent_teacher", -1)) + ":" +
            std::to_string(JsonInt(item, "substitute_teacher", -1));
        if (!item.At("uid").IsString() || item.At("uid").string_value.empty())
            item.At("uid") = JsonValue::MakeString(StableUid("substitution", key));
        if (!item.At("hours").IsNumber()) item.At("hours") = JsonValue::MakeNumber(2);
        if (!item.At("status").IsString()) item.At("status") = JsonValue::MakeString("active");
        if (!item.At("reason").IsString()) item.At("reason") = JsonValue::MakeString("");
        if (!item.At("comment").IsString()) item.At("comment") = JsonValue::MakeString("");
    }
}

void EnsureDataFileExists() {
    std::lock_guard<std::recursive_mutex> lock(g_data_file_mutex);
    std::filesystem::path path(DataFilePath());
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        std::ifstream in(path, std::ios::binary);
        std::ostringstream current;
        current << in.rdbuf();
        // На Windows открытый ifstream не позволяет AtomicWriteTextFile
        // заменить исходный JSON через MoveFileExW.
        in.close();
        JsonParseResult parsed = ParseJson(current.str());
        if (parsed.ok && parsed.value.IsObject()) {
            const std::string before = ToJson(parsed.value, 2);
            NormalizeDataRoot(parsed.value);
            const std::string after = ToJson(parsed.value, 2);
            if (before != after) {
                std::string error;
                SaveDataJson(parsed.value, error, "Автоматическая миграция схемы данных");
            }
        }
        return;
    }
    JsonValue root = DefaultDataJson();
    NormalizeDataRoot(root);
    std::string error;
    AtomicWriteTextFile(path, ToJson(root, 2), error);
}

std::string ReadDataJsonText() {
    std::lock_guard<std::recursive_mutex> lock(g_data_file_mutex);
    EnsureDataFileExists();
    std::ifstream in(DataFilePath(), std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool SaveDataJson(const JsonValue& root, std::string& error, const std::string& reason) {
    std::lock_guard<std::recursive_mutex> lock(g_data_file_mutex);
    std::filesystem::path path(DataFilePath());
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    JsonValue normalized = root;
    NormalizeDataRoot(normalized);
    const std::string next_text = ToJson(normalized, 2);

    if (std::filesystem::exists(path, ec)) {
        std::ifstream current_in(path, std::ios::binary);
        std::ostringstream current_ss;
        current_ss << current_in.rdbuf();
        const std::string current_text = current_ss.str();
        if (!current_text.empty() && current_text != next_text) {
            const auto versions_dir = VersionsDir();
            std::filesystem::create_directories(versions_dir, ec);
            JsonValue snapshot = JsonValue::MakeObject();
            snapshot.At("created_at") = JsonValue::MakeString(VersionTimestamp());
            snapshot.At("reason") = JsonValue::MakeString(reason);
            JsonParseResult current_parsed = ParseJson(current_text);
            if (current_parsed.ok) snapshot.At("data") = current_parsed.value;
            else snapshot.At("raw") = JsonValue::MakeString(current_text);
            const auto version_file = versions_dir /
                ("version_" + VersionTimestamp() + ".json");
            std::string version_error;
            AtomicWriteTextFile(version_file, ToJson(snapshot, 2), version_error);
            PruneVersions();
        }
    }

    return AtomicWriteTextFile(path, next_text, error);
}

bool ParseDateIso(const std::string& text, Date& date) {
    if (text.size() != 10 || text[4] != '-' || text[7] != '-') return false;
    try {
        date.year = std::stoi(text.substr(0, 4));
        date.month = std::stoi(text.substr(5, 2));
        date.day = std::stoi(text.substr(8, 2));
        return date.month >= 1 && date.month <= 12 && date.day >= 1 && date.day <= 31;
    } catch (...) {
        return false;
    }
}

std::string DateToIso(const Date& date) {
    std::ostringstream ss;
    ss << date.year << "-";
    if (date.month < 10) ss << "0";
    ss << date.month << "-";
    if (date.day < 10) ss << "0";
    ss << date.day;
    return ss.str();
}

bool WorkScheduleAllows(const WorkSchedule& schedule, const Date& date, int zero_based_slot) {
    if (schedule.has_period && (date < schedule.from || date > schedule.to)) return false;
    const int day = DayOfWeek(date);
    if (day < 1 || day > 7 || zero_based_slot < 0 || zero_based_slot >= SLOTS_PER_DAY) return false;
    const auto override_it = schedule.date_slot_overrides.find(date);
    if (override_it != schedule.date_slot_overrides.end())
        return override_it->second.count(zero_based_slot + 1) != 0;
    const WorkDayWindow& window = schedule.days[day - 1];
    const int human_slot = zero_based_slot + 1;
    return window.enabled && window.slots.count(human_slot) != 0;
}

bool LoadScheduleInputData(ScheduleInputData& data, std::string& error) {
    EnsureDataFileExists();
    JsonParseResult parsed = ParseJson(ReadDataJsonText());
    if (!parsed.ok) {
        error = parsed.error;
        return false;
    }

    const JsonValue& root = parsed.value;
    if (!root.IsObject()) {
        error = "Файл данных должен быть JSON-объектом";
        return false;
    }

    const JsonValue& settings = root.At("settings");
    ParseDateIso(JsonString(settings, "start_date", "2026-01-12"), data.start_date);
    ParseDateIso(JsonString(settings, "end_date", "2026-06-19"), data.end_date);
    LoadSolverConfigFromJson(settings.At("solver_config"));

    data.teacher_period_targets.clear();
    const JsonValue& teacher_period_targets = settings.At("teacher_period_targets");
    if (teacher_period_targets.IsArray()) {
        for (const JsonValue& item : teacher_period_targets.array_value) {
            if (!item.IsObject()) continue;
            const int teacher = JsonInt(item, "teacher", -1);
            const int minimum_pairs = JsonInt(item, "minimum_pairs", 0);
            if (teacher < 0 || minimum_pairs <= 0) continue;
            // При случайном дубле выбираем более строгое значение.
            data.teacher_period_targets[teacher] = std::max(
                data.teacher_period_targets[teacher], minimum_pairs);
        }
    }

    data.prior_theory_pairs.clear();
    const JsonValue& prior_theory_pairs = settings.At("prior_theory_pairs");
    if (prior_theory_pairs.IsArray()) {
        for (const JsonValue& item : prior_theory_pairs.array_value) {
            if (!item.IsObject()) continue;
            const int group = JsonInt(item, "group", -1);
            const int subject = JsonInt(item, "subject", -1);
            const int pairs = JsonInt(item, "pairs", 0);
            if (group < 0 || subject < 0 || pairs <= 0) continue;
            const std::pair<int, int> key{group, subject};
            data.prior_theory_pairs[key] = std::max(data.prior_theory_pairs[key], pairs);
        }
    }

    data.groups.clear();
    const JsonValue& groups = root.At("groups");
    if (!groups.IsArray() || groups.array_value.empty()) {
        error = "Нужен непустой массив groups";
        return false;
    }
    for (const JsonValue& item : groups.array_value) {
        if (!item.IsObject()) continue;
        GroupData group;
        group.id = JsonInt(item, "id", static_cast<int>(data.groups.size()));
        group.uid = JsonString(item, "uid", "");
        group.name = JsonString(item, "name", "Группа " + std::to_string(group.id));
        group.parts = JsonInt(item, "parts", PARTS_PER_GROUP);
        group.size = JsonInt(item, "size", 0);
        group.home_campus = JsonInt(item, "home_campus", -1);
        group.curator_teacher = JsonInt(item, "curator_teacher", -1);
        group.class_hour_enabled = JsonBool(item, "class_hour_enabled", true);
        group.class_hour_campus = JsonInt(item, "class_hour_campus", -1);
        group.work_schedule = ParseWorkSchedule(item);
        data.groups.push_back(group);
    }

    data.teachers.clear();
    const JsonValue& teachers = root.At("teachers");
    if (!teachers.IsArray() || teachers.array_value.empty()) {
        error = "Нужен непустой массив teachers";
        return false;
    }
    for (const JsonValue& item : teachers.array_value) {
        if (!item.IsObject()) continue;
        TeacherData teacher;
        teacher.id = JsonInt(item, "id", static_cast<int>(data.teachers.size()));
        teacher.uid = JsonString(item, "uid", "");
        teacher.name = JsonString(item, "name", "Преподаватель " + std::to_string(teacher.id));
        teacher.work_schedule = ParseWorkSchedule(item);
        teacher.default_room = JsonInt(item, "default_room", -1);
        teacher.room_responsibility = JsonString(item, "room_responsibility", "");
        teacher.max_work_days_per_week = std::max(0, JsonInt(item, "max_work_days_per_week", 0));
        teacher.max_pairs_per_day = std::max(0, JsonInt(item, "max_pairs_per_day", 0));
        const JsonValue& priority = item.At("campus_priority");
        if (priority.IsArray()) {
            for (const JsonValue& value : priority.array_value) {
                if (!value.IsNumber()) continue;
                const int campus = static_cast<int>(std::llround(value.number_value));
                if ((campus == LESNAYA || campus == KRIVOUSOVA) &&
                    std::find(teacher.campus_priority.begin(), teacher.campus_priority.end(), campus) ==
                        teacher.campus_priority.end()) {
                    teacher.campus_priority.push_back(campus);
                }
            }
        }
        const JsonValue& allowed_campuses = item.At("allowed_campuses");
        if (allowed_campuses.IsArray()) {
            for (const JsonValue& value : allowed_campuses.array_value) {
                if (!value.IsNumber()) continue;
                const int campus = static_cast<int>(std::llround(value.number_value));
                if (campus == LESNAYA || campus == KRIVOUSOVA)
                    teacher.allowed_campuses.insert(campus);
            }
        }
        data.teachers.push_back(teacher);
    }

    data.rooms.clear();
    std::unordered_map<int, RoomData> rooms_by_id;
    const JsonValue& rooms = root.At("rooms");
    if (rooms.IsArray()) {
        for (const JsonValue& item : rooms.array_value) {
            if (!item.IsObject()) continue;
            RoomData room;
            room.id = JsonInt(item, "id", static_cast<int>(data.rooms.size()));
            room.uid = JsonString(item, "uid", "");
            room.name = JsonString(item, "name", "Кабинет " + std::to_string(room.id));
            room.campus = JsonInt(item, "campus", LESNAYA);
            room.capacity = JsonInt(item, "capacity", 0);
            room.room_type = JsonInt(item, "room_type", 0);
            room.active = JsonBool(item, "active", true);
            room.access_mode = JsonString(item, "access_mode", "general");
            if (room.access_mode != "general" && room.access_mode != "exclusive" &&
                room.access_mode != "blocked") room.access_mode = "general";
            if (room.access_mode == "blocked") room.active = false;
            room.purpose = JsonString(item, "purpose", "");
            if (room.purpose != "sports_hall") room.purpose.clear();
            room.work_schedule = ParseWorkSchedule(item);
            const JsonValue& responsible_teachers = item.At("responsible_teacher_ids");
            if (responsible_teachers.IsArray()) {
                for (const JsonValue& value : responsible_teachers.array_value) {
                    if (value.IsNumber())
                        room.responsible_teacher_ids.insert(
                            static_cast<int>(std::llround(value.number_value)));
                }
            }
            const JsonValue& available_slots = item.At("available_slots");
            if (available_slots.IsArray()) {
                for (const JsonValue& value : available_slots.array_value) {
                    if (value.IsNumber()) {
                        const int slot = static_cast<int>(value.number_value);
                        if (slot >= 1 && slot <= SLOTS_PER_DAY) room.available_slots.insert(slot);
                    }
                }
            }
            for (const std::string& value : JsonStringArray(item.At("equipment"))) {
                room.equipment.insert(value);
            }
            data.rooms.push_back(room);
            rooms_by_id[room.id] = room;
        }
    }

    data.unavailable.clear();
    data.teacher_unavailable.clear();
    data.unavailable_day_texts.clear();
    data.special_days.clear();
    const JsonValue& unavailable = root.At("unavailable");
    if (unavailable.IsArray()) {
        for (const JsonValue& item : unavailable.array_value) {
            if (!item.IsObject()) continue;

            std::vector<int> target_groups = TargetGroupsForUnavailable(item, data.groups);
            std::string text = JsonString(item, "text", "");
            std::vector<Date> exact_dates = DatesFromUnavailableItem(item);

            SpecialDayData special_day;
            special_day.id = JsonInt(item, "id", static_cast<int>(data.special_days.size()));
            special_day.all_groups = JsonBool(item, "all_groups", false);
            special_day.group = JsonInt(item, "group", -1);
            special_day.text = text;
            special_day.dates = exact_dates;

            Date from;
            Date to;
            bool has_range = ParseDateIso(JsonString(item, "from", ""), from) &&
                             ParseDateIso(JsonString(item, "to", ""), to);

            for (int group : target_groups) {
                for (const Date& date : exact_dates) {
                    data.unavailable[group].push_back({date, date});
                    if (!text.empty()) {
                        data.unavailable_day_texts[group][date] = text;
                    }
                }

                if (has_range) {
                    data.unavailable[group].push_back({from, to});
                    AddTextForRange(data.unavailable_day_texts[group], from, to, text);
                }
            }

            if (!exact_dates.empty() || has_range) {
                if (has_range) {
                    Date cur = from;
                    while (cur <= to) {
                        if (DayOfWeek(cur) != 7) {
                            special_day.dates.push_back(cur);
                        }
                        cur = NextDay(cur);
                    }
                    std::sort(special_day.dates.begin(), special_day.dates.end());
                    special_day.dates.erase(std::unique(special_day.dates.begin(), special_day.dates.end()), special_day.dates.end());
                }
                data.special_days.push_back(special_day);
            }
        }
    }

    const JsonValue& teacher_unavailable = root.At("teacher_unavailable");
    if (teacher_unavailable.IsArray()) {
        for (const JsonValue& item : teacher_unavailable.array_value) {
            if (!item.IsObject()) continue;
            const int teacher = JsonInt(item, "teacher", -1);
            if (teacher < 0) continue;
            const std::vector<Date> exact_dates = DatesFromUnavailableItem(item);
            for (const Date& date : exact_dates) {
                data.teacher_unavailable[teacher].push_back({date, date});
            }
            Date from;
            Date to;
            if (ParseDateIso(JsonString(item, "from", ""), from) &&
                ParseDateIso(JsonString(item, "to", ""), to)) {
                data.teacher_unavailable[teacher].push_back({from, to});
            }
        }
    }

    data.lessons.clear();
    std::unordered_map<int, int> teacher_default_rooms;
    std::unordered_map<int, std::set<int>> teacher_allowed_campuses;
    for (const TeacherData& teacher : data.teachers)
        teacher_default_rooms[teacher.id] = teacher.default_room;
    for (const TeacherData& teacher : data.teachers)
        teacher_allowed_campuses[teacher.id] = teacher.allowed_campuses;
    const JsonValue& lessons = root.At("lessons");
    if (!lessons.IsArray()) {
        error = "Нужен массив lessons";
        return false;
    }
    for (const JsonValue& item : lessons.array_value) {
        if (!item.IsObject()) continue;
        if (!JsonBool(item, "plan_active", true)) continue;
        if (!JsonBool(item, "generation_active", true)) continue;
        Lesson lesson;
        lesson.id = JsonInt(item, "id", static_cast<int>(data.lessons.size()));
        lesson.uid = JsonString(item, "uid", "");
        lesson.group = JsonInt(item, "group", 0);
        lesson.subgroup = JsonInt(item, "subgroup", -1);
        {
            const JsonValue& tv = item.At("teacher");
            lesson.teacher = tv.IsNumber() ? static_cast<int>(std::llround(tv.number_value)) : -1;
        }
        lesson.total_slots = JsonInt(item, "total_slots", 1);
        lesson.name = JsonString(item, "name", "Занятие");
        lesson.subject_id = JsonInt(item, "subject_id", -1);
        lesson.is_lab = JsonBool(item, "is_lab", false);
        lesson.is_block = JsonBool(item, "is_block", false);
        lesson.is_pp = JsonBool(item, "is_pp", false);
        lesson.week_parity = JsonString(item, "week_parity", "all");
        if (lesson.week_parity != "odd" && lesson.week_parity != "even") lesson.week_parity = "all";
        lesson.fixed_room = JsonInt(item, "fixed_room", -1);
        lesson.preferred_room = teacher_default_rooms.count(lesson.teacher)
            ? teacher_default_rooms[lesson.teacher] : -1;
        lesson.allow_room_substitution = JsonBool(item, "allow_room_substitution", true);
        lesson.required_capacity = JsonInt(item, "required_capacity", 0);
        lesson.required_room_type = JsonInt(item, "required_room_type", 0);
        lesson.required_room_purpose = JsonString(item, "required_room_purpose", "");
        if (lesson.name == "Физическая культура") lesson.required_room_purpose = "sports_hall";
        if (lesson.required_room_purpose != "sports_hall") lesson.required_room_purpose.clear();
        lesson.required_equipment.clear();
        for (const std::string& value : JsonStringArray(item.At("required_equipment"))) {
            lesson.required_equipment.insert(value);
        }
        auto room_it = rooms_by_id.find(lesson.fixed_room);
        if (room_it != rooms_by_id.end()) {
            lesson.fixed_room_name = room_it->second.name;
        }
        lesson.allowed_campuses.clear();
        const JsonValue& campuses = item.At("allowed_campuses");
        if (campuses.IsArray()) {
            for (const JsonValue& campus : campuses.array_value) {
                if (!campus.IsNumber()) continue;
                int c = static_cast<int>(campus.number_value);
                if (c == LESNAYA || c == KRIVOUSOVA) lesson.allowed_campuses.insert(static_cast<Campus>(c));
            }
        }
        if (lesson.allowed_campuses.empty()) {
            lesson.allowed_campuses.insert(LESNAYA);
            lesson.allowed_campuses.insert(KRIVOUSOVA);
        }
        if (room_it != rooms_by_id.end()) {
            lesson.allowed_campuses.clear();
            lesson.allowed_campuses.insert(static_cast<Campus>(room_it->second.campus));
        }
        // Ограничение преподавателя имеет приоритет над импортированным мягким
        // списком площадок занятия. Иначе CP-SAT мог перенести Цимфер, Силенок
        // и других владельцев кабинетов в чужой корпус.
        const auto teacher_campuses = teacher_allowed_campuses.find(lesson.teacher);
        if (teacher_campuses != teacher_allowed_campuses.end() &&
            !teacher_campuses->second.empty()) {
            lesson.allowed_campuses.clear();
            for (int campus : teacher_campuses->second)
                lesson.allowed_campuses.insert(static_cast<Campus>(campus));
        }
        data.lessons.push_back(lesson);
    }

    SetRuntimeNames(NamesFromGroups(data.groups), NamesFromTeachers(data.teachers));
    std::vector<int> curator_teachers;
    std::vector<int> home_campuses;
    std::vector<int> class_hour_campuses;
    std::vector<bool> class_hour_enabled;
    curator_teachers.reserve(data.groups.size());
    home_campuses.reserve(data.groups.size());
    class_hour_campuses.reserve(data.groups.size());
    class_hour_enabled.reserve(data.groups.size());
    for (const GroupData& group : data.groups) {
        curator_teachers.push_back(group.curator_teacher);
        home_campuses.push_back(group.home_campus);
        class_hour_campuses.push_back(group.class_hour_campus);
        class_hour_enabled.push_back(group.class_hour_enabled);
    }
    SetRuntimeGroupMetadata(curator_teachers, home_campuses, class_hour_campuses, class_hour_enabled);
    return true;
}

int NextId(const JsonValue& array_value) {
    int next = 0;
    if (!array_value.IsArray()) return next;
    for (const JsonValue& item : array_value.array_value) {
        if (!item.IsObject()) continue;
        next = std::max(next, JsonInt(item, "id", -1) + 1);
    }
    return next;
}

JsonValue* FindObjectById(JsonValue& array_value, int id) {
    if (!array_value.IsArray()) return nullptr;
    for (JsonValue& item : array_value.array_value) {
        if (item.IsObject() && JsonInt(item, "id", -1) == id) return &item;
    }
    return nullptr;
}

bool RemoveObjectById(JsonValue& array_value, int id) {
    if (!array_value.IsArray()) return false;
    auto old_size = array_value.array_value.size();
    array_value.array_value.erase(
        std::remove_if(array_value.array_value.begin(), array_value.array_value.end(), [id](const JsonValue& item) {
            return item.IsObject() && JsonInt(item, "id", -1) == id;
        }),
        array_value.array_value.end()
    );
    return array_value.array_value.size() != old_size;
}

JsonValue BuildDataAudit(const JsonValue& source_root) {
    JsonValue root = source_root;
    NormalizeDataRoot(root);
    JsonValue issues = JsonValue::MakeArray();

    const JsonValue& groups = root.At("groups");
    const JsonValue& teachers = root.At("teachers");
    const JsonValue& room_types = root.At("room_types");
    const JsonValue& rooms = root.At("rooms");
    const JsonValue& lessons = root.At("lessons");

    std::unordered_set<int> group_ids;
    std::unordered_set<int> teacher_ids;
    std::unordered_set<int> room_ids;
    std::unordered_set<std::string> all_uids;
    std::unordered_map<int, int> group_sizes;
    std::unordered_map<int, JsonValue> rooms_by_id;
    std::unordered_set<int> room_type_ids;

    auto audit_work_schedule = [&](const JsonValue& entity, const std::string& type, int id) {
        const JsonValue& period = entity.At("work_period");
        const std::string from_text = JsonString(period, "from", "");
        const std::string to_text = JsonString(period, "to", "");
        Date from{};
        Date to{};
        if (from_text.empty() != to_text.empty()) {
            AddIssue(issues, "error", "work_period_incomplete",
                "Для рабочего периода нужно указать обе даты", type, id);
        } else if (!from_text.empty() &&
            (!ParseDateIso(from_text, from) || !ParseDateIso(to_text, to) || to < from)) {
            AddIssue(issues, "error", "work_period_invalid",
                "Некорректный диапазон рабочего периода", type, id);
        }
        int enabled_days = 0;
        for (const JsonValue& day : entity.At("work_days").array_value) {
            const int number = JsonInt(day, "day", 0);
            const int first = JsonInt(day, "start_slot", 0);
            const int last = JsonInt(day, "end_slot", 0);
            if (number < 1 || number > 7 || first < 1 || last > SLOTS_PER_DAY || last < first) {
                AddIssue(issues, "error", "work_day_invalid",
                    "Некорректное рабочее окно дня недели", type, id);
            }
            if (JsonBool(day, "enabled", false)) enabled_days++;
        }
        if (enabled_days == 0)
            AddIssue(issues, "warning", "work_schedule_empty",
                "Не выбран ни один рабочий день", type, id);
    };

    auto register_uid = [&](const JsonValue& item, const std::string& type) {
        const std::string uid = JsonString(item, "uid", "");
        if (uid.empty()) {
            AddIssue(issues, "error", "missing_uid", "У сущности отсутствует стабильный uid", type, JsonInt(item, "id", -1));
        } else if (!all_uids.insert(type + ":" + uid).second) {
            AddIssue(issues, "error", "duplicate_uid", "Повторяется стабильный uid: " + uid, type, JsonInt(item, "id", -1));
        }
    };

    std::unordered_set<std::string> group_names;
    for (const JsonValue& group : groups.array_value) {
        const int id = JsonInt(group, "id", -1);
        const std::string name = JsonString(group, "name", "");
        register_uid(group, "group");
        if (id < 0 || !group_ids.insert(id).second)
            AddIssue(issues, "error", "duplicate_group_id", "Некорректный или повторяющийся ID группы", "group", id);
        if (name.empty()) AddIssue(issues, "error", "empty_group_name", "У группы не заполнено название", "group", id);
        if (!name.empty() && !group_names.insert(name).second)
            AddIssue(issues, "warning", "duplicate_group_name", "Повторяется название группы: " + name, "group", id);
        const int size = JsonInt(group, "size", 0);
        group_sizes[id] = size;
        if (size <= 0) AddIssue(issues, "info", "group_size_unknown", "Не указана численность группы " + name, "group", id);
        audit_work_schedule(group, "group", id);
    }

    std::unordered_set<std::string> teacher_names;
    for (const JsonValue& teacher : teachers.array_value) {
        const int id = JsonInt(teacher, "id", -1);
        const std::string name = JsonString(teacher, "name", "");
        register_uid(teacher, "teacher");
        if (id < 0 || !teacher_ids.insert(id).second)
            AddIssue(issues, "error", "duplicate_teacher_id", "Некорректный или повторяющийся ID преподавателя", "teacher", id);
        if (name.empty()) AddIssue(issues, "error", "empty_teacher_name", "У преподавателя не заполнено ФИО", "teacher", id);
        if (!name.empty() && !teacher_names.insert(name).second)
            AddIssue(issues, "warning", "duplicate_teacher_name", "Повторяется ФИО преподавателя: " + name, "teacher", id);
        audit_work_schedule(teacher, "teacher", id);
    }

    std::unordered_map<int, int> curator_group;
    for (const JsonValue& group : groups.array_value) {
        const int group_id = JsonInt(group, "id", -1);
        const int curator = JsonInt(group, "curator_teacher", -1);
        if (!JsonBool(group, "class_hour_enabled", true)) continue;
        if (curator < 0) {
            AddIssue(issues, "info", "group_curator_missing",
                "Для группы не назначен куратор — нулевой урок не будет опубликован", "group", group_id);
        } else if (!teacher_ids.count(curator)) {
            AddIssue(issues, "error", "group_curator_missing_teacher",
                "Куратор группы ссылается на отсутствующего преподавателя", "group", group_id);
        } else if (curator_group.count(curator)) {
            AddIssue(issues, "error", "curator_class_hour_conflict",
                "Один куратор назначен двум группам на понедельник 07:50–09:15", "group", group_id);
        } else {
            curator_group[curator] = group_id;
        }
        const int campus = JsonInt(group, "class_hour_campus", -1);
        if (campus != -1 && campus != LESNAYA && campus != KRIVOUSOVA)
            AddIssue(issues, "error", "class_hour_campus_invalid",
                "Для классного часа указана неизвестная площадка", "group", group_id);
    }

    std::unordered_set<std::string> room_names;
    for (const JsonValue& type : room_types.array_value) {
        const int id = JsonInt(type, "id", -1);
        register_uid(type, "room_type");
        if (id <= 0 || !room_type_ids.insert(id).second)
            AddIssue(issues, "error", "duplicate_room_type", "Код типа аудитории должен быть положительным и уникальным", "room_type", id);
        if (JsonString(type, "name", "").empty())
            AddIssue(issues, "error", "empty_room_type_name", "У типа аудитории не заполнено название", "room_type", id);
    }
    for (const JsonValue& room : rooms.array_value) {
        const int id = JsonInt(room, "id", -1);
        const std::string name = JsonString(room, "name", "");
        register_uid(room, "room");
        if (id < 0 || !room_ids.insert(id).second)
            AddIssue(issues, "error", "duplicate_room_id", "Некорректный или повторяющийся ID кабинета", "room", id);
        if (name.empty()) AddIssue(issues, "error", "empty_room_name", "У кабинета не заполнено название", "room", id);
        if (!name.empty() && !room_names.insert(std::to_string(JsonInt(room, "campus", 0)) + ":" + name).second)
            AddIssue(issues, "warning", "duplicate_room_name", "Повторяется кабинет в одном кампусе: " + name, "room", id);
        const int campus = JsonInt(room, "campus", -1);
        if (campus != LESNAYA && campus != KRIVOUSOVA)
            AddIssue(issues, "error", "invalid_room_campus", "У кабинета указан неизвестный кампус", "room", id);
        if (JsonInt(room, "capacity", 0) <= 0)
            AddIssue(issues, "info", "room_capacity_unknown", "Не указана вместимость кабинета " + name, "room", id);
        const int room_type = JsonInt(room, "room_type", 0);
        if (room_type > 0 && !room_type_ids.count(room_type))
            AddIssue(issues, "error", "room_type_missing", "Кабинет «" + name + "» ссылается на неизвестный тип " + std::to_string(room_type), "room", id);
        rooms_by_id[id] = room;
    }

    std::unordered_set<int> lesson_ids;
    std::unordered_map<int, long long> teacher_slots;
    std::unordered_map<int, long long> group_slots;
    for (const JsonValue& lesson : lessons.array_value) {
        const int id = JsonInt(lesson, "id", -1);
        const int group = JsonInt(lesson, "group", -1);
        const int teacher = JsonInt(lesson, "teacher", -1);
        const int total_slots = JsonInt(lesson, "total_slots", 0);
        const std::string name = JsonString(lesson, "name", "Занятие");
        register_uid(lesson, "lesson");
        if (id < 0 || !lesson_ids.insert(id).second)
            AddIssue(issues, "error", "duplicate_lesson_id", "Некорректный или повторяющийся ID занятия", "lesson", id);
        if (!JsonBool(lesson, "plan_active", true)) continue;
        const bool generation_active = JsonBool(lesson, "generation_active", true);
        if (!group_ids.count(group))
            AddIssue(issues, "error", "lesson_group_missing", "Занятие «" + name + "» ссылается на отсутствующую группу", "lesson", id);
        if (teacher >= 0 && !teacher_ids.count(teacher))
            AddIssue(issues, "error", "lesson_teacher_missing", "Занятие «" + name + "» ссылается на отсутствующего преподавателя", "lesson", id);
        if (teacher < 0)
            AddIssue(issues, "warning", "teacher_vacancy", "Для занятия «" + name + "» преподаватель не назначен", "lesson", id);
        if (generation_active && total_slots <= 0)
            AddIssue(issues, "error", "lesson_hours_invalid", "У занятия «" + name + "» отсутствует положительная нагрузка", "lesson", id);
        const std::string parity = JsonString(lesson, "week_parity", "all");
        if (parity != "all" && parity != "odd" && parity != "even")
            AddIssue(issues, "error", "invalid_week_parity", "Некорректная чётность недели у занятия «" + name + "»", "lesson", id);
        const int required_room_type = JsonInt(lesson, "required_room_type", 0);
        if (required_room_type > 0 && !room_type_ids.count(required_room_type))
            AddIssue(issues, "error", "lesson_room_type_missing", "Занятие «" + name + "» требует неизвестный тип аудитории " + std::to_string(required_room_type), "lesson", id);
        const std::string required_room_purpose = JsonString(lesson, "required_room_purpose", "");

        const int fixed_room = JsonInt(lesson, "fixed_room", -1);
        if (fixed_room >= 0) {
            const bool can_substitute = JsonBool(lesson, "allow_room_substitution", true);
            const std::string severity = can_substitute ? "warning" : "error";
            auto rit = rooms_by_id.find(fixed_room);
            if (rit == rooms_by_id.end()) {
                AddIssue(issues, severity, "lesson_room_missing", "Занятие «" + name + "» ссылается на отсутствующий кабинет; будет выполнен поиск замены", "lesson", id);
            } else {
                if (required_room_type > 0 && JsonInt(rit->second, "room_type", 1) != required_room_type)
                    AddIssue(issues, severity, "room_type_mismatch", "Тип закреплённого кабинета «" + JsonString(rit->second, "name", "") +
                        "» не соответствует занятию «" + name + "»; будет выполнен поиск замены", "lesson", id);
                if (JsonString(rit->second, "purpose", "") != required_room_purpose)
                    AddIssue(issues, severity, "room_purpose_mismatch", "Назначение закреплённой аудитории «" + JsonString(rit->second, "name", "") +
                        "» не соответствует занятию «" + name + "»; будет выполнен поиск замены", "lesson", id);
                int need = JsonInt(lesson, "required_capacity", group_sizes[group]);
                int capacity = JsonInt(rit->second, "capacity", 0);
                if (need > 0 && capacity > 0 && capacity < need)
                    AddIssue(issues, severity, "room_too_small", "Кабинет «" + JsonString(rit->second, "name", "") +
                        "» мал для занятия «" + name + "»: нужно " + std::to_string(need) + ", мест " + std::to_string(capacity), "lesson", id);
                std::unordered_set<std::string> available_equipment;
                for (const std::string& value : JsonStringArray(rit->second.At("equipment"))) available_equipment.insert(value);
                for (const std::string& required : JsonStringArray(lesson.At("required_equipment"))) {
                    if (!available_equipment.count(required))
                        AddIssue(issues, severity, "room_equipment_missing", "В кабинете нет оборудования «" + required +
                            "» для занятия «" + name + "»", "lesson", id);
                }
            }
        }

        const long long occupied = generation_active
            ? static_cast<long long>(total_slots) * (JsonBool(lesson, "is_block", false) ? 2 : 1)
            : 0;
        if (teacher >= 0) teacher_slots[teacher] += occupied;
        if (group >= 0) group_slots[group] += occupied;
    }

    Date start;
    Date end;
    const JsonValue& settings = root.At("settings");
    int available_slot_capacity = 0;
    if (!ParseDateIso(JsonString(settings, "start_date", ""), start) ||
        !ParseDateIso(JsonString(settings, "end_date", ""), end) || end < start) {
        AddIssue(issues, "error", "invalid_date_range", "Некорректный диапазон дат семестра");
    } else {
        available_slot_capacity = static_cast<int>(GenerateSchoolDays(start, end).size()) * SLOTS_PER_DAY;
        for (const auto& pair : teacher_slots) {
            if (pair.second > available_slot_capacity)
                AddIssue(issues, "error", "teacher_over_capacity", "Нагрузка преподавателя ID " +
                    std::to_string(pair.first) + " превышает физическую ёмкость семестра", "teacher", pair.first);
        }
        const int max_pairs = JsonInt(settings.At("solver_config"), "max_student_pairs_per_day", 5);
        const int group_capacity = static_cast<int>(GenerateSchoolDays(start, end).size()) * max_pairs;
        for (const auto& pair : group_slots) {
            if (pair.second > group_capacity * PARTS_PER_GROUP)
                AddIssue(issues, "error", "group_over_capacity", "Нагрузка группы ID " +
                    std::to_string(pair.first) + " превышает доступное число пар", "group", pair.first);
        }
    }

    const JsonValue& teacher_unavailable = root.At("teacher_unavailable");
    for (const JsonValue& item : teacher_unavailable.array_value) {
        const int teacher = JsonInt(item, "teacher", -1);
        if (!teacher_ids.count(teacher))
            AddIssue(issues, "error", "unavailable_teacher_missing", "Недоступность ссылается на отсутствующего преподавателя", "teacher_unavailable", JsonInt(item, "id", -1));
        Date from;
        Date to;
        const bool has_range = ParseDateIso(JsonString(item, "from", ""), from) && ParseDateIso(JsonString(item, "to", ""), to);
        const bool has_dates = item.At("dates").IsArray() && !item.At("dates").array_value.empty();
        if (!has_range && !has_dates)
            AddIssue(issues, "error", "unavailable_dates_missing", "Не заданы даты недоступности преподавателя", "teacher_unavailable", JsonInt(item, "id", -1));
        if (has_range && to < from)
            AddIssue(issues, "error", "unavailable_range_invalid", "Дата окончания недоступности раньше даты начала", "teacher_unavailable", JsonInt(item, "id", -1));
    }

    std::unordered_map<int, int> lesson_teacher;
    for (const JsonValue& lesson : lessons.array_value)
        lesson_teacher[JsonInt(lesson, "id", -1)] = JsonInt(lesson, "teacher", -1);
    for (const JsonValue& teacher : teachers.array_value) {
        const int id = JsonInt(teacher, "id", -1);
        const int room = JsonInt(teacher, "default_room", -1);
        if (room >= 0 && !room_ids.count(room))
            AddIssue(issues, "warning", "teacher_default_room_missing",
                "Закреплённый кабинет преподавателя ещё не добавлен", "teacher", id);
        std::set<int> priorities;
        for (const JsonValue& campus : teacher.At("campus_priority").array_value) {
            const int value = campus.IsNumber() ? static_cast<int>(std::llround(campus.number_value)) : -1;
            if ((value != LESNAYA && value != KRIVOUSOVA) || !priorities.insert(value).second)
                AddIssue(issues, "error", "teacher_campus_priority_invalid",
                    "Некорректный приоритет площадок преподавателя", "teacher", id);
        }
    }

    std::unordered_set<int> substitution_ids;
    std::set<std::string> substitution_events;
    for (const JsonValue& item : root.At("substitutions").array_value) {
        const int id = JsonInt(item, "id", -1);
        const int lesson = JsonInt(item, "lesson_id", -1);
        const int absent = JsonInt(item, "absent_teacher", -1);
        const int substitute = JsonInt(item, "substitute_teacher", -1);
        if (id < 0 || !substitution_ids.insert(id).second)
            AddIssue(issues, "error", "duplicate_substitution_id", "Некорректный ID замены", "substitution", id);
        if (!lesson_ids.count(lesson))
            AddIssue(issues, "error", "substitution_lesson_missing", "Замена ссылается на отсутствующее занятие", "substitution", id);
        if (!teacher_ids.count(absent) || !teacher_ids.count(substitute))
            AddIssue(issues, "error", "substitution_teacher_missing", "В замене указан отсутствующий преподаватель", "substitution", id);
        if (absent == substitute)
            AddIssue(issues, "error", "substitution_same_teacher", "Преподаватель не может заменять сам себя", "substitution", id);
        const std::string event_key = std::to_string(lesson) + "|" +
            JsonString(item, "date", "") + "|" + std::to_string(JsonInt(item, "slot", 0));
        if (JsonString(item, "status", "active") == "active" && !substitution_events.insert(event_key).second)
            AddIssue(issues, "error", "duplicate_active_substitution",
                "На одно занятие назначено несколько активных замен", "substitution", id);
        if (lesson_teacher.count(lesson) && lesson_teacher[lesson] >= 0 && lesson_teacher[lesson] != absent)
            AddIssue(issues, "warning", "substitution_absent_mismatch", "Отсутствующий не совпадает с преподавателем занятия", "substitution", id);
        Date date{};
        if (!ParseDateIso(JsonString(item, "date", ""), date) ||
            JsonInt(item, "slot", 0) < 1 || JsonInt(item, "slot", 0) > SLOTS_PER_DAY ||
            JsonInt(item, "hours", 0) <= 0)
            AddIssue(issues, "error", "substitution_event_invalid", "У замены некорректны дата, пара или часы", "substitution", id);
    }

    int errors = 0;
    int warnings = 0;
    int infos = 0;
    for (const JsonValue& issue : issues.array_value) {
        const std::string severity = JsonString(issue, "severity", "info");
        if (severity == "error") errors++;
        else if (severity == "warning") warnings++;
        else infos++;
    }

    JsonValue result = JsonValue::MakeObject();
    result.At("ok") = JsonValue::MakeBool(errors == 0);
    JsonValue summary = JsonValue::MakeObject();
    summary.At("errors") = JsonValue::MakeNumber(errors);
    summary.At("warnings") = JsonValue::MakeNumber(warnings);
    summary.At("info") = JsonValue::MakeNumber(infos);
    summary.At("groups") = JsonValue::MakeNumber(groups.array_value.size());
    summary.At("teachers") = JsonValue::MakeNumber(teachers.array_value.size());
    summary.At("room_types") = JsonValue::MakeNumber(room_types.array_value.size());
    summary.At("rooms") = JsonValue::MakeNumber(rooms.array_value.size());
    summary.At("lessons") = JsonValue::MakeNumber(lessons.array_value.size());
    summary.At("semester_slot_capacity") = JsonValue::MakeNumber(available_slot_capacity);
    result.At("summary") = summary;
    result.At("issues") = issues;
    return result;
}

JsonValue BuildHoursReport(const JsonValue& source_root, const std::string& schedule_file) {
    JsonValue root = source_root;
    NormalizeDataRoot(root);
    struct Occurrence {
        int lesson = -1;
        int group = -1;
        int teacher = -1;
        std::string date;
        int slot = 0;
        int week = 0;
        std::string room;
        int room_type = 0;
        bool room_substituted = false;
        std::string requested_room;
        std::string room_substitution_reason;
    };
    std::vector<Occurrence> occurrences;
    std::unordered_map<int, int> scheduled_slots;
    std::map<int, std::pair<std::string, std::string>> week_ranges;
    Date semester_start{};
    Date semester_end{};
    const std::string semester_start_iso = JsonString(root.At("settings"), "start_date", "");
    const std::string semester_end_iso = JsonString(root.At("settings"), "end_date", "");
    const bool has_semester_start = ParseDateIso(semester_start_iso, semester_start);
    const bool has_semester_end = ParseDateIso(semester_end_iso, semester_end);
    if (has_semester_start && has_semester_end && semester_start <= semester_end) {
        Date week_start = semester_start;
        int week = 0;
        while (week_start <= semester_end) {
            Date week_end = week_start;
            for (int day = 0; day < 6 && week_end < semester_end; ++day) week_end = NextDay(week_end);
            week_ranges[week++] = {DateToIso(week_start), DateToIso(week_end)};
            week_start = NextDay(week_end);
        }
    }

    std::ifstream schedule_in(schedule_file, std::ios::binary);
    if (schedule_in) {
        std::ostringstream ss;
        ss << schedule_in.rdbuf();
        JsonParseResult parsed = ParseJson(ss.str());
        if (parsed.ok) {
            const JsonValue& schedule_groups = parsed.value.At("groups");
            if (schedule_groups.IsArray()) {
                for (const JsonValue& group : schedule_groups.array_value) {
                    const int group_id = JsonInt(group, "group_index", -1);
                    for (const JsonValue& day : group.At("days").array_value) {
                        const std::string date_iso = JsonString(day, "date_iso", "");
                        Date date{};
                        const int week = ParseDateIso(date_iso, date) && semester_start.year > 0
                            ? std::max(0, WeekIndexFromStart(semester_start, date)) : 0;
                        auto& range = week_ranges[week];
                        if (range.first.empty() || date_iso < range.first) range.first = date_iso;
                        if (range.second.empty() || date_iso > range.second) range.second = date_iso;
                        for (const JsonValue& slot : day.At("slots").array_value) {
                            for (const JsonValue& lesson : slot.At("lessons").array_value) {
                                const int id = JsonInt(lesson, "id", -1);
                                if (id < 0) continue;
                                scheduled_slots[id]++;
                                occurrences.push_back({id, group_id, -1, date_iso,
                                    JsonInt(slot, "slot", 0), week,
                                    JsonString(lesson, "room_name", ""),
                                    JsonInt(lesson, "room_type", 0),
                                    JsonBool(lesson, "room_substituted", false),
                                    JsonString(lesson, "requested_room_name", ""),
                                    JsonString(lesson, "room_substitution_reason", "")});
                            }
                        }
                    }
                }
            }
        }
    }

    std::unordered_map<int, std::string> group_names;
    for (const JsonValue& group : root.At("groups").array_value)
        group_names[JsonInt(group, "id", -1)] = JsonString(group, "name", "");
    std::unordered_map<int, std::string> teacher_names;
    for (const JsonValue& teacher : root.At("teachers").array_value)
        teacher_names[JsonInt(teacher, "id", -1)] = JsonString(teacher, "name", "");

    struct Totals {
        int planned = 0;
        int scheduled = 0;
        int credited = 0;
        int substitution_in = 0;
        int substitution_out = 0;
        int adjustment = 0;
        std::map<int, int> weekly;
    };
    std::unordered_map<int, Totals> by_group;
    std::unordered_map<int, Totals> by_teacher;
    JsonValue lesson_rows = JsonValue::MakeArray();

    for (const JsonValue& lesson : root.At("lessons").array_value) {
        const int id = JsonInt(lesson, "id", -1);
        const int group = JsonInt(lesson, "group", -1);
        const int teacher = JsonInt(lesson, "teacher", -1);
        const int planned_hours = JsonBool(lesson, "plan_active", true)
            ? JsonInt(lesson, "total_hours",
                JsonInt(lesson, "total_slots", 0) * (JsonBool(lesson, "is_block", false) ? 4 : 2))
            : 0;
        const int actual_hours = scheduled_slots[id] * 2;
        by_group[group].planned += planned_hours;
        by_group[group].scheduled += actual_hours;
        by_group[group].credited += actual_hours;
        if (teacher >= 0) {
            by_teacher[teacher].planned += planned_hours;
            by_teacher[teacher].scheduled += actual_hours;
            by_teacher[teacher].credited += actual_hours;
        }
        JsonValue row = JsonValue::MakeObject();
        row.At("lesson_id") = JsonValue::MakeNumber(id);
        row.At("lesson_uid") = JsonValue::MakeString(JsonString(lesson, "uid", ""));
        row.At("name") = JsonValue::MakeString(JsonString(lesson, "name", ""));
        row.At("subject_id") = JsonValue::MakeNumber(JsonInt(lesson, "subject_id", id));
        row.At("subgroup") = JsonValue::MakeNumber(JsonInt(lesson, "subgroup", -1));
        row.At("is_lab") = JsonValue::MakeBool(JsonBool(lesson, "is_lab", false));
        row.At("is_block") = JsonValue::MakeBool(JsonBool(lesson, "is_block", false));
        row.At("group_id") = JsonValue::MakeNumber(group);
        row.At("group_name") = JsonValue::MakeString(group_names[group]);
        if (teacher >= 0) row.At("teacher_id") = JsonValue::MakeNumber(teacher);
        else row.At("teacher_id") = JsonValue::MakeNull();
        row.At("teacher_name") = JsonValue::MakeString(teacher >= 0 ? teacher_names[teacher] : "вакансия");
        row.At("planned_hours") = JsonValue::MakeNumber(planned_hours);
        row.At("scheduled_hours") = JsonValue::MakeNumber(actual_hours);
        row.At("credited_hours") = JsonValue::MakeNumber(actual_hours);
        row.At("remaining_hours") = JsonValue::MakeNumber(planned_hours - actual_hours);
        lesson_rows.array_value.push_back(row);
    }

    std::unordered_map<int, int> lesson_teachers;
    std::unordered_map<int, std::string> lesson_names;
    for (const JsonValue& lesson : root.At("lessons").array_value) {
        const int lesson_id = JsonInt(lesson, "id", -1);
        lesson_teachers[lesson_id] = JsonInt(lesson, "teacher", -1);
        lesson_names[lesson_id] = JsonString(lesson, "name", "");
    }
    for (Occurrence& occurrence : occurrences) {
        occurrence.teacher = lesson_teachers.count(occurrence.lesson) ? lesson_teachers[occurrence.lesson] : -1;
        by_group[occurrence.group].weekly[occurrence.week] += 2;
        if (occurrence.teacher >= 0) by_teacher[occurrence.teacher].weekly[occurrence.week] += 2;
    }

    auto occurrence_week = [&](int lesson, const std::string& date, int slot) {
        for (const Occurrence& occurrence : occurrences) {
            if (occurrence.lesson == lesson && occurrence.date == date &&
                (slot <= 0 || occurrence.slot == slot)) return occurrence.week;
        }
        Date parsed_date{};
        return ParseDateIso(date, parsed_date) && semester_start.year > 0
            ? std::max(0, WeekIndexFromStart(semester_start, parsed_date)) : 0;
    };

    std::set<std::string> applied_substitution_events;
    std::map<std::string, const JsonValue*> substitution_by_event;
    for (const JsonValue& substitution : root.At("substitutions").array_value) {
        if (JsonString(substitution, "status", "active") != "active") continue;
        const std::string event_key = std::to_string(JsonInt(substitution, "lesson_id", -1)) + "|" +
            JsonString(substitution, "date", "") + "|" + std::to_string(JsonInt(substitution, "slot", 0));
        if (!applied_substitution_events.insert(event_key).second) continue;
        substitution_by_event[event_key] = &substitution;
        const int absent = JsonInt(substitution, "absent_teacher", -1);
        const int substitute = JsonInt(substitution, "substitute_teacher", -1);
        const int hours = std::max(0, JsonInt(substitution, "hours", 2));
        const int week = occurrence_week(JsonInt(substitution, "lesson_id", -1),
            JsonString(substitution, "date", ""), JsonInt(substitution, "slot", 0));
        if (absent >= 0) {
            by_teacher[absent].credited -= hours;
            by_teacher[absent].substitution_out += hours;
            by_teacher[absent].weekly[week] -= hours;
        }
        if (substitute >= 0) {
            by_teacher[substitute].credited += hours;
            by_teacher[substitute].substitution_in += hours;
            by_teacher[substitute].weekly[week] += hours;
        }
    }

    for (const JsonValue& adjustment : root.At("accounting_adjustments").array_value) {
        if (JsonString(adjustment, "status", "active") != "active") continue;
        const int teacher = JsonInt(adjustment, "teacher", -1);
        const int hours = JsonInt(adjustment, "hours", 0);
        if (teacher < 0 || hours == 0) continue;
        const int week = occurrence_week(-1, JsonString(adjustment, "date", ""), 0);
        by_teacher[teacher].credited += hours;
        by_teacher[teacher].adjustment += hours;
        by_teacher[teacher].weekly[week] += hours;
    }

    std::unordered_map<int, std::vector<JsonValue>> scheduled_by_teacher;
    std::unordered_map<int, std::vector<JsonValue>> credited_by_teacher;
    std::unordered_map<int, std::vector<JsonValue>> occurrences_by_group;
    std::unordered_map<int, std::vector<JsonValue>> occurrences_by_lesson;
    for (const Occurrence& occurrence : occurrences) {
        const std::string event_key = std::to_string(occurrence.lesson) + "|" +
            occurrence.date + "|" + std::to_string(occurrence.slot);
        const JsonValue* substitution = substitution_by_event.count(event_key)
            ? substitution_by_event[event_key] : nullptr;
        const int actual_teacher = substitution
            ? JsonInt(*substitution, "substitute_teacher", occurrence.teacher)
            : occurrence.teacher;

        JsonValue item = JsonValue::MakeObject();
        item.At("date") = JsonValue::MakeString(occurrence.date);
        item.At("slot") = JsonValue::MakeNumber(occurrence.slot);
        item.At("week_index") = JsonValue::MakeNumber(occurrence.week + 1);
        item.At("hours") = JsonValue::MakeNumber(2);
        item.At("lesson_id") = JsonValue::MakeNumber(occurrence.lesson);
        item.At("lesson_name") = JsonValue::MakeString(lesson_names[occurrence.lesson]);
        item.At("group_id") = JsonValue::MakeNumber(occurrence.group);
        item.At("group_name") = JsonValue::MakeString(group_names[occurrence.group]);
        item.At("teacher_id") = JsonValue::MakeNumber(occurrence.teacher);
        item.At("teacher_name") = JsonValue::MakeString(
            occurrence.teacher >= 0 ? teacher_names[occurrence.teacher] : "вакансия");
        item.At("actual_teacher_id") = JsonValue::MakeNumber(actual_teacher);
        item.At("actual_teacher_name") = JsonValue::MakeString(
            actual_teacher >= 0 ? teacher_names[actual_teacher] : "вакансия");
        item.At("room") = JsonValue::MakeString(occurrence.room);
        item.At("room_type") = JsonValue::MakeNumber(occurrence.room_type);
        item.At("room_substituted") = JsonValue::MakeBool(occurrence.room_substituted);
        item.At("requested_room") = JsonValue::MakeString(occurrence.requested_room);
        item.At("room_substitution_reason") = JsonValue::MakeString(occurrence.room_substitution_reason);
        item.At("is_substitution") = JsonValue::MakeBool(substitution != nullptr);

        occurrences_by_group[occurrence.group].push_back(item);
        occurrences_by_lesson[occurrence.lesson].push_back(item);
        if (occurrence.teacher >= 0) scheduled_by_teacher[occurrence.teacher].push_back(item);
        if (actual_teacher >= 0) credited_by_teacher[actual_teacher].push_back(item);
    }

    auto sort_occurrences = [](auto& map) {
        for (auto& entry : map) {
            std::sort(entry.second.begin(), entry.second.end(), [](const JsonValue& left, const JsonValue& right) {
                const std::string left_date = JsonString(left, "date", "");
                const std::string right_date = JsonString(right, "date", "");
                if (left_date != right_date) return left_date < right_date;
                if (JsonInt(left, "slot", 0) != JsonInt(right, "slot", 0))
                    return JsonInt(left, "slot", 0) < JsonInt(right, "slot", 0);
                return JsonInt(left, "lesson_id", -1) < JsonInt(right, "lesson_id", -1);
            });
        }
    };
    sort_occurrences(scheduled_by_teacher);
    sort_occurrences(credited_by_teacher);
    sort_occurrences(occurrences_by_group);
    sort_occurrences(occurrences_by_lesson);

    const std::vector<int> week_ids = [&]() {
        std::set<int> ids;
        for (const auto& item : week_ranges) ids.insert(item.first);
        for (const auto& item : by_teacher) for (const auto& week : item.second.weekly) ids.insert(week.first);
        return std::vector<int>(ids.begin(), ids.end());
    }();

    auto totals_to_json = [&week_ids](const std::unordered_map<int, Totals>& totals,
                             const std::unordered_map<int, std::string>& names,
                             const std::string& id_key, const std::string& name_key) {
        JsonValue array = JsonValue::MakeArray();
        std::vector<int> ids;
        for (const auto& pair : names) if (pair.first >= 0) ids.push_back(pair.first);
        std::sort(ids.begin(), ids.end());
        for (int id : ids) {
            Totals value;
            auto tit = totals.find(id);
            if (tit != totals.end()) value = tit->second;
            JsonValue row = JsonValue::MakeObject();
            row.At(id_key) = JsonValue::MakeNumber(id);
            auto nit = names.find(id);
            row.At(name_key) = JsonValue::MakeString(nit == names.end() ? "" : nit->second);
            row.At("planned_hours") = JsonValue::MakeNumber(value.planned);
            row.At("scheduled_hours") = JsonValue::MakeNumber(value.scheduled);
            row.At("credited_hours") = JsonValue::MakeNumber(value.credited);
            row.At("substitution_in_hours") = JsonValue::MakeNumber(value.substitution_in);
            row.At("substitution_out_hours") = JsonValue::MakeNumber(value.substitution_out);
            row.At("adjustment_hours") = JsonValue::MakeNumber(value.adjustment);
            row.At("remaining_hours") = JsonValue::MakeNumber(value.planned - value.credited);
            JsonValue weekly = JsonValue::MakeArray();
            for (int week : week_ids) {
                auto wit = value.weekly.find(week);
                weekly.array_value.push_back(JsonValue::MakeNumber(
                    wit == value.weekly.end() ? 0 : wit->second));
            }
            row.At("weekly_hours") = weekly;
            array.array_value.push_back(row);
        }
        return array;
    };

    auto vector_to_json = [](const std::vector<JsonValue>& values) {
        JsonValue array = JsonValue::MakeArray();
        for (const JsonValue& value : values) array.array_value.push_back(value);
        return array;
    };

    auto attach_occurrences = [&vector_to_json](
            JsonValue& rows, const std::string& id_key,
            const std::unordered_map<int, std::vector<JsonValue>>& source,
            const std::string& output_key) {
        for (JsonValue& row : rows.array_value) {
            const int id = JsonInt(row, id_key, -1);
            auto it = source.find(id);
            row.At(output_key) = it == source.end()
                ? JsonValue::MakeArray() : vector_to_json(it->second);
        }
    };

    JsonValue result = JsonValue::MakeObject();
    JsonValue weeks = JsonValue::MakeArray();
    for (int week : week_ids) {
        JsonValue item = JsonValue::MakeObject();
        item.At("index") = JsonValue::MakeNumber(week + 1);
        auto it = week_ranges.find(week);
        item.At("from") = JsonValue::MakeString(it == week_ranges.end() ? "" : it->second.first);
        item.At("to") = JsonValue::MakeString(it == week_ranges.end() ? "" : it->second.second);
        weeks.array_value.push_back(item);
    }
    JsonValue group_rows = totals_to_json(by_group, group_names, "group_id", "group_name");
    JsonValue teacher_rows = totals_to_json(by_teacher, teacher_names, "teacher_id", "teacher_name");
    attach_occurrences(group_rows, "group_id", occurrences_by_group, "scheduled_occurrences");
    attach_occurrences(teacher_rows, "teacher_id", scheduled_by_teacher, "scheduled_occurrences");
    attach_occurrences(teacher_rows, "teacher_id", credited_by_teacher, "credited_occurrences");
    attach_occurrences(lesson_rows, "lesson_id", occurrences_by_lesson, "scheduled_occurrences");

    result.At("weeks") = weeks;
    result.At("semester_start") = JsonValue::MakeString(semester_start_iso);
    result.At("semester_end") = JsonValue::MakeString(semester_end_iso);
    result.At("lessons") = lesson_rows;
    result.At("groups") = group_rows;
    result.At("teachers") = teacher_rows;
    result.At("schedule_found") = JsonValue::MakeBool(std::filesystem::exists(schedule_file));
    return result;
}

JsonValue BuildTeacherOccupancyReport(const JsonValue& source_root, const std::string& schedule_file) {
    JsonValue root = source_root;
    NormalizeDataRoot(root);
    std::unordered_map<int, std::string> teacher_names;
    std::unordered_map<int, std::string> group_names;
    std::unordered_map<int, std::string> lesson_names;
    std::unordered_map<int, int> lesson_teachers;
    for (const JsonValue& value : root.At("teachers").array_value)
        teacher_names[JsonInt(value, "id", -1)] = JsonString(value, "name", "");
    for (const JsonValue& value : root.At("groups").array_value)
        group_names[JsonInt(value, "id", -1)] = JsonString(value, "name", "");
    for (const JsonValue& value : root.At("lessons").array_value) {
        const int id = JsonInt(value, "id", -1);
        lesson_names[id] = JsonString(value, "name", "");
        lesson_teachers[id] = JsonInt(value, "teacher", -1);
    }

    std::map<std::string, const JsonValue*> substitutions;
    for (const JsonValue& value : root.At("substitutions").array_value) {
        if (JsonString(value, "status", "active") != "active") continue;
        const std::string key = std::to_string(JsonInt(value, "lesson_id", -1)) + "|" +
            JsonString(value, "date", "") + "|" + std::to_string(JsonInt(value, "slot", 0));
        substitutions[key] = &value;
    }

    JsonValue entries = JsonValue::MakeArray();
    std::ifstream input(schedule_file, std::ios::binary);
    std::ostringstream text;
    if (input) text << input.rdbuf();
    JsonParseResult parsed = ParseJson(text.str());
    if (parsed.ok) {
        for (const JsonValue& group : parsed.value.At("groups").array_value) {
            const int group_id = JsonInt(group, "group_index", -1);
            for (const JsonValue& day : group.At("days").array_value) {
                const std::string date = JsonString(day, "date_iso", "");
                Date parsed_date{};
                const int weekday = ParseDateIso(date, parsed_date) ? DayOfWeek(parsed_date) : 0;
                for (const JsonValue& slot : day.At("slots").array_value) {
                    const int slot_number = JsonInt(slot, "slot", 0);
                    for (const JsonValue& lesson : slot.At("lessons").array_value) {
                        const int lesson_id = JsonInt(lesson, "id", -1);
                        const bool is_class_hour = JsonBool(lesson, "is_class_hour", false);
                        const int original_teacher = is_class_hour
                            ? JsonInt(lesson, "teacher_id", -1)
                            : (lesson_teachers.count(lesson_id) ? lesson_teachers[lesson_id] : -1);
                        const std::string key = std::to_string(lesson_id) + "|" + date + "|" + std::to_string(slot_number);
                        const JsonValue* replacement = !is_class_hour && substitutions.count(key) ? substitutions[key] : nullptr;
                        const int actual_teacher = replacement
                            ? JsonInt(*replacement, "substitute_teacher", original_teacher) : original_teacher;
                        JsonValue item = JsonValue::MakeObject();
                        item.At("teacher_id") = JsonValue::MakeNumber(actual_teacher);
                        item.At("teacher_name") = JsonValue::MakeString(teacher_names[actual_teacher]);
                        item.At("original_teacher_id") = JsonValue::MakeNumber(original_teacher);
                        item.At("date") = JsonValue::MakeString(date);
                        item.At("weekday") = JsonValue::MakeNumber(weekday);
                        item.At("slot") = JsonValue::MakeNumber(slot_number);
                        item.At("lesson_id") = JsonValue::MakeNumber(lesson_id);
                        item.At("lesson_name") = JsonValue::MakeString(is_class_hour
                            ? JsonString(lesson, "name", "Классный час") : lesson_names[lesson_id]);
                        item.At("group_id") = JsonValue::MakeNumber(group_id);
                        item.At("group_name") = JsonValue::MakeString(group_names[group_id]);
                        item.At("room") = JsonValue::MakeString(JsonString(lesson, "room_name", ""));
                        item.At("is_substitution") = JsonValue::MakeBool(replacement != nullptr);
                        entries.array_value.push_back(item);
                    }
                }
            }
        }
    }
    JsonValue result = JsonValue::MakeObject();
    result.At("schedule_found") = JsonValue::MakeBool(std::filesystem::exists(schedule_file));
    result.At("entries") = entries;
    return result;
}

std::string BuildSubstitutionsCsv(const JsonValue& source_root) {
    JsonValue root = source_root;
    NormalizeDataRoot(root);
    std::unordered_map<int, std::string> teacher_names;
    std::unordered_map<int, std::string> lesson_names;
    for (const JsonValue& value : root.At("teachers").array_value)
        teacher_names[JsonInt(value, "id", -1)] = JsonString(value, "name", "");
    for (const JsonValue& value : root.At("lessons").array_value)
        lesson_names[JsonInt(value, "id", -1)] = JsonString(value, "name", "");
    auto csv = [](std::string value) {
        size_t pos = 0;
        while ((pos = value.find('"', pos)) != std::string::npos) { value.insert(pos, 1, '"'); pos += 2; }
        return "\"" + value + "\"";
    };
    std::ostringstream out;
    out << "\xEF\xBB\xBFДата;Пара;Дисциплина;Отсутствующий;Заменяющий;Часы;Причина;Комментарий;Статус\r\n";
    for (const JsonValue& value : root.At("substitutions").array_value) {
        const int lesson = JsonInt(value, "lesson_id", -1);
        const int absent = JsonInt(value, "absent_teacher", -1);
        const int substitute = JsonInt(value, "substitute_teacher", -1);
        out << csv(JsonString(value, "date", "")) << ';'
            << JsonInt(value, "slot", 0) << ';'
            << csv(lesson_names[lesson]) << ';'
            << csv(teacher_names[absent]) << ';'
            << csv(teacher_names[substitute]) << ';'
            << JsonInt(value, "hours", 2) << ';'
            << csv(JsonString(value, "reason", "")) << ';'
            << csv(JsonString(value, "comment", "")) << ';'
            << csv(JsonString(value, "status", "active")) << "\r\n";
    }
    return out.str();
}

JsonValue ListDataVersions() {
    std::lock_guard<std::recursive_mutex> lock(g_data_file_mutex);
    JsonValue result = JsonValue::MakeArray();
    std::error_code ec;
    const auto dir = VersionsDir();
    if (!std::filesystem::exists(dir, ec)) return result;
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") files.push_back(entry.path());
    }
    std::sort(files.rbegin(), files.rend());
    for (const auto& file : files) {
        JsonValue item = JsonValue::MakeObject();
        item.At("filename") = JsonValue::MakeString(file.filename().string());
        item.At("size") = JsonValue::MakeNumber(static_cast<double>(std::filesystem::file_size(file, ec)));
        std::ifstream in(file, std::ios::binary);
        std::ostringstream ss;
        ss << in.rdbuf();
        JsonParseResult parsed = ParseJson(ss.str());
        if (parsed.ok) {
            item.At("created_at") = JsonValue::MakeString(JsonString(parsed.value, "created_at", ""));
            item.At("reason") = JsonValue::MakeString(JsonString(parsed.value, "reason", ""));
        }
        result.array_value.push_back(item);
    }
    return result;
}

bool RestoreDataVersion(const std::string& filename, std::string& error) {
    std::lock_guard<std::recursive_mutex> lock(g_data_file_mutex);
    if (!IsSafeVersionFilename(filename)) {
        error = "Некорректное имя версии";
        return false;
    }
    const auto path = VersionsDir() / filename;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "Версия не найдена";
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    JsonParseResult parsed = ParseJson(ss.str());
    if (!parsed.ok || !parsed.value.At("data").IsObject()) {
        error = "Файл версии повреждён";
        return false;
    }
    return SaveDataJson(parsed.value.At("data"), error, "Откат к " + filename);
}

}  // namespace timetable
