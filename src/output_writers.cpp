#include "output_writers.h"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "config.h"
#include "date_utils.h"
#include "data_store.h"
#include "format_utils.h"

namespace timetable {

using operations_research::sat::BoolVar;
using operations_research::sat::CpSolverResponse;
using operations_research::sat::IntVar;

std::string BuildGroupSlotText(
    const CpSolverResponse& response,
    const std::vector<Date>& all_days,
    const std::vector<Lesson>& lessons,
    const std::vector<std::vector<BoolVar>>& x,
    const std::vector<std::vector<IntVar>>& group_day_campus,
    int group,
    int day,
    int slot
) {
    int t = day * SLOTS_PER_DAY + slot;
    std::vector<std::string> items;

    int campus = IntValue(response, group_day_campus[group][day]);

    for (int l = 0; l < static_cast<int>(lessons.size()); l++) {
        if (lessons[l].group != group) continue;

        if (BoolValue(response, x[l][t])) {
            std::ostringstream ss;
            std::string lesson_name = lessons[l].name;

            if (lessons[l].is_block) {
                std::string up_label = UpShiftLabelForDisplaySlot(all_days[day], slot);
                if (!up_label.empty()) {
                    lesson_name += " (" + up_label + ")";
                }
            }

            std::string teacher_display = lessons[l].teacher >= 0
                ? TEACHER_NAME[lessons[l].teacher]
                : "вакансия";
            ss << lesson_name
               << " — " << SubgroupName(lessons[l].subgroup)
               << ", " << teacher_display
               << ", " << CampusName(campus);

            items.push_back(ss.str());
        }
    }

    if (items.empty()) {
        return "-";
    }

    return Join(items, " | ");
}

std::string BuildTeacherSlotText(
    const CpSolverResponse& response,
    const std::vector<Date>& all_days,
    const std::vector<Lesson>& lessons,
    const std::vector<std::vector<BoolVar>>& x,
    const std::vector<BlockInfo>& blocks,
    const std::vector<std::vector<IntVar>>& teacher_day_campus,
    int teacher,
    int day,
    int slot
) {
    int t = day * SLOTS_PER_DAY + slot;
    std::vector<std::string> items;

    int campus = IntValue(response, teacher_day_campus[teacher][day]);
    TimeInterval pair_interval = PairSlotInterval(DayOfWeek(all_days[day]), slot);

    for (int l = 0; l < static_cast<int>(lessons.size()); l++) {
        if (lessons[l].teacher != teacher) continue;
        if (lessons[l].is_block) continue;

        if (BoolValue(response, x[l][t])) {
            std::ostringstream ss;

            ss << GROUP_NAME[lessons[l].group]
               << ", " << lessons[l].name
               << " — " << SubgroupName(lessons[l].subgroup)
               << ", " << CampusName(campus);

            items.push_back(ss.str());
        }
    }

    for (const auto& blk : blocks) {
        const Lesson& lesson = lessons[blk.lesson_id];

        if (lesson.teacher != teacher) {
            continue;
        }

        for (int i = 0; i < static_cast<int>(blk.possible_starts.size()); i++) {
            if (!BoolValue(response, blk.start_vars[i])) {
                continue;
            }

            int start_t = blk.possible_starts[i];
            int start_day = start_t / SLOTS_PER_DAY;
            int start_slot = start_t % SLOTS_PER_DAY;

            if (start_day != day) {
                continue;
            }

            TimeInterval up_interval = UpIntervalForStartSlot(all_days[day], start_slot);

            if (!IntervalsOverlap(up_interval, pair_interval)) {
                continue;
            }

            std::ostringstream ss;
            ss << GROUP_NAME[lesson.group]
               << ", " << lesson.name
               << " (" << UpIntervalLabelForStartSlot(all_days[day], start_slot) << ")"
               << " — " << SubgroupName(lesson.subgroup)
               << ", " << CampusName(campus);

            items.push_back(ss.str());
        }
    }

    if (items.empty()) {
        return "-";
    }

    return Join(items, " | ");
}

bool HasGroupDay(
    const CpSolverResponse& response,
    const std::vector<std::vector<BoolVar>>& group_busy,
    int group,
    int day
) {
    for (int s = 0; s < SLOTS_PER_DAY; s++) {
        int t = day * SLOTS_PER_DAY + s;

        if (BoolValue(response, group_busy[group][t])) {
            return true;
        }
    }

    return false;
}

bool HasTeacherDay(
    const CpSolverResponse& response,
    const std::vector<std::vector<BoolVar>>& teacher_busy,
    int teacher,
    int day
) {
    for (int s = 0; s < SLOTS_PER_DAY; s++) {
        int t = day * SLOTS_PER_DAY + s;

        if (BoolValue(response, teacher_busy[teacher][t])) {
            return true;
        }
    }

    return false;
}

namespace {

std::string SpecialDayText(
    const std::map<int, std::map<Date, std::string>>& unavailable_day_texts,
    int group,
    const Date& date
) {
    auto group_it = unavailable_day_texts.find(group);
    if (group_it == unavailable_day_texts.end()) return "";
    auto day_it = group_it->second.find(date);
    if (day_it == group_it->second.end()) return "";
    return day_it->second;
}

int ClassHourCampus(int group) {
    if (group >= 0 && group < static_cast<int>(GROUP_CLASS_HOUR_CAMPUS.size()) &&
        GROUP_CLASS_HOUR_CAMPUS[group] >= 0) {
        return GROUP_CLASS_HOUR_CAMPUS[group];
    }
    if (group >= 0 && group < static_cast<int>(GROUP_HOME_CAMPUS.size())) {
        return GROUP_HOME_CAMPUS[group];
    }
    return -1;
}

bool HasClassHour(
    const std::map<int, std::map<Date, std::string>>& unavailable_day_texts,
    int group,
    const Date& date
) {
    if (DayOfWeek(date) != 1 || !SpecialDayText(unavailable_day_texts, group, date).empty()) return false;
    if (group < 0 || group >= static_cast<int>(GROUP_CURATOR_TEACHER.size())) return false;
    if (group >= static_cast<int>(GROUP_CLASS_HOUR_ENABLED.size()) || !GROUP_CLASS_HOUR_ENABLED[group]) return false;
    const int teacher = GROUP_CURATOR_TEACHER[group];
    return teacher >= 0 && teacher < TEACHERS;
}

std::string ClassHourText(int group) {
    const int teacher = GROUP_CURATOR_TEACHER[group];
    const int campus = ClassHourCampus(group);
    return "Классный час — " + TEACHER_NAME[teacher] + ", " +
        (campus >= 0 ? CampusName(campus) : std::string("любая площадка"));
}

std::vector<int> CuratorGroupsForTeacher(int teacher) {
    std::vector<int> result;
    for (int group = 0; group < GROUPS; ++group) {
        if (group < static_cast<int>(GROUP_CLASS_HOUR_ENABLED.size()) &&
            GROUP_CLASS_HOUR_ENABLED[group] &&
            group < static_cast<int>(GROUP_CURATOR_TEACHER.size()) &&
            GROUP_CURATOR_TEACHER[group] == teacher) {
            result.push_back(group);
        }
    }
    return result;
}

bool HasPrintableGroupDay(
    const CpSolverResponse& response,
    const std::vector<std::vector<BoolVar>>& group_busy,
    const std::map<int, std::map<Date, std::string>>& unavailable_day_texts,
    const std::vector<Date>& all_days,
    int group,
    int day
) {
    if (HasGroupDay(response, group_busy, group, day)) return true;
    if (HasClassHour(unavailable_day_texts, group, all_days[day])) return true;
    return !SpecialDayText(unavailable_day_texts, group, all_days[day]).empty();
}

std::string BuildPrintableGroupSlotText(
    const CpSolverResponse& response,
    const std::vector<Date>& all_days,
    const std::vector<Lesson>& lessons,
    const std::vector<std::vector<BoolVar>>& x,
    const std::vector<std::vector<IntVar>>& group_day_campus,
    const std::map<int, std::map<Date, std::string>>& unavailable_day_texts,
    int group,
    int day,
    int slot
) {
    std::string special_text = SpecialDayText(unavailable_day_texts, group, all_days[day]);
    if (!special_text.empty()) {
        return slot == 0 ? special_text : "-";
    }

    return BuildGroupSlotText(
        response,
        all_days,
        lessons,
        x,
        group_day_campus,
        group,
        day,
        slot
    );
}

}  // namespace

void WriteGroupScheduleTxt(
    const std::string& file_name,
    const CpSolverResponse& response,
    const std::vector<Date>& all_days,
    const std::vector<Lesson>& lessons,
    const std::vector<std::vector<BoolVar>>& x,
    const std::vector<std::vector<BoolVar>>& group_busy,
    const std::vector<std::vector<IntVar>>& group_day_campus,
    const std::map<int, std::map<Date, std::string>>& unavailable_day_texts,
    int group
) {
    std::ofstream out(file_name, std::ios::binary);
    if (!out) {
        std::cerr << "Не удалось открыть файл: " << file_name << "\n";
        return;
    }

    WriteUtf8Bom(out);

    out << "Расписание группы " << GROUP_NAME[group] << "\n";
    out << "========================================\n\n";

    for (int d = 0; d < static_cast<int>(all_days.size()); d++) {
        if (!HasPrintableGroupDay(response, group_busy, unavailable_day_texts, all_days, group, d)) {
            continue;
        }

        const Date& dt = all_days[d];

        out << DateToString(dt)
            << " (" << WEEKDAY_NAME[DayOfWeek(dt) - 1] << ")\n";

        if (HasClassHour(unavailable_day_texts, group, dt)) {
            out << "  0 урок (07:50-09:15): " << ClassHourText(group) << "\n";
        }

        for (int s = 0; s < SLOTS_PER_DAY; s++) {
            out << "  " << PairSlotLabel(dt, s) << ": "
                << BuildPrintableGroupSlotText(
                    response,
                    all_days,
                    lessons,
                    x,
                    group_day_campus,
                    unavailable_day_texts,
                    group,
                    d,
                    s
                )
                << "\n";
        }

        out << "\n";
    }
}

void WriteAllGroupsTxt(
    const std::string& file_name,
    const CpSolverResponse& response,
    const std::vector<Date>& all_days,
    const std::vector<Lesson>& lessons,
    const std::vector<std::vector<BoolVar>>& x,
    const std::vector<std::vector<BoolVar>>& group_busy,
    const std::vector<std::vector<IntVar>>& group_day_campus,
    const std::map<int, std::map<Date, std::string>>& unavailable_day_texts
) {
    std::ofstream out(file_name, std::ios::binary);
    if (!out) {
        std::cerr << "Не удалось открыть файл: " << file_name << "\n";
        return;
    }

    WriteUtf8Bom(out);

    out << "Общее расписание групп\n";
    out << "======================\n\n";

    for (int g = 0; g < GROUPS; g++) {
        out << "\n\n========== " << GROUP_NAME[g] << " ==========\n\n";

        for (int d = 0; d < static_cast<int>(all_days.size()); d++) {
            if (!HasPrintableGroupDay(response, group_busy, unavailable_day_texts, all_days, g, d)) {
                continue;
            }

            const Date& dt = all_days[d];

            out << DateToString(dt)
                << " (" << WEEKDAY_NAME[DayOfWeek(dt) - 1] << ")\n";

            if (HasClassHour(unavailable_day_texts, g, dt)) {
                out << "  0 урок (07:50-09:15): " << ClassHourText(g) << "\n";
            }

            for (int s = 0; s < SLOTS_PER_DAY; s++) {
                out << "  " << PairSlotLabel(dt, s) << ": "
                    << BuildPrintableGroupSlotText(
                        response,
                        all_days,
                        lessons,
                        x,
                        group_day_campus,
                        unavailable_day_texts,
                        g,
                        d,
                        s
                    )
                    << "\n";
            }

            out << "\n";
        }
    }
}

void WriteGroupsCsv(
    const std::string& file_name,
    const CpSolverResponse& response,
    const std::vector<Date>& all_days,
    const std::vector<Lesson>& lessons,
    const std::vector<std::vector<BoolVar>>& x,
    const std::vector<std::vector<BoolVar>>& group_busy,
    const std::vector<std::vector<IntVar>>& group_day_campus,
    const std::map<int, std::map<Date, std::string>>& unavailable_day_texts
) {
    std::ofstream out(file_name, std::ios::binary);
    if (!out) {
        std::cerr << "Не удалось открыть файл: " << file_name << "\n";
        return;
    }

    WriteUtf8Bom(out);

    out << CsvEscape("Группа") << ";"
        << CsvEscape("Дата") << ";"
        << CsvEscape("День") << ";"
        << CsvEscape("Пара") << ";"
        << CsvEscape("Занятия") << "\n";

    for (int g = 0; g < GROUPS; g++) {
        for (int d = 0; d < static_cast<int>(all_days.size()); d++) {
            if (!HasPrintableGroupDay(response, group_busy, unavailable_day_texts, all_days, g, d)) {
                continue;
            }

            const Date& dt = all_days[d];

            if (HasClassHour(unavailable_day_texts, g, dt)) {
                out << CsvEscape(GROUP_NAME[g]) << ";"
                    << CsvEscape(DateToString(dt)) << ";"
                    << CsvEscape(WEEKDAY_NAME[DayOfWeek(dt) - 1]) << ";"
                    << CsvEscape("0 урок (07:50-09:15)") << ";"
                    << CsvEscape(ClassHourText(g)) << "\n";
            }

            for (int s = 0; s < SLOTS_PER_DAY; s++) {
                std::string text = BuildPrintableGroupSlotText(
                    response,
                    all_days,
                    lessons,
                    x,
                    group_day_campus,
                    unavailable_day_texts,
                    g,
                    d,
                    s
                );

                out << CsvEscape(GROUP_NAME[g]) << ";"
                    << CsvEscape(DateToString(dt)) << ";"
                    << CsvEscape(WEEKDAY_NAME[DayOfWeek(dt) - 1]) << ";"
                    << CsvEscape(PairSlotLabel(dt, s)) << ";"
                    << CsvEscape(text) << "\n";
            }
        }
    }
}

void WriteTeachersTxt(
    const std::string& file_name,
    const CpSolverResponse& response,
    const std::vector<Date>& all_days,
    const std::vector<Lesson>& lessons,
    const std::vector<std::vector<BoolVar>>& x,
    const std::vector<BlockInfo>& blocks,
    const std::vector<std::vector<BoolVar>>& teacher_busy,
    const std::vector<std::vector<IntVar>>& teacher_day_campus
) {
    std::ofstream out(file_name, std::ios::binary);
    if (!out) {
        std::cerr << "Не удалось открыть файл: " << file_name << "\n";
        return;
    }

    WriteUtf8Bom(out);

    out << "Расписание преподавателей\n";
    out << "=========================\n\n";

    for (int teacher = 0; teacher < TEACHERS; teacher++) {
        out << "\n\n========== " << TEACHER_NAME[teacher] << " ==========\n\n";

        const std::vector<int> curator_groups = CuratorGroupsForTeacher(teacher);

        for (int d = 0; d < static_cast<int>(all_days.size()); d++) {
            const bool has_class_hour = DayOfWeek(all_days[d]) == 1 && !curator_groups.empty();
            if (!HasTeacherDay(response, teacher_busy, teacher, d) && !has_class_hour) {
                continue;
            }

            const Date& dt = all_days[d];

            out << DateToString(dt)
                << " (" << WEEKDAY_NAME[DayOfWeek(dt) - 1] << ")\n";

            if (has_class_hour) {
                std::vector<std::string> items;
                for (int group : curator_groups) {
                    items.push_back(GROUP_NAME[group] + ", классный час, " +
                        (ClassHourCampus(group) >= 0 ? CampusName(ClassHourCampus(group)) : std::string("любая площадка")));
                }
                out << "  0 урок (07:50-09:15): " << Join(items, " | ") << "\n";
            }

            for (int s = 0; s < SLOTS_PER_DAY; s++) {
                out << "  " << PairSlotLabel(dt, s) << ": "
                    << BuildTeacherSlotText(
                        response,
                        all_days,
                        lessons,
                        x,
                        blocks,
                        teacher_day_campus,
                        teacher,
                        d,
                        s
                    )
                    << "\n";
            }

            out << "\n";
        }
    }
}


namespace {

std::string JsonEscape(const std::string& s) {
    std::ostringstream out;
    for (unsigned char ch : s) {
        switch (ch) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\b': out << "\\b"; break;
        case '\f': out << "\\f"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (ch < 0x20) {
                out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(ch);
            } else {
                out << static_cast<char>(ch);
            }
        }
    }
    return out.str();
}

void WriteSlotLessonsJson(
    std::ofstream& out,
    const CpSolverResponse& response,
    const std::vector<Lesson>& lessons,
    const std::vector<std::vector<BoolVar>>& x,
    int group,
    int day,
    int slot,
    const RoomAssignmentMap* room_assignments
) {
    int t = day * SLOTS_PER_DAY + slot;
    out << "[";
    bool first = true;
    for (int l = 0; l < static_cast<int>(lessons.size()); l++) {
        if (lessons[l].group != group) continue;
        if (!BoolValue(response, x[l][t])) continue;

        if (!first) out << ",";
        first = false;

        const AssignedRoom* assigned_room = nullptr;
        if (room_assignments) {
            auto room_it = room_assignments->find(RoomAssignmentKey(l, t));
            if (room_it != room_assignments->end()) assigned_room = &room_it->second;
        }
        const int room_id = assigned_room ? assigned_room->id
            : (room_assignments ? -1 : lessons[l].fixed_room);
        const std::string room_name = assigned_room ? assigned_room->name
            : (room_assignments ? "" : lessons[l].fixed_room_name);

        out << "{"
            << "\"id\":" << lessons[l].id
            << ",\"uid\":\"" << JsonEscape(lessons[l].uid) << "\""
            << ",\"name\":\"" << JsonEscape(lessons[l].name) << "\""
            << ",\"teacher_id\":" << (lessons[l].teacher >= 0 ? std::to_string(lessons[l].teacher) : "null")
            << ",\"subgroup\":" << lessons[l].subgroup
            << ",\"is_lab\":" << (lessons[l].is_lab ? "true" : "false")
            << ",\"is_block\":" << (lessons[l].is_block ? "true" : "false")
            << ",\"consecutive_pairs\":" << lessons[l].consecutive_pairs
            << ",\"avoid_lunch_split\":" << (lessons[l].avoid_lunch_split ? "true" : "false")
            << ",\"week_parity\":\"" << JsonEscape(lessons[l].week_parity) << "\""
            << ",\"room_id\":" << (room_id >= 0 ? std::to_string(room_id) : "null")
            << ",\"room_name\":" << (room_name.empty()
                ? "null" : "\"" + JsonEscape(room_name) + "\"")
            << ",\"room_type\":" << (assigned_room ? std::to_string(assigned_room->room_type) : "null")
            << ",\"room_substituted\":" << (assigned_room && assigned_room->substituted ? "true" : "false")
            << ",\"requested_room_id\":" << (assigned_room && assigned_room->requested_room_id >= 0
                ? std::to_string(assigned_room->requested_room_id) : "null")
            << ",\"requested_room_name\":" << (assigned_room && !assigned_room->requested_room_name.empty()
                ? "\"" + JsonEscape(assigned_room->requested_room_name) + "\"" : "null")
            << ",\"room_substitution_reason\":" << (assigned_room && !assigned_room->substitution_reason.empty()
                ? "\"" + JsonEscape(assigned_room->substitution_reason) + "\"" : "null")
            << "}";
    }
    out << "]";
}

void WriteGroupJsonBody(
    std::ofstream& out,
    const CpSolverResponse& response,
    const std::vector<Date>& all_days,
    const std::vector<Lesson>& lessons,
    const std::vector<std::vector<BoolVar>>& x,
    const std::vector<std::vector<BoolVar>>& group_busy,
    const std::vector<std::vector<IntVar>>& group_day_campus,
    const std::map<int, std::map<Date, std::string>>& unavailable_day_texts,
    int group,
    const RoomAssignmentMap* room_assignments
) {
    out << "{\n";
    out << "  \"group_index\": " << group << ",\n";
    out << "  \"group_name\": \"" << JsonEscape(GROUP_NAME[group]) << "\",\n";
    out << "  \"days\": [\n";

    bool first_day = true;
    for (int d = 0; d < static_cast<int>(all_days.size()); d++) {
        if (!HasPrintableGroupDay(response, group_busy, unavailable_day_texts, all_days, group, d)) {
            continue;
        }

        const Date& dt = all_days[d];
        if (!first_day) {
            out << ",\n";
        }
        first_day = false;

        out << "    {\n";
        out << "      \"date\": \"" << JsonEscape(DateToString(dt)) << "\",\n";
        out << "      \"date_iso\": \"" << JsonEscape(DateToIso(dt)) << "\",\n";
        out << "      \"day_index\": " << d << ",\n";
        out << "      \"weekday\": \"" << JsonEscape(WEEKDAY_NAME[DayOfWeek(dt) - 1]) << "\",\n";
        out << "      \"slots\": [\n";

        const bool has_class_hour = HasClassHour(unavailable_day_texts, group, dt);
        if (has_class_hour) {
            const int curator = GROUP_CURATOR_TEACHER[group];
            const int campus = ClassHourCampus(group);
            out << "        {\"slot\": 0, \"time\": \"0 урок (07:50-09:15)\""
                << ", \"text\": \"" << JsonEscape(ClassHourText(group)) << "\""
                << ", \"lessons\": [{\"id\":-1,\"uid\":\"class-hour-" << group
                << "\",\"name\":\"Классный час\",\"teacher_id\":" << curator
                << ",\"subgroup\":-1,\"is_lab\":false,\"is_block\":false"
                << ",\"is_class_hour\":true,\"week_parity\":\"all\""
                << ",\"room_id\":null,\"room_name\":null,\"room_type\":null"
                << ",\"room_substituted\":false,\"requested_room_id\":null"
                << ",\"requested_room_name\":null,\"room_substitution_reason\":null"
                << ",\"campus\":" << (campus >= 0 ? std::to_string(campus) : "null") << "}]}"
                << ",\n";
        }

        for (int s = 0; s < SLOTS_PER_DAY; s++) {
            std::string text = BuildPrintableGroupSlotText(
                response,
                all_days,
                lessons,
                x,
                group_day_campus,
                unavailable_day_texts,
                group,
                d,
                s
            );

            out << "        {\"slot\": " << (s + 1)
                << ", \"time\": \"" << JsonEscape(PairSlotLabel(dt, s))
                << "\", \"text\": \"" << JsonEscape(text) << "\""
                << ", \"lessons\": ";
            WriteSlotLessonsJson(out, response, lessons, x, group, d, s, room_assignments);
            out << "}";

            if (s + 1 < SLOTS_PER_DAY) {
                out << ",";
            }
            out << "\n";
        }

        out << "      ]\n";
        out << "    }";
    }

    out << "\n  ]\n";
    out << "}";
}

}  // namespace

