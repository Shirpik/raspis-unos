#include "semester_plan.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <filesystem>
#include <fstream>
#include "date_utils.h"

namespace timetable {
namespace {
int Capacity(const ScheduleInputData& data, const TeacherData& teacher, Date first, Date last) {
    if (last < first) return 0;
    std::map<int, std::vector<int>> weeks;
    for (const auto& date : GenerateSchoolDays(first, last)) {
        int count = 0;
        if (IsAvailable(date, teacher.id, data.teacher_unavailable))
            for (int slot = 0; slot < 7; ++slot) if (WorkScheduleAllows(teacher.work_schedule, date, slot)) count++;
        if (teacher.max_pairs_per_day > 0) count = std::min(count, teacher.max_pairs_per_day);
        weeks[(DaysBetween(first, date) + DayOfWeek(first) - 1) / 7].push_back(count);
    }
    int total = 0;
    for (auto& week : weeks) {
        std::sort(week.second.begin(), week.second.end(), std::greater<int>());
        int remaining_days = teacher.max_work_days_per_week > 0 ? teacher.max_work_days_per_week : 7;
        for (int count : week.second) if (remaining_days-- > 0) total += count;
    }
    return total;
}
int Course(const JsonValue& root, int group) {
    for (const auto& item : root.At("groups").array_value) if (JsonInt(item, "id", -1) == group) {
        const int explicit_year = JsonInt(item, "course_year", 0);
        if (explicit_year >= 1 && explicit_year <= 4) return explicit_year;
        const auto name = JsonString(item, "name", "");
        for (size_t i = 1; i < name.size(); ++i)
            if (name[i - 1] == '-' && name[i] >= '1' && name[i] <= '4') return name[i] - '0';
    }
    return 0;
}
}

void PrepareSemesterRequirements(const JsonValue& root, ScheduleInputData& data) {
    data.load_requirements.clear();
    auto& report = data.semester_readout_report;
    report = JsonValue::MakeObject();
    auto rows = JsonValue::MakeArray();
    auto deferred = JsonValue::MakeArray();
    auto issues = JsonValue::MakeArray();
    auto issue = [&](const std::string& code, const std::string& message, int teacher) {
        auto entry = JsonValue::MakeObject();
        entry.At("code") = JsonValue::MakeString(code);
        entry.At("message") = JsonValue::MakeString(message);
        entry.At("teacher") = JsonValue::MakeNumber(teacher);
        issues.array_value.push_back(entry);
    };
    const auto& settings = root.At("settings");
    const bool semester_enabled = JsonBool(settings, "enforce_semester_readout", false);
    Date semester_first{};
    const bool configured = ParseDateIso(JsonString(settings, "semester_start_date", ""), semester_first);
    const int weeks = JsonInt(settings, "semester_weeks", 16);
    Date semester_last = semester_first;
    if (configured && weeks >= 1 && weeks <= 52)
        for (int n = 1; n < weeks * 7; ++n) semester_last = NextDay(semester_last);
    if (semester_enabled && (!configured || weeks < 1 || weeks > 52))
        issue("semester_dates_missing", "Задайте начало учебного плана и число недель отдельно от периода генерации", -1);

    // Only confirmed records count as taught. Re-generating a date never creates credits.
    std::map<int, int> confirmed;
    std::set<std::string> seen;
    for (const auto& entry : root.At("teaching_ledger").array_value) {
        if (JsonString(entry, "status", "") != "confirmed") continue;
        Date date{};
        if (!ParseDateIso(JsonString(entry, "date", ""), date) || !(date < data.start_date)) continue;
        const int lesson = JsonInt(entry, "lesson_id", -1);
        const int pair = JsonInt(entry, "slot", 0);
        const int hours = JsonInt(entry, "hours", 0);
        const auto key = std::to_string(lesson) + "|" + DateToIso(date) + "|" + std::to_string(pair);
        if (lesson < 0 || pair < 1 || pair > 7 || hours <= 0 || !seen.insert(key).second) {
            issue("ledger_record_invalid", "Некорректная или повторная запись подтверждённых часов", -1);
            continue;
        }
        confirmed[lesson] += hours;
    }
    for (const auto& teacher : data.teachers) {
        const JsonValue* source_teacher = nullptr;
        for (const auto& t : root.At("teachers").array_value) if (JsonInt(t, "id", -1) == teacher.id) source_teacher = &t;
        if (!source_teacher) continue;
        if (!teacher.scheduling_active) {
            int hours = 0;
            for (const auto& lesson : root.At("lessons").array_value)
                if (JsonInt(lesson, "teacher", -1) == teacher.id && JsonBool(lesson, "curriculum_active", true))
                    hours += std::max(0, JsonInt(lesson, "total_hours", 0) - confirmed[JsonInt(lesson, "id", -1)]);
            auto row = JsonValue::MakeObject();
            row.At("teacher") = JsonValue::MakeNumber(teacher.id);
            row.At("teacher_name") = JsonValue::MakeString(teacher.name);
            row.At("remaining_hours") = JsonValue::MakeNumber(hours);
            row.At("reason") = JsonValue::MakeString("Преподаватель временно выключен из генерации; часы сохранены");
            deferred.array_value.push_back(row);
            continue;
        }
        std::vector<JsonValue> rules = source_teacher->At("desired_load_rules").array_value;
        if (semester_enabled && configured && weeks >= 1 && weeks <= 52) {
            auto rule = JsonValue::MakeObject();
            rule.At("deadline") = JsonValue::MakeString(DateToIso(semester_last));
            rule.At("whole_curriculum") = JsonValue::MakeBool(true);
            rules.push_back(rule);
        }
        for (const auto& rule : rules) {
            const auto deadline_text = JsonString(rule, "deadline", "");
            const int weekly_minimum = JsonInt(rule, "minimum_pairs_per_week", 0);
            Date deadline = configured ? semester_last : data.end_date;
            if ((!deadline_text.empty() && !ParseDateIso(deadline_text, deadline)) || weekly_minimum < 0 || weekly_minimum > 42 ||
                (deadline_text.empty() && weekly_minimum == 0)) {
                issue("desired_load_rule_invalid", "Условия нагрузки должны содержать корректный срок или минимум пар", teacher.id);
                continue;
            }
            std::set<int> groups;
            for (const auto& id : rule.At("group_ids").array_value) if (id.IsNumber()) groups.insert(static_cast<int>(id.number_value));
            const int course = JsonInt(rule, "course_year", 0);
            int remaining = 0, practice_hours = 0, planned = 0, already_taught = 0;
            LoadRequirement requirement;
            requirement.teacher = teacher.id;
            requirement.label = teacher.name + (course ? " / " + std::to_string(course) + " курс" : "") + " / до " + DateToIso(deadline);
            for (const auto& lesson : root.At("lessons").array_value) {
                if (JsonInt(lesson, "teacher", -1) != teacher.id || !JsonBool(lesson, "curriculum_active", true)) continue;
                int group = JsonInt(lesson, "group", -1);
                if ((!groups.empty() && !groups.count(group)) || (course && Course(root, group) != course)) continue;
                const int id = JsonInt(lesson, "id", -1);
                const int hours = std::max(0, JsonInt(lesson, "total_hours", 0));
                if (confirmed[id] > hours) issue("ledger_exceeds_curriculum", "Подтверждённые часы превышают учебный план", teacher.id);
                const int left = std::max(0, hours - confirmed[id]);
                planned += hours; already_taught += confirmed[id];
                if (JsonBool(lesson, "is_block", false) || JsonBool(lesson, "is_pp", false)) {
                    practice_hours += left;
                    continue;
                }
                remaining += left;
                requirement.lesson_ids.insert(id);
            }
            const int total_capacity = Capacity(data, teacher, data.start_date, deadline);
            const int current_capacity = Capacity(data, teacher, data.start_date, std::min(data.end_date, deadline));
            const int remaining_pairs = CeilDiv(remaining, 2);
            int minimum = total_capacity ? static_cast<int>(std::ceil(double(remaining_pairs) * current_capacity / total_capacity)) : 0;
            if (deadline_text.empty()) minimum = 0;
            int week_count = static_cast<int>(std::ceil(double(DaysBetween(data.start_date, data.end_date) + 1) / 7));
            minimum = std::max(minimum, weekly_minimum * std::max(1, week_count));
            if (remaining_pairs < minimum) minimum = remaining_pairs;
            requirement.minimum_pairs = minimum;
            int selected = 0;
            for (const auto& lesson : data.lessons)
                if (requirement.lesson_ids.count(lesson.id)) selected += lesson.total_slots;
            auto row = JsonValue::MakeObject();
            row.At("teacher") = JsonValue::MakeNumber(teacher.id);
            row.At("label") = JsonValue::MakeString(requirement.label);
            row.At("planned_hours") = JsonValue::MakeNumber(planned);
            row.At("confirmed_hours") = JsonValue::MakeNumber(already_taught);
            row.At("remaining_regular_hours") = JsonValue::MakeNumber(remaining);
            row.At("practice_hours_needing_calendar") = JsonValue::MakeNumber(practice_hours);
            row.At("capacity_pairs_until_deadline") = JsonValue::MakeNumber(total_capacity);
            row.At("minimum_period_pairs") = JsonValue::MakeNumber(minimum);
            row.At("selected_period_pairs") = JsonValue::MakeNumber(selected);
            row.At("deadline") = JsonValue::MakeString(DateToIso(deadline));
            row.At("group_ids") = rule.At("group_ids");
            row.At("course_year") = JsonValue::MakeNumber(course);
            rows.array_value.push_back(row);
            if (remaining_pairs > total_capacity && !deadline_text.empty())
                issue("semester_capacity_shortfall", requirement.label + ": нужно " + std::to_string(remaining_pairs) + " пар, доступно не более " + std::to_string(total_capacity), teacher.id);
            if (practice_hours && !deadline_text.empty())
                issue("practice_calendar_required", requirement.label + ": отдельно требуется календарь УП/ПП на " + std::to_string(practice_hours) + " ч; полная вычитка пока не доказана", teacher.id);
            if (selected < minimum)
                issue("semester_quota_shortfall", requirement.label + ": квота периода " + std::to_string(selected) + " пар меньше требуемого темпа " + std::to_string(minimum), teacher.id);
            if (minimum > 0) data.load_requirements.push_back(requirement);
        }
    }
    report.At("source") = JsonValue::MakeString("database_curriculum_and_confirmed_ledger");
    report.At("capacity_is_upper_bound") = JsonValue::MakeBool(true);
    report.At("full_semester_feasibility_proven") = JsonValue::MakeBool(false);
    report.At("semester_start_date") = JsonValue::MakeString(configured ? DateToIso(semester_first) : "");
    report.At("semester_end_date") = JsonValue::MakeString(configured ? DateToIso(semester_last) : "");
    report.At("semester_weeks") = JsonValue::MakeNumber(weeks);
    report.At("rows") = rows;
    report.At("deferred_teachers") = deferred;
    report.At("issues") = issues;
    report.At("ok") = JsonValue::MakeBool(issues.array_value.empty());
}

std::string LoadRequirementError(const ScheduleInputData& data) {
    std::string error;
    for (const auto& issue : data.semester_readout_report.At("issues").array_value) {
        if (error.size() > 1500) { error += " | …"; break; }
        if (!error.empty()) error += " | ";
        error += JsonString(issue, "message", "Ошибка плана");
    }
    return error;
}

bool CheckSemesterPreflight(const ScheduleInputData& data, const std::string& output_dir, std::string& error) {
    std::filesystem::create_directories(output_dir);
    std::ofstream stream(std::filesystem::path(output_dir) / "semester_readout_report.json", std::ios::binary);
    stream << ToJson(data.semester_readout_report, 2);
    stream.close();
    if (!stream) { error = "Не удалось сохранить отчёт темпа вычитки"; return false; }
    error = LoadRequirementError(data);
    return error.empty();
}
}
