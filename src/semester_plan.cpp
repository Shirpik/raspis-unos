#include "semester_plan.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <filesystem>
#include <fstream>
#include <ctime>
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

TeachingBalances ReadTeachingBalances(const JsonValue& root, Date before) {
    TeachingBalances result;
    std::map<std::string, std::pair<int, const JsonValue*>> records;
    for (const auto& entry : root.At("teaching_ledger").array_value) {
        const auto status = JsonString(entry, "status", "");
        Date day{};
        const int id = JsonInt(entry, "lesson_id", -1), slot = JsonInt(entry, "slot", 0);
        if ((status != "confirmed" && status != "planned") ||
            !ParseDateIso(JsonString(entry, "date", ""), day) || !(day < before) ||
            id < 0 || slot < 1 || slot > 7 || JsonInt(entry, "hours", 0) <= 0 ||
            JsonBool(entry, "is_class_hour", false)) continue;
        const auto key = std::to_string(id) + "|" + DateToIso(day) + "|" + std::to_string(slot);
        const int priority = status == "confirmed" ? 2 : 1;
        if (!records.count(key) || priority > records[key].first) records[key] = {priority, &entry};
    }
    for (const auto& [key, record] : records) {
        const auto& entry = *record.second;
        auto& totals = record.first == 2 ? result.confirmed : result.reserved;
        totals[JsonInt(entry, "lesson_id", -1)] += JsonInt(entry, "hours", 0);
    }
    return result;
}

Date GroupTeachingDeadline(const GroupData& group, Date semester_first, Date semester_last) {
    Date result = semester_last, manual{};
    if (ParseDateIso(group.semester_end_date, manual)) result = manual;
    if (ParseDateIso(group.teaching_deadline, manual)) result = std::min(result, manual);
    for (const auto& period : group.practice_periods) {
        if (period.second < semester_first || result < period.first) continue;
        Date before = period.first;
        if (--before.day == 0) {
            if (--before.month == 0) { before.month = 12; --before.year; }
            before.day = DaysInMonth(before.month, before.year);
        }
        result = std::min(result, before);
    }
    // Imported PP weeks also protect the deadline if a manual period is
    // accidentally removed. A weekly calendar cannot specify a later day.
    for (const auto& week : group.academic_calendar) {
        if (week.pp_hours <= 0 || week.to < semester_first || result < week.from) continue;
        Date before = week.from;
        if (--before.day == 0) {
            if (--before.month == 0) { before.month = 12; --before.year; }
            before.day = DaysInMonth(before.month, before.year);
        }
        result = std::min(result, before);
    }
    // A trailing UP/exam-only interval is unavailable for ordinary subjects.
    // Mixed theory/UP weeks remain available. Never infer beyond calendar coverage.
    if (!group.academic_calendar.empty()) {
        Date last_theory = semester_first;
        bool found = false;
        for (const auto& week : group.academic_calendar) {
            if (week.theory_hours <= 0 || week.vacation || week.pp_hours > 0 ||
                week.to < semester_first || result < week.from) continue;
            const auto last = std::min(result, week.to);
            if (!found || last_theory < last) last_theory = last;
            found = true;
        }
        if (found) result = last_theory;
    }
    return result;
}

bool GroupRegularCalendarAllows(const GroupData& group, const Date& date) {
    if (group.academic_calendar.empty()) return true;
    for (const auto& week : group.academic_calendar)
        if (week.from <= date && date <= week.to)
            return week.theory_hours > 0 && !week.vacation && week.pp_hours == 0;
    return false; // A loaded calendar must cover the date; never guess beyond it.
}

int GroupTeachingCapacity(const ScheduleInputData& data, const GroupData& group,
                          Date first, Date last, const TeacherData* teacher, int student_streams) {
    if (last < first) return 0;
    std::map<int, std::vector<int>> weeks;
    for (const auto& day : GenerateSchoolDays(first, last)) {
        int count = 0;
        if (GroupRegularCalendarAllows(group, day) && IsAvailable(day, group.id, data.unavailable) &&
            (!teacher || (teacher->scheduling_active && IsAvailable(day, teacher->id, data.teacher_unavailable))))
            for (int slot = 0; slot < 7; ++slot)
                if (WorkScheduleAllows(group.work_schedule, day, slot) &&
                    (!teacher || WorkScheduleAllows(teacher->work_schedule, day, slot))) ++count;
        count = std::min(count, (data.student_daily_limit > 0 ? data.student_daily_limit : MAX_STUDENT_PAIRS_PER_DAY) * student_streams);
        if (teacher && teacher->max_pairs_per_day > 0) count = std::min(count, teacher->max_pairs_per_day);
        weeks[(DaysBetween(first, day) + DayOfWeek(first) - 1) / 7].push_back(count);
    }
    int total = 0;
    for (auto& week : weeks) {
        std::sort(week.second.begin(), week.second.end(), std::greater<int>());
        int days = teacher && teacher->max_work_days_per_week > 0 ? teacher->max_work_days_per_week : 7;
        for (int count : week.second) if (days-- > 0) total += count;
    }
    return total;
}

