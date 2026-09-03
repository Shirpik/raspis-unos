#include "schedule_validator.h"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <tuple>
#include <utility>
#include <vector>

#include "config.h"
#include "date_utils.h"

namespace timetable {
namespace {

struct Event {
    Date date{};
    int pair = 0;  // 1..7
    int group = -1;
    int lesson = -1;
    int teacher = -1;
    int room = -1;
};

struct IssueCollector {
    JsonValue issues = JsonValue::MakeArray();
    std::map<std::string, std::pair<int, int>> category_counts;
    int hard_errors = 0;
    int warnings = 0;

    void Add(const std::string& severity, const std::string& category,
             const std::string& code, const std::string& message,
             JsonValue context = JsonValue::MakeObject()) {
        JsonValue issue = JsonValue::MakeObject();
        issue.At("severity") = JsonValue::MakeString(severity);
        issue.At("category") = JsonValue::MakeString(category);
        issue.At("code") = JsonValue::MakeString(code);
        issue.At("message") = JsonValue::MakeString(message);
        issue.At("context") = context.IsObject() ? context : JsonValue::MakeObject();
        issues.array_value.push_back(issue);
        if (severity == "error") {
            hard_errors++;
            category_counts[category].first++;
        } else {
            warnings++;
            category_counts[category].second++;
        }
    }
};

JsonValue Context() { return JsonValue::MakeObject(); }

void Put(JsonValue& value, const std::string& key, int number) {
    value.At(key) = JsonValue::MakeNumber(number);
}

void Put(JsonValue& value, const std::string& key, const std::string& text) {
    value.At(key) = JsonValue::MakeString(text);
}

std::string DateLabel(const Date& date) { return DateToIso(date); }

std::vector<Date> ExpectedDates(const Date& from, const Date& to) {
    std::vector<Date> result;
    if (to < from) return result;
    for (Date current = from; current <= to; current = NextDay(current)) {
        if (DayOfWeek(current) != 7) result.push_back(current);
    }
    return result;
}

bool DateInRanges(const Date& date,
                  const std::map<int, std::vector<std::pair<Date, Date>>>& ranges,
                  int id) {
    const auto it = ranges.find(id);
    if (it == ranges.end()) return false;
    for (const auto& range : it->second) {
        if (range.first <= date && date <= range.second) return true;
    }
    return false;
}

const GroupData* FindGroup(const ScheduleInputData& data, int id) {
    for (const GroupData& item : data.groups) if (item.id == id) return &item;
    return nullptr;
}

const TeacherData* FindTeacher(const ScheduleInputData& data, int id) {
    for (const TeacherData& item : data.teachers) if (item.id == id) return &item;
    return nullptr;
}

const RoomData* FindRoom(const ScheduleInputData& data, int id) {
    for (const RoomData& item : data.rooms) if (item.id == id) return &item;
    return nullptr;
}

const Lesson* FindLesson(const ScheduleInputData& data, int id) {
    for (const Lesson& item : data.lessons) if (item.id == id) return &item;
    return nullptr;
}

bool IsContiguous(const std::set<int>& slots) {
    return slots.empty() || (*slots.rbegin() - *slots.begin() + 1 == static_cast<int>(slots.size()));
}

int MondaySerial(const Date& semester_start, const Date& date) {
    return WeekIndexFromStart(semester_start, date);
}

bool LessonParityAllows(const Lesson& lesson, const Date& semester_start, const Date& date) {
    if (lesson.week_parity == "all") return true;
    const int one_based_week = MondaySerial(semester_start, date) + 1;
    return lesson.week_parity == "odd" ? one_based_week % 2 == 1 : one_based_week % 2 == 0;
}

std::string TeacherLabel(const TeacherData* teacher, int id) {
    return teacher ? teacher->name : "преподаватель #" + std::to_string(id);
}

std::string GroupLabel(const GroupData* group, int id) {
    return group ? group->name : "группа #" + std::to_string(id);
}

bool RoomFitsLesson(const RoomData& room, const Lesson& lesson) {
    if (lesson.required_room_type > 0 && room.room_type != lesson.required_room_type) return false;
    if (lesson.required_capacity > 0 && room.capacity < lesson.required_capacity) return false;
    if (room.purpose != lesson.required_room_purpose) return false;
    for (const std::string& equipment : lesson.required_equipment) {
        if (!room.equipment.count(equipment)) return false;
    }
    return true;
}

JsonValue SlotsJson(const std::set<int>& slots) {
    JsonValue value = JsonValue::MakeArray();
    for (int slot : slots) value.array_value.push_back(JsonValue::MakeNumber(slot));
    return value;
}

JsonValue CategoriesJson(const IssueCollector& collector) {
    static const std::vector<std::pair<std::string, std::string>> labels = {
        {"structure", "Структура и период"},
        {"availability", "Доступность"},
        {"conflicts", "Пересечения"},
        {"rooms", "Аудиторный фонд"},
        {"campus", "Площадки"},
        {"daily_load", "Суточная и недельная нагрузка"},
        {"windows", "Окна"},
        {"subject_repeat", "Повтор предметов"},
        {"theory_labs", "Теория и ЛПЗ"},
        {"quota", "Полнота вычитки"},
    };
    JsonValue result = JsonValue::MakeArray();
    for (const auto& item : labels) {
        const auto it = collector.category_counts.find(item.first);
        const int errors = it == collector.category_counts.end() ? 0 : it->second.first;
        const int warnings = it == collector.category_counts.end() ? 0 : it->second.second;
        JsonValue row = JsonValue::MakeObject();
        row.At("id") = JsonValue::MakeString(item.first);
        row.At("label") = JsonValue::MakeString(item.second);
        row.At("passed") = JsonValue::MakeBool(errors == 0);
        row.At("hard_errors") = JsonValue::MakeNumber(errors);
        row.At("warnings") = JsonValue::MakeNumber(warnings);
        result.array_value.push_back(row);
    }
    return result;
}

}  // namespace

ScheduleValidationResult ValidateScheduleJson(
    const ScheduleInputData& data,
    const RuntimeSolverConfig& config,
    const JsonValue& schedule,
    const ScheduleValidationOptions& options) {
    ScheduleValidationResult result;
    result.checked = true;
    IssueCollector collector;

    const std::vector<Date> expected_dates = ExpectedDates(data.start_date, data.end_date);
    const std::set<Date> expected_date_set(expected_dates.begin(), expected_dates.end());
    std::set<int> expected_groups;
    for (const GroupData& group : data.groups) expected_groups.insert(group.id);

    std::vector<Event> events;
    std::set<int> seen_groups;
    std::map<int, std::set<Date>> group_dates;

    if (!schedule.IsObject() || !schedule.At("groups").IsArray()) {
        collector.Add("error", "structure", "invalid_schedule_json",
                      "Расписание не содержит массив groups");
    } else {
        for (const JsonValue& group_output : schedule.At("groups").array_value) {
            const int group_id = JsonInt(group_output, "group_index", -1);
            const GroupData* group = FindGroup(data, group_id);
            if (!group) {
                JsonValue ctx = Context(); Put(ctx, "group", group_id);
                collector.Add("error", "structure", "unknown_output_group",
                              "В расписании есть неизвестная группа", ctx);
                continue;
            }
            if (!seen_groups.insert(group_id).second) {
                JsonValue ctx = Context(); Put(ctx, "group", group_id);
                collector.Add("error", "structure", "duplicate_output_group",
                              "Группа повторяется в итоговом JSON: " + group->name, ctx);
            }
            const JsonValue& days = group_output.At("days");
            if (!days.IsArray()) {
                JsonValue ctx = Context(); Put(ctx, "group", group_id);
                collector.Add("error", "structure", "group_days_missing",
                              "У группы отсутствует массив дней: " + group->name, ctx);
                continue;
            }
            for (const JsonValue& day : days.array_value) {
                Date date{};
                if (!ParseDateIso(JsonString(day, "date_iso", ""), date)) {
                    JsonValue ctx = Context(); Put(ctx, "group", group_id);
                    Put(ctx, "date", JsonString(day, "date_iso", ""));
                    collector.Add("error", "structure", "invalid_output_date",
                                  "Некорректная дата в итоговом JSON", ctx);
                    continue;
                }
                if (!group_dates[group_id].insert(date).second) {
                    JsonValue ctx = Context(); Put(ctx, "group", group_id); Put(ctx, "date", DateLabel(date));
                    collector.Add("error", "structure", "duplicate_group_date",
                                  "Дата повторяется у группы " + group->name, ctx);
                }
                if (!expected_date_set.count(date)) {
                    JsonValue ctx = Context(); Put(ctx, "group", group_id); Put(ctx, "date", DateLabel(date));
                    collector.Add("error", "structure", "event_day_outside_period",
                                  "В расписании есть день вне выбранного периода", ctx);
                }
                const JsonValue& slots = day.At("slots");
                if (!slots.IsArray()) continue;
                for (const JsonValue& slot : slots.array_value) {
                    const int pair = JsonInt(slot, "slot", 0);
                    const JsonValue& rendered_lessons = slot.At("lessons");
                    if (!rendered_lessons.IsArray()) continue;
                    const bool class_hour_slot = pair == 0 && std::all_of(
                        rendered_lessons.array_value.begin(), rendered_lessons.array_value.end(),
                        [](const JsonValue& rendered) {
                            return JsonBool(rendered, "is_class_hour", false);
                        });
                    if (class_hour_slot) continue;
                    if ((pair < 1 || pair > SLOTS_PER_DAY) && !rendered_lessons.array_value.empty()) {
                        JsonValue ctx = Context(); Put(ctx, "group", group_id); Put(ctx, "date", DateLabel(date)); Put(ctx, "pair", pair);
                        collector.Add("error", "structure", "invalid_output_pair",
                                      "Занятие поставлено на недопустимый номер пары", ctx);
                        continue;
                    }
                    for (const JsonValue& rendered : rendered_lessons.array_value) {
                        const int lesson_id = JsonInt(rendered, "id", -1);
                        const Lesson* lesson = FindLesson(data, lesson_id);
                        if (!lesson) {
                            JsonValue ctx = Context(); Put(ctx, "lesson", lesson_id); Put(ctx, "date", DateLabel(date)); Put(ctx, "pair", pair);
                            collector.Add("error", "structure", "unknown_output_lesson",
                                          "В расписании есть неизвестное или отключённое занятие", ctx);
                            continue;
                        }
                        if (lesson->group != group_id) {
                            JsonValue ctx = Context(); Put(ctx, "lesson", lesson_id); Put(ctx, "group", group_id); Put(ctx, "expected_group", lesson->group);
                            collector.Add("error", "structure", "lesson_in_wrong_group",
                                          "Занятие находится в чужой группе", ctx);
                        }
                        const int rendered_teacher = JsonInt(rendered, "teacher_id", lesson->teacher);
                        if (rendered_teacher != lesson->teacher) {
                            JsonValue ctx = Context(); Put(ctx, "lesson", lesson_id); Put(ctx, "teacher", rendered_teacher); Put(ctx, "expected_teacher", lesson->teacher);
                            collector.Add("error", "structure", "lesson_teacher_mismatch",
                                          "В итоговом JSON изменён преподаватель занятия", ctx);
                        }
                        const JsonValue& room_value = rendered.At("room_id");
                        const int room_id = room_value.IsNumber() ? static_cast<int>(room_value.number_value) : -1;
                        events.push_back({date, pair, group_id, lesson_id, lesson->teacher, room_id});
                    }
                }
            }
        }
    }

    if (options.require_full_period) {
        for (int group_id : expected_groups) {
            const GroupData* group = FindGroup(data, group_id);
            if (!seen_groups.count(group_id)) {
                JsonValue ctx = Context(); Put(ctx, "group", group_id);
                collector.Add("error", "structure", "group_missing_from_output",
                              "В итоговом JSON отсутствует группа " + GroupLabel(group, group_id), ctx);
                continue;
            }
        }
    }

    std::map<int, int> raw_occurrences;
    std::map<std::pair<int, Date>, std::set<int>> block_slots;
    std::map<std::tuple<Date, int, int>, std::vector<int>> teacher_slot;
    std::map<std::tuple<Date, int, int>, std::vector<int>> room_slot;
    std::map<std::tuple<Date, int, int, int>, std::vector<int>> part_slot;
    std::map<std::pair<int, Date>, std::set<int>> teacher_day_slots;
    std::map<std::tuple<int, int, Date>, std::set<int>> part_day_slots;
    std::map<std::tuple<int, Date, std::string>, int> whole_day_subject;
    std::map<std::tuple<int, int, Date, std::string>, int> part_day_subject;
    std::map<std::pair<int, Date>, std::set<int>> teacher_day_campuses;
    std::map<std::tuple<int, int, Date>, std::set<int>> part_day_campuses;
    std::set<std::tuple<int, Date, int, int>> teacher_nonpreferred_campuses;
    std::map<int, std::vector<Event>> events_by_lesson;

    for (const Event& event : events) {
        const Lesson* lesson = FindLesson(data, event.lesson);
        const GroupData* group = FindGroup(data, event.group);
        const TeacherData* teacher = FindTeacher(data, event.teacher);
        if (!lesson || !group) continue;
        raw_occurrences[event.lesson]++;
        events_by_lesson[event.lesson].push_back(event);
        if (lesson->is_block) block_slots[{event.lesson, event.date}].insert(event.pair);

        if (!expected_date_set.count(event.date)) {
            JsonValue ctx = Context(); Put(ctx, "lesson", event.lesson); Put(ctx, "date", DateLabel(event.date));
            collector.Add("error", "structure", "event_outside_period",
                          "Занятие находится вне выбранного периода", ctx);
        }
        if (!LessonParityAllows(*lesson, data.start_date, event.date)) {
            JsonValue ctx = Context(); Put(ctx, "lesson", event.lesson); Put(ctx, "date", DateLabel(event.date));
            collector.Add("error", "availability", "week_parity_mismatch",
                          "Занятие поставлено на запрещённую чётность недели", ctx);
        }
        if (DateInRanges(event.date, data.unavailable, event.group) ||
            !WorkScheduleAllows(group->work_schedule, event.date, event.pair - 1)) {
            JsonValue ctx = Context(); Put(ctx, "group", event.group); Put(ctx, "date", DateLabel(event.date)); Put(ctx, "pair", event.pair);
            collector.Add("error", "availability", "group_unavailable",
                          "Группа недоступна в это время: " + group->name, ctx);
        }
        if (!teacher) {
            JsonValue ctx = Context(); Put(ctx, "teacher", event.teacher); Put(ctx, "lesson", event.lesson);
            collector.Add("error", "availability", "unknown_teacher",
                          "У занятия отсутствует действующий преподаватель", ctx);
        } else if (DateInRanges(event.date, data.teacher_unavailable, event.teacher) ||
                   !WorkScheduleAllows(teacher->work_schedule, event.date, event.pair - 1)) {
            JsonValue ctx = Context(); Put(ctx, "teacher", event.teacher); Put(ctx, "date", DateLabel(event.date)); Put(ctx, "pair", event.pair);
            collector.Add("error", "availability", "teacher_unavailable",
                          TeacherLabel(teacher, event.teacher) + " недоступен(на) в это время", ctx);
        }

        teacher_slot[{event.date, event.pair, event.teacher}].push_back(event.lesson);
        teacher_day_slots[{event.teacher, event.date}].insert(event.pair);
        const std::string subject = SubjectFamilyKey(*lesson);
        for (int part = 0; part < std::max(1, group->parts); ++part) {
            if (!LessonAffectsPart(*lesson, event.group, part)) continue;
            part_slot[{event.date, event.pair, event.group, part}].push_back(event.lesson);
            part_day_slots[{event.group, part, event.date}].insert(event.pair);
            part_day_subject[{event.group, part, event.date, subject}]++;
        }
        if (lesson->subgroup == -1) whole_day_subject[{event.group, event.date, subject}]++;

        const RoomData* room = FindRoom(data, event.room);
        if (!room) {
            result.unassigned_rooms++;
            JsonValue ctx = Context(); Put(ctx, "lesson", event.lesson); Put(ctx, "date", DateLabel(event.date)); Put(ctx, "pair", event.pair);
            collector.Add("error", "rooms", "room_unassigned",
                          "Занятию не назначена существующая аудитория", ctx);
            continue;
        }
        room_slot[{event.date, event.pair, event.room}].push_back(event.lesson);
        teacher_day_campuses[{event.teacher, event.date}].insert(room->campus);
        for (int part = 0; part < std::max(1, group->parts); ++part) {
            if (LessonAffectsPart(*lesson, event.group, part))
                part_day_campuses[{event.group, part, event.date}].insert(room->campus);
        }
        if (!room->active || room->access_mode == "blocked") {
            JsonValue ctx = Context(); Put(ctx, "room", event.room); Put(ctx, "lesson", event.lesson); Put(ctx, "date", DateLabel(event.date)); Put(ctx, "pair", event.pair);
            collector.Add("error", "rooms", "blocked_room",
                          "Используется отключённая аудитория " + room->name, ctx);
        }
        if (!room->available_slots.empty() && !room->available_slots.count(event.pair)) {
            JsonValue ctx = Context(); Put(ctx, "room", event.room); Put(ctx, "date", DateLabel(event.date)); Put(ctx, "pair", event.pair);
            collector.Add("error", "rooms", "room_global_slot_unavailable",
                          "Аудитория недоступна на этой паре", ctx);
        }
        if (!WorkScheduleAllows(room->work_schedule, event.date, event.pair - 1)) {
            JsonValue ctx = Context(); Put(ctx, "room", event.room); Put(ctx, "date", DateLabel(event.date)); Put(ctx, "pair", event.pair);
            collector.Add("error", "rooms", "room_unavailable",
                          "Аудитория недоступна по календарю", ctx);
        }
        if (room->access_mode == "exclusive" && !room->responsible_teacher_ids.count(event.teacher)) {
            JsonValue ctx = Context(); Put(ctx, "room", event.room); Put(ctx, "teacher", event.teacher);
            collector.Add("error", "rooms", "exclusive_room_access",
                          "Преподаватель не имеет доступа к закреплённой аудитории", ctx);
        }
        if (!RoomFitsLesson(*room, *lesson)) {
            JsonValue ctx = Context(); Put(ctx, "room", event.room); Put(ctx, "lesson", event.lesson);
            collector.Add("error", "rooms", "room_requirements_mismatch",
                          "Аудитория не соответствует типу, вместимости, оборудованию или назначению занятия", ctx);
        }
        if (lesson->fixed_room >= 0 && event.room != lesson->fixed_room) {
            JsonValue ctx = Context(); Put(ctx, "lesson", event.lesson); Put(ctx, "room", event.room); Put(ctx, "expected_room", lesson->fixed_room);
            if (!lesson->allow_room_substitution) {
                collector.Add("error", "rooms", "fixed_room_mismatch",
                              "Нарушено жёсткое закрепление аудитории", ctx);
            } else if (options.include_soft_warnings) {
                collector.Add("warning", "rooms", "fixed_room_substituted",
                              "Вместо закреплённой аудитории использована допустимая замена", ctx);
            }
        }
        if (!lesson->allowed_campuses.count(static_cast<Campus>(room->campus))) {
            JsonValue ctx = Context(); Put(ctx, "lesson", event.lesson); Put(ctx, "campus", room->campus);
            collector.Add("error", "campus", "lesson_campus_mismatch",
                          "Занятие поставлено на запрещённую площадку", ctx);
        }
        if (teacher && !teacher->allowed_campuses.empty() &&
            !teacher->allowed_campuses.count(room->campus)) {
            JsonValue ctx = Context(); Put(ctx, "teacher", event.teacher); Put(ctx, "campus", room->campus);
            collector.Add("error", "campus", "teacher_campus_mismatch",
                          TeacherLabel(teacher, event.teacher) + " поставлен(а) на запрещённую площадку", ctx);
        } else if (teacher && options.include_soft_warnings &&
                   !teacher->campus_priority.empty() &&
                   room->campus != teacher->campus_priority.front()) {
            teacher_nonpreferred_campuses.insert(
                {event.teacher, event.date, room->campus, teacher->campus_priority.front()});
        }
    }

    for (const auto& item : teacher_nonpreferred_campuses) {
        const int teacher_id = std::get<0>(item);
        JsonValue ctx = Context(); Put(ctx, "teacher", teacher_id); Put(ctx, "date", DateLabel(std::get<1>(item))); Put(ctx, "campus", std::get<2>(item)); Put(ctx, "preferred_campus", std::get<3>(item));
        collector.Add("warning", "campus", "teacher_campus_not_preferred",
                      TeacherLabel(FindTeacher(data, teacher_id), teacher_id) +
                          " находится не на приоритетной площадке", ctx);
    }

    for (const auto& item : teacher_slot) {
        if (item.second.size() <= 1) continue;
        JsonValue ctx = Context(); Put(ctx, "date", DateLabel(std::get<0>(item.first))); Put(ctx, "pair", std::get<1>(item.first)); Put(ctx, "teacher", std::get<2>(item.first));
        collector.Add("error", "conflicts", "teacher_conflict",
                      "У преподавателя несколько занятий одновременно", ctx);
    }
    for (const auto& item : room_slot) {
        if (item.second.size() <= 1) continue;
        JsonValue ctx = Context(); Put(ctx, "date", DateLabel(std::get<0>(item.first))); Put(ctx, "pair", std::get<1>(item.first)); Put(ctx, "room", std::get<2>(item.first));
        collector.Add("error", "conflicts", "room_conflict",
                      "Аудитория занята несколькими занятиями одновременно", ctx);
    }
    for (const auto& item : part_slot) {
        if (item.second.size() <= 1) continue;
        JsonValue ctx = Context(); Put(ctx, "date", DateLabel(std::get<0>(item.first))); Put(ctx, "pair", std::get<1>(item.first)); Put(ctx, "group", std::get<2>(item.first)); Put(ctx, "part", std::get<3>(item.first) + 1);
        collector.Add("error", "conflicts", "student_conflict",
                      "У физической подгруппы несколько занятий одновременно", ctx);
    }

    for (const auto& item : teacher_day_campuses) {
        if (item.second.size() <= 1) continue;
        JsonValue ctx = Context(); Put(ctx, "teacher", item.first.first); Put(ctx, "date", DateLabel(item.first.second));
        collector.Add("error", "campus", "teacher_multi_campus_day",
                      "Преподаватель поставлен на разные площадки в один день", ctx);
    }
    for (const auto& item : part_day_campuses) {
        if (item.second.size() <= 1) continue;
        JsonValue ctx = Context(); Put(ctx, "group", std::get<0>(item.first)); Put(ctx, "part", std::get<1>(item.first) + 1); Put(ctx, "date", DateLabel(std::get<2>(item.first)));
        collector.Add("error", "campus", "student_multi_campus_day",
                      "Подгруппа поставлена на разные площадки в один день", ctx);
    }

    for (const auto& item : part_day_slots) {
        const int group_id = std::get<0>(item.first);
        const int part = std::get<1>(item.first);
        const Date& date = std::get<2>(item.first);
        const int count = static_cast<int>(item.second.size());
        if (count < config.min_student_pairs_per_study_day || count > config.max_student_pairs_per_day) {
            JsonValue ctx = Context(); Put(ctx, "group", group_id); Put(ctx, "part", part + 1); Put(ctx, "date", DateLabel(date)); Put(ctx, "count", count); Put(ctx, "min", config.min_student_pairs_per_study_day); Put(ctx, "max", config.max_student_pairs_per_day);
            collector.Add("error", "daily_load", "student_daily_load",
                          "Суточная нагрузка подгруппы выходит за заданные границы", ctx);
        }
        if (!IsContiguous(item.second)) {
            JsonValue ctx = Context(); Put(ctx, "group", group_id); Put(ctx, "part", part + 1); Put(ctx, "date", DateLabel(date)); ctx.At("slots") = SlotsJson(item.second);
            collector.Add(config.hard_no_student_windows ? "error" : "warning", "windows", "student_window",
                          "У подгруппы есть окно между занятиями", ctx);
        }
    }

    std::map<std::tuple<int, int, int>, int> part_week_days;
    std::map<std::tuple<int, int, int>, int> part_week_two_pair_days;
    for (const auto& item : part_day_slots) {
        const int week = MondaySerial(data.start_date, std::get<2>(item.first));
        part_week_days[{std::get<0>(item.first), std::get<1>(item.first), week}]++;
        if (item.second.size() == 2) part_week_two_pair_days[{std::get<0>(item.first), std::get<1>(item.first), week}]++;
    }
    if (config.hard_min_study_days_per_week || config.hard_max_one_two_pair_student_day) {
        std::set<int> weeks;
        for (const Date& date : expected_dates) weeks.insert(MondaySerial(data.start_date, date));
        for (const GroupData& group : data.groups) {
            for (int part = 0; part < std::max(1, group.parts); ++part) {
                for (int week : weeks) {
                    if (config.hard_min_study_days_per_week) {
                        int available_days = 0;
                        for (const Date& date : expected_dates) {
                            if (MondaySerial(data.start_date, date) != week) continue;
                            if (DateInRanges(date, data.unavailable, group.id)) continue;
                            bool any_slot = false;
                            for (int pair = 1; pair <= SLOTS_PER_DAY; ++pair)
                                any_slot = any_slot || WorkScheduleAllows(group.work_schedule, date, pair - 1);
                            if (any_slot) available_days++;
                        }
                        const int required = std::min(config.min_student_study_days_per_week, available_days);
                        const int actual = part_week_days[{group.id, part, week}];
                        if (actual < required) {
                            JsonValue ctx = Context(); Put(ctx, "group", group.id); Put(ctx, "part", part + 1); Put(ctx, "week", week + 1); Put(ctx, "actual", actual); Put(ctx, "required", required);
                            collector.Add("error", "daily_load", "student_study_days",
                                          "Недостаточно учебных дней у подгруппы за неделю", ctx);
                        }
                    }
                    if (config.hard_max_one_two_pair_student_day &&
                        part_week_two_pair_days[{group.id, part, week}] > 1) {
                        JsonValue ctx = Context(); Put(ctx, "group", group.id); Put(ctx, "part", part + 1); Put(ctx, "week", week + 1); Put(ctx, "days", part_week_two_pair_days[{group.id, part, week}]);
                        collector.Add("error", "daily_load", "too_many_two_pair_student_days",
                                      "У подгруппы больше одного двухпарного дня за неделю", ctx);
                    }
                }
            }
        }
    }

    std::map<std::pair<int, int>, int> teacher_week_days;
    for (const auto& item : teacher_day_slots) {
        const int teacher_id = item.first.first;
        const Date& date = item.first.second;
        const TeacherData* teacher = FindTeacher(data, teacher_id);
        const int count = static_cast<int>(item.second.size());
        if (teacher && teacher->max_pairs_per_day > 0 && count > teacher->max_pairs_per_day) {
            JsonValue ctx = Context(); Put(ctx, "teacher", teacher_id); Put(ctx, "date", DateLabel(date)); Put(ctx, "count", count); Put(ctx, "max", teacher->max_pairs_per_day);
            collector.Add("error", "daily_load", "teacher_daily_limit",
                          "Превышен суточный максимум преподавателя", ctx);
        }
        if (config.hard_min_2_teacher_pairs_per_day && count < 2) {
            JsonValue ctx = Context(); Put(ctx, "teacher", teacher_id); Put(ctx, "date", DateLabel(date)); Put(ctx, "count", count);
            collector.Add("error", "daily_load", "teacher_daily_minimum",
                          "У работающего преподавателя меньше двух пар за день", ctx);
        }
        if (!IsContiguous(item.second)) {
            JsonValue ctx = Context(); Put(ctx, "teacher", teacher_id); Put(ctx, "date", DateLabel(date)); ctx.At("slots") = SlotsJson(item.second);
            collector.Add(config.hard_no_teacher_windows ? "error" : "warning", "windows", "teacher_window",
                          "У преподавателя есть окно между занятиями", ctx);
        }
        teacher_week_days[{teacher_id, MondaySerial(data.start_date, date)}]++;
    }
    for (const auto& item : teacher_week_days) {
        const TeacherData* teacher = FindTeacher(data, item.first.first);
        if (teacher && teacher->max_work_days_per_week > 0 && item.second > teacher->max_work_days_per_week) {
            JsonValue ctx = Context(); Put(ctx, "teacher", teacher->id); Put(ctx, "week", item.first.second + 1); Put(ctx, "count", item.second); Put(ctx, "max", teacher->max_work_days_per_week);
            collector.Add("error", "daily_load", "teacher_weekly_days_limit",
                          "Превышен максимум рабочих дней преподавателя за неделю", ctx);
        }
    }

    if (config.hard_max_two_same_subject_per_day) {
        for (const auto& item : whole_day_subject) {
            if (item.second <= config.max_whole_group_same_subject_pairs_per_day) continue;
            JsonValue ctx = Context(); Put(ctx, "group", std::get<0>(item.first)); Put(ctx, "date", DateLabel(std::get<1>(item.first))); Put(ctx, "subject", std::get<2>(item.first)); Put(ctx, "count", item.second); Put(ctx, "max", config.max_whole_group_same_subject_pairs_per_day);
            collector.Add("error", "subject_repeat", "whole_group_same_subject_daily_limit",
                          "Слишком много общегрупповых пар одного предмета за день", ctx);
        }
        for (const auto& item : part_day_subject) {
            if (item.second <= config.max_same_subject_pairs_per_day) continue;
            JsonValue ctx = Context(); Put(ctx, "group", std::get<0>(item.first)); Put(ctx, "part", std::get<1>(item.first) + 1); Put(ctx, "date", DateLabel(std::get<2>(item.first))); Put(ctx, "subject", std::get<3>(item.first)); Put(ctx, "count", item.second); Put(ctx, "max", config.max_same_subject_pairs_per_day);
            collector.Add("error", "subject_repeat", "physical_subgroup_same_subject_daily_limit",
                          "Слишком много пар одного предмета у физической подгруппы за день", ctx);
        }
    }

    std::map<int, int> scheduled_occurrences = raw_occurrences;
    for (const Lesson& lesson : data.lessons) {
        if (!lesson.is_block) continue;
        int starts = 0;
        for (const auto& item : block_slots) {
            if (item.first.first != lesson.id) continue;
            for (int pair : item.second) if (!item.second.count(pair - 1)) starts++;
        }
        scheduled_occurrences[lesson.id] = starts;
    }
    for (const Lesson& lesson : data.lessons) {
        result.planned_occurrences += std::max(0, lesson.total_slots);
        const int actual = scheduled_occurrences[lesson.id];
        result.scheduled_occurrences += actual;
        if (!options.require_exact_quotas || actual == lesson.total_slots) continue;
        result.mismatched_lessons++;
        JsonValue ctx = Context(); Put(ctx, "lesson", lesson.id); Put(ctx, "name", lesson.name); Put(ctx, "expected", lesson.total_slots); Put(ctx, "actual", actual);
        collector.Add("error", "quota", "lesson_quota_mismatch",
                      "Не совпадает квота занятия «" + lesson.name + "»", ctx);
        const int hours_per_occurrence = lesson.is_block ? 4 : 2;
        if (actual < lesson.total_slots) {
            result.incomplete_lessons++;
            result.remaining_hours += static_cast<long long>(lesson.total_slots - actual) * hours_per_occurrence;
        } else {
            result.excess_hours += static_cast<long long>(actual - lesson.total_slots) * hours_per_occurrence;
        }
    }

    std::map<int, int> teacher_planned;
    std::map<int, int> teacher_actual;
    for (const Lesson& lesson : data.lessons) {
        if (lesson.teacher < 0) continue;
        teacher_planned[lesson.teacher] += lesson.total_slots;
        teacher_actual[lesson.teacher] += scheduled_occurrences[lesson.id];
    }
    for (const auto& target : data.teacher_period_targets) {
        const TeacherData* teacher = FindTeacher(data, target.first);
        if (teacher_planned[target.first] < target.second) {
            JsonValue ctx = Context(); Put(ctx, "teacher", target.first); Put(ctx, "minimum_pairs", target.second); Put(ctx, "planned_pairs", teacher_planned[target.first]);
            collector.Add("error", "quota", "teacher_period_target_not_planned",
                          "В квоты не включена требуемая нагрузка: " + TeacherLabel(teacher, target.first), ctx);
        }
        if (teacher_actual[target.first] < target.second) {
            JsonValue ctx = Context(); Put(ctx, "teacher", target.first); Put(ctx, "minimum_pairs", target.second); Put(ctx, "scheduled_pairs", teacher_actual[target.first]);
            collector.Add("error", "quota", "teacher_period_target_not_scheduled",
                          "Не вычитан минимум преподавателя за период: " + TeacherLabel(teacher, target.first), ctx);
        }
    }

    // Проверка порядка теории и ЛПЗ независимо от модели.
    for (const Lesson& lab : data.lessons) {
        if (!lab.is_lab) continue;
        std::vector<const Lesson*> theories;
        int current_theory_total = 0;
        for (const Lesson& theory : data.lessons) {
            if (theory.group == lab.group && theory.subject_id == lab.subject_id &&
                !theory.is_lab && !theory.is_block && !theory.is_pp) {
                theories.push_back(&theory);
                current_theory_total += scheduled_occurrences[theory.id];
            }
        }
        if (theories.empty()) continue;
        int prior = 0;
        const auto prior_it = data.prior_theory_pairs.find({lab.group, lab.subject_id});
        if (prior_it != data.prior_theory_pairs.end()) prior = prior_it->second;
        for (const Event& lab_event : events_by_lesson[lab.id]) {
            int theory_before = prior;
            for (const Lesson* theory : theories) {
                for (const Event& event : events_by_lesson[theory->id]) {
                    if (event.date < lab_event.date || (event.date == lab_event.date && event.pair < lab_event.pair))
                        theory_before++;
                }
            }
            const int required = config.strict_all_theory_before_labs
                ? prior + current_theory_total
                : config.min_initial_theory_slots_before_labs;
            if (theory_before < required) {
                JsonValue ctx = Context(); Put(ctx, "lesson", lab.id); Put(ctx, "date", DateLabel(lab_event.date)); Put(ctx, "pair", lab_event.pair); Put(ctx, "theory_before", theory_before); Put(ctx, "required", required);
                collector.Add("error", "theory_labs", "theory_before_lab_violation",
                              "ЛПЗ поставлена раньше требуемого объёма теории", ctx);
            }
        }
    }

    result.event_count = static_cast<int>(events.size());
    result.hard_error_count = collector.hard_errors;
    result.warning_count = collector.warnings;
    result.ok = collector.hard_errors == 0;
    if (result.ok) {
        result.message = result.warning_count == 0
            ? "Полная проверка пройдена: жёсткие ограничения соблюдены"
            : "Жёсткие ограничения соблюдены; есть рекомендаций: " + std::to_string(result.warning_count);
    } else {
        result.message = "Полная проверка не пройдена: нарушений " +
            std::to_string(result.hard_error_count) + ", предупреждений " +
            std::to_string(result.warning_count);
    }

    JsonValue summary = JsonValue::MakeObject();
    summary.At("hard_errors") = JsonValue::MakeNumber(result.hard_error_count);
    summary.At("warnings") = JsonValue::MakeNumber(result.warning_count);
    summary.At("events") = JsonValue::MakeNumber(result.event_count);
    summary.At("planned_occurrences") = JsonValue::MakeNumber(result.planned_occurrences);
    summary.At("scheduled_occurrences") = JsonValue::MakeNumber(result.scheduled_occurrences);
    summary.At("remaining_hours") = JsonValue::MakeNumber(static_cast<double>(result.remaining_hours));
    summary.At("excess_hours") = JsonValue::MakeNumber(static_cast<double>(result.excess_hours));
    summary.At("incomplete_lessons") = JsonValue::MakeNumber(result.incomplete_lessons);
    summary.At("mismatched_lessons") = JsonValue::MakeNumber(result.mismatched_lessons);
    summary.At("unassigned_rooms") = JsonValue::MakeNumber(result.unassigned_rooms);

    result.report = JsonValue::MakeObject();
    result.report.At("checked") = JsonValue::MakeBool(true);
    result.report.At("ok") = JsonValue::MakeBool(result.ok);
    result.report.At("source") = JsonValue::MakeString(options.source);
    result.report.At("message") = JsonValue::MakeString(result.message);
    result.report.At("period_from") = JsonValue::MakeString(DateLabel(data.start_date));
    result.report.At("period_to") = JsonValue::MakeString(DateLabel(data.end_date));
    result.report.At("effective_solver_config") = SolverConfigToJson(config);
    result.report.At("summary") = summary;
    result.report.At("categories") = CategoriesJson(collector);
    result.report.At("issues") = collector.issues;
    return result;
}

}  // namespace timetable