void WriteGroupJson(
    const std::string& file_name,
    const CpSolverResponse& response,
    const std::vector<Date>& all_days,
    const std::vector<Lesson>& lessons,
    const std::vector<std::vector<BoolVar>>& x,
    const std::vector<std::vector<BoolVar>>& group_busy,
    const std::vector<std::vector<IntVar>>& group_day_campus,
    const std::map<int, std::map<Date, std::string>>& unavailable_day_texts,
    int group,
    const RoomAssignmentMap* room_assignments
) {
    std::ofstream out(file_name, std::ios::binary);
    if (!out) {
        std::cerr << "Не удалось открыть файл: " << file_name << "\n";
        return;
    }

    WriteGroupJsonBody(out, response, all_days, lessons, x, group_busy, group_day_campus,
        unavailable_day_texts, group, room_assignments);
}

void WriteAllGroupsJson(
    const std::string& file_name,
    const CpSolverResponse& response,
    const std::vector<Date>& all_days,
    const std::vector<Lesson>& lessons,
    const std::vector<std::vector<BoolVar>>& x,
    const std::vector<std::vector<BoolVar>>& group_busy,
    const std::vector<std::vector<IntVar>>& group_day_campus,
    const std::map<int, std::map<Date, std::string>>& unavailable_day_texts,
    const RoomAssignmentMap* room_assignments
) {
    std::ofstream out(file_name, std::ios::binary);
    if (!out) {
        std::cerr << "Не удалось открыть файл: " << file_name << "\n";
        return;
    }

    out << "{\n";
    out << "  \"groups\": [\n";
    for (int g = 0; g < GROUPS; g++) {
        out << "    ";
        WriteGroupJsonBody(out, response, all_days, lessons, x, group_busy, group_day_campus,
            unavailable_day_texts, g, room_assignments);
        if (g + 1 < GROUPS) {
            out << ",";
        }
        out << "\n";
    }
    out << "  ]\n";
    out << "}\n";
}

}  // namespace timetable