void PrepareSemesterRequirements(const JsonValue& root, ScheduleInputData& data) {
    data.load_requirements.clear();
    auto& report = data.semester_readout_report;
    report = JsonValue::MakeObject();
    report.At("rules_version") = JsonValue::MakeNumber(3);
    auto rows = JsonValue::MakeArray();
    auto deferred = JsonValue::MakeArray();
    auto issues = JsonValue::MakeArray();
    auto issue = [&](const std::string& code, const std::string& message, int teacher,
                     const std::string& severity = "error", int group = -1, int lesson = -1) {
        auto entry = JsonValue::MakeObject();
        entry.At("code") = JsonValue::MakeString(code);
        entry.At("message") = JsonValue::MakeString(message);
        entry.At("teacher") = JsonValue::MakeNumber(teacher);
        entry.At("severity") = JsonValue::MakeString(severity);
        entry.At("group") = JsonValue::MakeNumber(group);
        entry.At("lesson") = JsonValue::MakeNumber(lesson);
        issues.array_value.push_back(entry);
    };
    const auto& settings = root.At("settings");
    const bool semester_enabled = JsonBool(settings, "enforce_semester_readout", false);
    Date semester_first{};
    const bool configured = ParseDateIso(JsonString(settings, "semester_start_date", ""), semester_first);
    const int weeks = JsonInt(settings, "semester_weeks", 16);
    Date semester_last = semester_first;
    const std::string explicit_end = JsonString(settings, "semester_end_date", "");
    const bool explicit_end_valid = !explicit_end.empty() && ParseDateIso(explicit_end, semester_last) && configured && semester_first <= semester_last;
    if (!explicit_end_valid && configured && weeks >= 1 && weeks <= 52)
        for (int n = 1; n < weeks * 7; ++n) semester_last = NextDay(semester_last);
    Date first_course_end{};
    if (ParseDateIso(JsonString(settings, "first_course_semester_end_date", ""), first_course_end))
        semester_last = std::max(semester_last, first_course_end);
    if (semester_enabled && (!configured || (!explicit_end_valid && (weeks < 1 || weeks > 52))))
        issue("semester_dates_missing", "Задайте начало и корректный конец учебного плана отдельно от периода генерации", -1);

    // Only confirmed records count as taught. Re-generating a date never creates credits.
    std::map<int, int> confirmed;
    std::set<std::string> seen;
    const auto now_time = std::time(nullptr);
    std::tm local_now{};
#ifdef _WIN32
    localtime_s(&local_now, &now_time);
#else
    localtime_r(&now_time, &local_now);
#endif
    const Date today{local_now.tm_year + 1900, local_now.tm_mon + 1, local_now.tm_mday};
    int future_confirmed_hours = 0;
    std::set<std::string> future_keys;
    for (const auto& entry : root.At("teaching_ledger").array_value) {
        if (JsonString(entry, "status", "") != "confirmed") continue;
        Date date{};
        if (!ParseDateIso(JsonString(entry, "date", ""), date)) continue;
        const int lesson = JsonInt(entry, "lesson_id", -1);
        const int pair = JsonInt(entry, "slot", 0);
        const int hours = JsonInt(entry, "hours", 0);
        const auto key = std::to_string(lesson) + "|" + DateToIso(date) + "|" + std::to_string(pair);
        if (today < date && lesson >= 0 && pair >= 1 && pair <= 7 && hours > 0 && future_keys.insert(key).second)
            future_confirmed_hours += hours;
        if (!(date < data.start_date)) continue;
        if (lesson < 0 || pair < 1 || pair > 7 || hours <= 0 || !seen.insert(key).second) {
            issue("ledger_record_invalid", "Некорректная или повторная запись подтверждённых часов", -1);
            continue;
        }
        confirmed[lesson] += hours;
    }
    report.At("today") = JsonValue::MakeString(DateToIso(today));
    const auto balances = ReadTeachingBalances(root, data.start_date);
    confirmed = balances.confirmed;
    auto reserved = balances.reserved;
    report.At("future_confirmed_hours") = JsonValue::MakeNumber(future_confirmed_hours);
    if (future_confirmed_hours)
        issue("future_confirmed_ledger", "В журнале отмечены проведёнными " + std::to_string(future_confirmed_hours) +
            " ч на даты после сегодняшней (" + DateToIso(today) + "). Проверьте факт: прогноз на будущую дату может занижать остаток", -1, "warning");
    // A quota can only grow where this particular teacher and group can meet.
    const auto period_capacity_for = [&](const Lesson& lesson) {
        const auto g = std::find_if(data.groups.begin(), data.groups.end(), [&](const auto& x) { return x.id == lesson.group; });
        const auto t = std::find_if(data.teachers.begin(), data.teachers.end(), [&](const auto& x) { return x.id == lesson.teacher; });
        if (g == data.groups.end() || t == data.teachers.end()) return 0;
        Date last = configured ? std::min(data.end_date, GroupTeachingDeadline(*g, semester_first, semester_last)) : data.end_date;
        return GroupTeachingCapacity(data, *g, data.start_date, last, &*t);
    };
    const auto shared_teacher_capacity = [&](const TeacherData& teacher, const std::set<int>& lesson_ids, Date first, Date last) {
        // Union of eligible slots, not a sum across groups: one teacher cannot
        // teach two groups at once. This is still an upper bound (no room solve).
        auto limited = teacher;
        std::set<int> ids;
        for (const auto& source : root.At("lessons").array_value)
            if (lesson_ids.count(JsonInt(source, "id", -1))) ids.insert(JsonInt(source, "group", -1));
        std::vector<const GroupData*> eligible;
        for (const auto& group : data.groups) if (ids.count(group.id)) eligible.push_back(&group);
        for (const auto& day : GenerateSchoolDays(first, last)) {
            std::set<int> slots;
            for (int slot = 0; slot < 7; ++slot) {
                if (!WorkScheduleAllows(teacher.work_schedule, day, slot)) continue;
                for (const auto* g : eligible) {
                    if ((!configured || day <= GroupTeachingDeadline(*g, semester_first, semester_last)) &&
                        GroupRegularCalendarAllows(*g, day) && IsAvailable(day, g->id, data.unavailable) &&
                        WorkScheduleAllows(g->work_schedule, day, slot)) { slots.insert(slot + 1); break; }
                }
            }
            limited.work_schedule.date_slot_overrides[day] = slots;
        }
        return Capacity(data, limited, first, last);
    };
    for (const auto& teacher : data.teachers) {
        const JsonValue* source_teacher = nullptr;
        for (const auto& t : root.At("teachers").array_value) if (JsonInt(t, "id", -1) == teacher.id) source_teacher = &t;
        if (!source_teacher) continue;
        if (!teacher.scheduling_active) {
            int hours = 0;
            for (const auto& lesson : root.At("lessons").array_value)
                if (JsonInt(lesson, "teacher", -1) == teacher.id && JsonBool(lesson, "curriculum_active", true))
                    hours += std::max(0, JsonInt(lesson, "total_hours", 0) - confirmed[JsonInt(lesson, "id", -1)] - reserved[JsonInt(lesson, "id", -1)]);
            auto row = JsonValue::MakeObject();
            row.At("teacher") = JsonValue::MakeNumber(teacher.id);
            row.At("teacher_name") = JsonValue::MakeString(teacher.name);
            row.At("remaining_hours") = JsonValue::MakeNumber(hours);
            row.At("reason") = JsonValue::MakeString("Преподаватель временно выключен из генерации; часы сохранены");
            deferred.array_value.push_back(row);
            continue;
        }
        std::vector<JsonValue> rules = source_teacher->At("desired_load_rules").array_value;
        // At each early group deadline, all of this teacher's earlier due work
        // competes for the same capacity. Do not assess each group in isolation.
        std::set<Date> group_deadlines;
        if (configured)
            for (const auto& group : data.groups) {
                const auto deadline = GroupTeachingDeadline(group, semester_first, semester_last);
                if (deadline < semester_last) group_deadlines.insert(deadline);
            }
        for (const auto& deadline : group_deadlines) {
            auto rule = JsonValue::MakeObject();
            rule.At("deadline") = JsonValue::MakeString(DateToIso(deadline));
            auto ids = JsonValue::MakeArray();
            for (const auto& group : data.groups)
                if (GroupTeachingDeadline(group, semester_first, semester_last) <= deadline)
                    ids.array_value.push_back(JsonValue::MakeNumber(group.id));
            rule.At("group_ids") = ids;
            rule.At("practice_deadline") = JsonValue::MakeBool(true);
            rules.push_back(rule);
        }
        if (semester_enabled && configured && (explicit_end_valid || (weeks >= 1 && weeks <= 52))) {
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
            int remaining = 0, practice_hours = 0, planned = 0, already_taught = 0, already_reserved = 0;
            int remaining_course_2_4 = 0, planned_course_2_4 = 0, confirmed_course_2_4 = 0;
            std::map<int, int> remaining_pairs_by_lesson;
            LoadRequirement requirement;
            requirement.teacher = teacher.id;
            requirement.label = teacher.name + (course ? " / " + std::to_string(course) + " курс" : "") + " / до " + DateToIso(deadline);
            for (const auto& lesson : root.At("lessons").array_value) {
                if (JsonInt(lesson, "teacher", -1) != teacher.id || !JsonBool(lesson, "curriculum_active", true)) continue;
                int group = JsonInt(lesson, "group", -1);
                const int group_course = Course(root, group);
                if ((!groups.empty() && !groups.count(group)) || (course && group_course != course)) continue;
                const int id = JsonInt(lesson, "id", -1);
                const int hours = std::max(0, JsonInt(lesson, "total_hours", 0));
                if (confirmed[id] > hours) issue("ledger_exceeds_curriculum", "Подтверждённые часы превышают учебный план", teacher.id);
                const int left = std::max(0, hours - confirmed[id] - reserved[id]);
                planned += hours; already_taught += confirmed[id]; already_reserved += reserved[id];
                if (group_course >= 2 && group_course <= 4) {
                    planned_course_2_4 += hours;
                    confirmed_course_2_4 += confirmed[id];
                }
                if (JsonBool(lesson, "is_block", false) || JsonBool(lesson, "is_pp", false)) {
                    practice_hours += left;
                    continue;
                }
                remaining += left;
                if (group_course >= 2 && group_course <= 4) remaining_course_2_4 += left;
                if (left > 0) requirement.lesson_ids.insert(id);
                remaining_pairs_by_lesson[id] = left / 2;
            }
            if (JsonBool(rule, "practice_deadline", false) && remaining == 0) continue;
            const int total_capacity = shared_teacher_capacity(teacher, requirement.lesson_ids, data.start_date, deadline);
            const int current_capacity = shared_teacher_capacity(teacher, requirement.lesson_ids, data.start_date, std::min(data.end_date, deadline));
            const int future_capacity = shared_teacher_capacity(teacher, requirement.lesson_ids, NextDay(data.end_date), deadline);
            const int remaining_pairs = CeilDiv(remaining, 2);
            int minimum = total_capacity ? static_cast<int>(std::ceil(double(remaining_pairs) * current_capacity / total_capacity)) : 0;
            minimum = std::max(minimum, remaining_pairs - future_capacity);
            if (deadline_text.empty()) minimum = 0;
            int week_count = static_cast<int>(std::ceil(double(DaysBetween(data.start_date, data.end_date) + 1) / 7));
            minimum = std::max(minimum, weekly_minimum * std::max(1, week_count));
            if (remaining_pairs < minimum) minimum = remaining_pairs;
            requirement.minimum_pairs = minimum;
            int selected = 0;
            for (const auto& lesson : data.lessons)
                if (requirement.lesson_ids.count(lesson.id)) selected += lesson.total_slots;
            int automatic_quota_added = 0;
            if (JsonBool(settings, "automatic_period_quotas", true) && selected < minimum) {
                bool changed = true;
                while (selected < minimum && changed) {
                    changed = false;
                    for (auto& lesson : data.lessons) {
                        if (!requirement.lesson_ids.count(lesson.id)) continue;
                        const int step = lesson.consecutive_pairs == 2 ? 2 : 1;
                        const int available = std::min(remaining_pairs_by_lesson[lesson.id], period_capacity_for(lesson)) - lesson.total_slots;
                        if (available < step) continue;
                        lesson.total_slots += step;
                        selected += step;
                        automatic_quota_added += step;
                        changed = true;
                        if (selected >= minimum) break;
                    }
                }
            }
            auto row = JsonValue::MakeObject();
            row.At("teacher") = JsonValue::MakeNumber(teacher.id);
            row.At("label") = JsonValue::MakeString(requirement.label);
            row.At("planned_hours") = JsonValue::MakeNumber(planned);
            row.At("confirmed_hours") = JsonValue::MakeNumber(already_taught);
            row.At("reserved_hours") = JsonValue::MakeNumber(already_reserved);
            const double remaining_weeks = std::max(0, DaysBetween(data.start_date, deadline) + 1) / 7.0;
            row.At("remaining_weeks") = JsonValue::MakeNumber(remaining_weeks);
            row.At("required_hours_per_week") = remaining_weeks > 0 ? JsonValue::MakeNumber(remaining / remaining_weeks) : JsonValue::MakeNull();
            row.At("remaining_regular_hours") = JsonValue::MakeNumber(remaining);
            row.At("planned_hours_course_2_4") = JsonValue::MakeNumber(planned_course_2_4);
            row.At("confirmed_hours_course_2_4") = JsonValue::MakeNumber(confirmed_course_2_4);
            row.At("remaining_regular_hours_course_2_4") = JsonValue::MakeNumber(remaining_course_2_4);
            row.At("course_2_4_capacity_ok") = JsonValue::MakeBool(CeilDiv(remaining_course_2_4, 2) <= total_capacity);
            row.At("practice_hours_needing_calendar") = JsonValue::MakeNumber(practice_hours);
            row.At("capacity_pairs_until_deadline") = JsonValue::MakeNumber(total_capacity);
            row.At("minimum_period_pairs") = JsonValue::MakeNumber(minimum);
            row.At("selected_period_pairs") = JsonValue::MakeNumber(selected);
            row.At("automatic_quota_added_pairs") = JsonValue::MakeNumber(automatic_quota_added);
            row.At("deadline") = JsonValue::MakeString(DateToIso(deadline));
            row.At("practice_deadline") = JsonValue::MakeBool(JsonBool(rule, "practice_deadline", false));
            row.At("group_ids") = rule.At("group_ids");
            row.At("course_year") = JsonValue::MakeNumber(course);
            rows.array_value.push_back(row);
            if (remaining_pairs > total_capacity && !deadline_text.empty())
                issue("semester_capacity_shortfall", requirement.label + ": нужно " + std::to_string(remaining_pairs) + " пар, доступно не более " + std::to_string(total_capacity), teacher.id);
            if (minimum > 0) data.load_requirements.push_back(requirement);
        }
    }
    auto group_rows = JsonValue::MakeArray();
    if (configured) for (const auto& group : data.groups) {
        const auto deadline = GroupTeachingDeadline(group, semester_first, semester_last);
        const int capacity = GroupTeachingCapacity(data, group, data.start_date, deadline);
        const int current_capacity = GroupTeachingCapacity(data, group, data.start_date, std::min(data.end_date, deadline));
        const int future_capacity = GroupTeachingCapacity(data, group, NextDay(data.end_date), deadline);
        auto group_row = JsonValue::MakeObject();
        group_row.At("group_id") = JsonValue::MakeNumber(group.id);
        group_row.At("group_name") = JsonValue::MakeString(group.name);
        group_row.At("deadline") = JsonValue::MakeString(DateToIso(deadline));
        group_row.At("early_deadline") = JsonValue::MakeBool(deadline < semester_last);
        group_row.At("capacity_hours_per_subgroup") = JsonValue::MakeNumber(capacity * 2);
        auto parts = JsonValue::MakeArray();
        bool risk = false;
        for (int part = 0; part < std::clamp(group.parts, 1, 2); ++part) {
            int planned = 0, taught = 0, left = 0, up = 0;
            LoadRequirement requirement;
            requirement.label = group.name + " / " + std::to_string(part + 1) + " п/г / до " + DateToIso(deadline);
            std::map<int, int> remaining_pairs;
            std::map<int, int> teacher_pairs;
            for (const auto& lesson : root.At("lessons").array_value) {
                if (JsonInt(lesson, "group", -1) != group.id || !JsonBool(lesson, "curriculum_active", true)) continue;
                const int subgroup = JsonInt(lesson, "subgroup", -1);
                if (subgroup >= 0 && subgroup % 2 != part) continue;
                const int id = JsonInt(lesson, "id", -1), total = std::max(0, JsonInt(lesson, "total_hours", 0));
                const int rest = std::max(0, total - confirmed[id] - reserved[id]);
                if (JsonBool(lesson, "is_pp", false)) continue;
                if (JsonBool(lesson, "is_block", false)) { up += rest; continue; }
                planned += total; taught += confirmed[id]; left += rest;
                if (rest > 0) {
                    requirement.lesson_ids.insert(id);
                    remaining_pairs[id] = rest / 2;
                    teacher_pairs[JsonInt(lesson, "teacher", -1)] += CeilDiv(rest, 2);
                }
            }
            const int needed_pairs = CeilDiv(left, 2);
            const int minimum = std::max(std::max(0, needed_pairs - future_capacity),
                capacity > 0 ? CeilDiv(needed_pairs * current_capacity, capacity) : 0);
            requirement.minimum_pairs = minimum;
            int selected = 0;
            for (const auto& lesson : data.lessons)
                if (requirement.lesson_ids.count(lesson.id)) selected += lesson.total_slots;
            if (deadline < semester_last && JsonBool(settings, "automatic_period_quotas", true)) {
                bool changed = true;
                while (selected < minimum && changed) {
                    changed = false;
                    for (auto& lesson : data.lessons) {
                        const int step = lesson.consecutive_pairs == 2 ? 2 : 1;
                        if (!requirement.lesson_ids.count(lesson.id) ||
                            std::min(remaining_pairs[lesson.id], period_capacity_for(lesson)) - lesson.total_slots < step) continue;
                        lesson.total_slots += step; selected += step; changed = true;
                        if (selected >= minimum) break;
                    }
                }
            }
            if (deadline < semester_last && minimum > 0) data.load_requirements.push_back(requirement);
            auto row = JsonValue::MakeObject();
            row.At("subgroup") = JsonValue::MakeNumber(part + 1);
            row.At("planned_hours") = JsonValue::MakeNumber(planned);
            row.At("confirmed_hours") = JsonValue::MakeNumber(taught);
            row.At("remaining_hours") = JsonValue::MakeNumber(left);
            row.At("up_hours_separate") = JsonValue::MakeNumber(up);
            row.At("minimum_period_pairs") = JsonValue::MakeNumber(minimum);
            row.At("required_hours_per_week") = capacity > 0 ? JsonValue::MakeNumber(double(left) * 7 / (DaysBetween(data.start_date, deadline) + 1)) : JsonValue::MakeNull();
            row.At("shortfall_hours") = JsonValue::MakeNumber(std::max(0, left - capacity * 2));
            parts.array_value.push_back(row);
            if (needed_pairs > capacity) {
                risk = true;
                issue("group_deadline_shortfall", requirement.label + ": осталось " + std::to_string(left) +
                    " ч, доступно не более " + std::to_string(capacity * 2) + " ч", -1, "error", group.id);
            }
            for (const auto& [teacher_id, demand] : teacher_pairs) {
                auto teacher = std::find_if(data.teachers.begin(), data.teachers.end(), [&](const auto& t) { return t.id == teacher_id; });
                const int available = teacher == data.teachers.end() ? 0 : GroupTeachingCapacity(data, group, data.start_date, deadline, &*teacher);
                if (demand > available) {
                    risk = true;
                    issue("group_teacher_deadline_shortfall", requirement.label + " / " +
                        (teacher == data.teachers.end() ? "преподаватель не назначен" : teacher->name) +
                        ": нужно " + std::to_string(demand * 2) + " ч, совместно доступно " + std::to_string(available * 2) + " ч", teacher_id, "error", group.id);
                }
            }
        }
        group_row.At("subgroups") = parts;
        group_row.At("status") = JsonValue::MakeString(risk ? "shortfall" : "capacity_upper_bound_ok");
        group_rows.array_value.push_back(group_row);
    }
    // Recompute selected totals only after every group has adjusted its quotas.
    // Earlier snapshots could report a deficit which had already been filled.
    for (const auto& requirement : data.load_requirements) {
        int selected = 0;
        for (const auto& lesson : data.lessons)
            if (requirement.lesson_ids.count(lesson.id)) selected += lesson.total_slots;
        for (auto& row : rows.array_value) if (JsonString(row, "label", "") == requirement.label)
            row.At("selected_period_pairs") = JsonValue::MakeNumber(selected);
        if (selected < requirement.minimum_pairs)
            issue("semester_quota_shortfall", requirement.label + ": квота периода " + std::to_string(selected) +
                " пар меньше требуемого темпа " + std::to_string(requirement.minimum_pairs), requirement.teacher);
    }

    auto details = JsonValue::MakeArray();
    for (auto& group_row : group_rows.array_value) {
        const int group_id = JsonInt(group_row, "group_id", -1);
        const auto& group = *std::find_if(data.groups.begin(), data.groups.end(), [&](const auto& g) { return g.id == group_id; });
        const auto deadline = GroupTeachingDeadline(group, semester_first, semester_last);
        const JsonValue* source_group = nullptr;
        for (const auto& g : root.At("groups").array_value) if (JsonInt(g, "id", -1) == group_id) source_group = &g;
        int teaching_days = 0, calendar_hours = 0, practice_left = 0;
        for (const auto& day : GenerateSchoolDays(data.start_date, deadline))
            if (GroupRegularCalendarAllows(group, day) && IsAvailable(day, group.id, data.unavailable)) ++teaching_days;
        for (const auto& week : group.academic_calendar)
            if (!(week.to < data.start_date) && !(deadline < week.from))
                calendar_hours += std::min(week.theory_hours, 2 * GroupTeachingCapacity(data, group,
                    std::max(data.start_date, week.from), std::min(deadline, week.to)));
        group_row.At("calendar_loaded") = JsonValue::MakeBool(!group.academic_calendar.empty());
        group_row.At("remaining_teaching_weeks") = JsonValue::MakeNumber(teaching_days / 6.0);
        group_row.At("calendar_theory_hours_upper_bound") = JsonValue::MakeNumber(calendar_hours);
        if (source_group) group_row.At("calendar_source") = source_group->At("practice_calendar_source");
        for (const auto& week : group.academic_calendar)
            if (week.theory_hours > 0 && week.pp_hours > 0)
                issue("mixed_practice_week", group.name + ": неделя " + DateToIso(week.from) + " — " + DateToIso(week.to) +
                    " содержит и теорию, и ПП. Точная дата перехода не указана; до уточнения обычные занятия запрещены с начала недели", -1, "warning", group.id);
        if (group.academic_calendar.empty())
            issue("academic_calendar_missing", group.name + ": полный календарь не загружен; каникулы, экзамены и УП не проверены", -1, "warning", group.id);
        else if (group.academic_calendar.front().from > data.start_date || group.academic_calendar.back().to < deadline)
            issue("academic_calendar_coverage", group.name + ": календарь не покрывает весь прогнозируемый период", -1, "error", group.id);
        std::map<int, int> teacher_demand;
        for (const auto& source : root.At("lessons").array_value) {
            if (JsonInt(source, "group", -1) != group_id || !JsonBool(source, "curriculum_active", true)) continue;
            const int id = JsonInt(source, "id", -1), teacher_id = JsonInt(source, "teacher", -1);
            const int total = std::max(0, JsonInt(source, "total_hours", 0));
            const int left = std::max(0, total - confirmed[id] - reserved[id]);
            const bool practice = JsonBool(source, "is_block", false) || JsonBool(source, "is_pp", false);
            if (practice) { practice_left += left; continue; }
            const auto t = std::find_if(data.teachers.begin(), data.teachers.end(), [&](const auto& x) { return x.id == teacher_id; });
            const std::string teacher_name = t == data.teachers.end() ? "Не назначен" : t->name;
            const int available = t == data.teachers.end() ? 0 : GroupTeachingCapacity(data, group, data.start_date, deadline, &*t);
            const std::string label = group.name + " / " + teacher_name + " / " + JsonString(source, "name", "Предмет");
            std::string status = left == 0 ? "completed" : "capacity_upper_bound_ok";
            if (confirmed[id] + reserved[id] > total)
                issue("ledger_and_reservations_exceed_curriculum", label + ": факт и уже запланированные часы превышают вклейку на " +
                    std::to_string(confirmed[id] + reserved[id] - total) + " ч", teacher_id, "error", group_id, id);
            if (left > 0) {
                teacher_demand[teacher_id] += left;
                if (t == data.teachers.end() || !t->scheduling_active || !JsonBool(source, "plan_active", true)) {
                    status = t == data.teachers.end() ? "teacher_missing" : !t->scheduling_active ? "teacher_inactive" : "lesson_disabled";
                    issue(status, label + ": осталось " + std::to_string(left) + " ч; " +
                        (t == data.teachers.end() ? "назначьте преподавателя" : !t->scheduling_active ? "преподаватель выключен из генерации" : "предмет выключен из плана генерации"), teacher_id, "error", group_id, id);
                } else if (left > available * 2) status = "shortfall";
                if (left % 2 || (JsonInt(source, "consecutive_pairs", 1) == 2 && left % 4)) {
                    status = "indivisible_remaining_hours";
                    issue(status, label + ": остаток " + std::to_string(left) + " ч не делится на размер обязательного занятия; проверьте вклейку и факт", teacher_id, "error", group_id, id);
                }
                if (deadline < data.start_date)
                    issue("deadline_overdue", label + ": срок уже прошёл, осталось " + std::to_string(left) + " ч", teacher_id, "error", group_id, id);
            }
            auto row = JsonValue::MakeObject();
            row.At("lesson_id") = JsonValue::MakeNumber(id);
            row.At("group_id") = JsonValue::MakeNumber(group_id);
            row.At("group_name") = JsonValue::MakeString(group.name);
            row.At("course_year") = JsonValue::MakeNumber(Course(root, group_id));
            row.At("teacher_id") = JsonValue::MakeNumber(teacher_id);
            row.At("teacher_name") = JsonValue::MakeString(teacher_name);
            row.At("subject") = source.At("name");
            row.At("kind") = JsonValue::MakeString(JsonBool(source, "is_lab", false) ? "ЛПЗ" : "Теория");
            row.At("subgroup") = JsonValue::MakeNumber(JsonInt(source, "subgroup", -1));
            row.At("planned_hours") = JsonValue::MakeNumber(total);
            row.At("confirmed_hours") = JsonValue::MakeNumber(confirmed[id]);
            row.At("reserved_hours") = JsonValue::MakeNumber(reserved[id]);
            row.At("remaining_before_reservations_hours") = JsonValue::MakeNumber(std::max(0, total - confirmed[id]));
            row.At("remaining_hours") = JsonValue::MakeNumber(left);
            row.At("deadline") = JsonValue::MakeString(DateToIso(deadline));
            row.At("shared_capacity_hours") = JsonValue::MakeNumber(available * 2);
            row.At("shortfall_hours") = JsonValue::MakeNumber(std::max(0, left - available * 2));
            row.At("required_hours_per_week") = teaching_days ? JsonValue::MakeNumber(left * 6.0 / teaching_days) : JsonValue::MakeNull();
            row.At("remaining_teaching_weeks") = JsonValue::MakeNumber(teaching_days / 6.0);
            row.At("status") = JsonValue::MakeString(status);
            details.array_value.push_back(row);
            if (status != "completed" && status != "capacity_upper_bound_ok") group_row.At("status") = JsonValue::MakeString("shortfall");
        }
        for (const auto& [teacher_id, demand] : teacher_demand) {
            const auto t = std::find_if(data.teachers.begin(), data.teachers.end(), [&](const auto& x) { return x.id == teacher_id; });
            const int available = t == data.teachers.end() ? 0 : GroupTeachingCapacity(data, group, data.start_date, deadline, &*t, std::clamp(group.parts, 1, 2));
            if (demand > available * 2 && t != data.teachers.end()) {
                group_row.At("status") = JsonValue::MakeString("shortfall");
                issue("teacher_group_combined_shortfall", group.name + " / " + t->name + ": по всем подгруппам нужно " +
                    std::to_string(demand) + " ч, совместно доступно не более " + std::to_string(available * 2) + " ч", teacher_id, "error", group_id);
            }
        }
        group_row.At("remaining_practice_hours") = JsonValue::MakeNumber(practice_left);
        if (practice_left > 0)
            issue("practice_staffing_unverified", group.name + ": УП/ПП — ещё " + std::to_string(practice_left) +
                " ч преподавательской нагрузки. Даты известны, но распределение наставников и рабочих мест не доказано", -1, "warning", group_id);
        for (auto& part : group_row.At("subgroups").array_value) {
            const int left = JsonInt(part, "remaining_hours", 0);
            part.At("required_hours_per_week") = teaching_days ? JsonValue::MakeNumber(left * 6.0 / teaching_days) : JsonValue::MakeNull();
            const int calendar_total = source_group ? JsonInt(*source_group, "calendar_theory_semester_hours", -1) : -1;
            if (calendar_total >= 0 && calendar_total != JsonInt(part, "planned_hours", 0))
                issue("curriculum_calendar_mismatch", group.name + " / " + std::to_string(JsonInt(part, "subgroup", 1)) +
                    " п/г: вклейки " + std::to_string(JsonInt(part, "planned_hours", 0)) + " ч обычных предметов, календарь " +
                    std::to_string(calendar_total) + " ч. Нагрузку автоматически не изменяем", -1, "warning", group_id);
            if (!group.academic_calendar.empty() && left > calendar_hours)
                issue("calendar_hours_shortfall", group.name + " / " + std::to_string(JsonInt(part, "subgroup", 1)) +
                    " п/г: осталось " + std::to_string(left) + " ч, в оставшихся учебных неделях календаря не более " +
                    std::to_string(calendar_hours) + " ч теории/ЛПЗ. Требуется сверка учебного плана и календаря", -1, "warning", group_id);
            part.At("calendar_shortfall_hours") = JsonValue::MakeNumber(group.academic_calendar.empty() ? 0 : std::max(0, left - calendar_hours));
        }
    }
    report.At("lessons") = details;
    report.At("groups") = group_rows;
    report.At("as_of_date") = JsonValue::MakeString(DateToIso(data.start_date));
    report.At("period_end_date") = JsonValue::MakeString(DateToIso(data.end_date));
    report.At("source") = JsonValue::MakeString("database_curriculum_and_confirmed_ledger");
    report.At("capacity_is_upper_bound") = JsonValue::MakeBool(true);
    report.At("full_semester_feasibility_proven") = JsonValue::MakeBool(false);
    report.At("semester_start_date") = JsonValue::MakeString(configured ? DateToIso(semester_first) : "");
    report.At("semester_end_date") = JsonValue::MakeString(configured ? DateToIso(semester_last) : "");
    report.At("semester_weeks") = JsonValue::MakeNumber(weeks);
    report.At("rows") = rows;
    report.At("deferred_teachers") = deferred;
    report.At("issues") = issues;
    bool has_errors = false;
    for (const auto& entry : issues.array_value)
        if (JsonString(entry, "severity", "error") == "error") has_errors = true;
    report.At("ok") = JsonValue::MakeBool(!has_errors);
}

std::string LoadRequirementError(const ScheduleInputData& data) {
    std::string error;
    for (const auto& issue : data.semester_readout_report.At("issues").array_value) {
        if (JsonString(issue, "severity", "error") != "error") continue;
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
