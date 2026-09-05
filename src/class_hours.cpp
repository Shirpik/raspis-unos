#include "class_hours.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <tuple>

#include "date_utils.h"
#include "schedule_validator.h"
#include "ortools/sat/cp_model.h"
#include "ortools/sat/cp_model_solver.h"

namespace timetable {
namespace {
struct AcademicEvent {
    Date date; int group; int teacher; int room; int pair; int campus; TimeInterval time; int subgroup;
};
template<class T> const T* Find(const std::vector<T>& items, int id) {
    for (const auto& item : items) if (item.id == id) return &item;
    return nullptr;
}
TimeInterval ClassTime(int pair) {
    if (pair == 0) return {MakeMinute(8, 15), MakeMinute(8, 55)};
    auto interval = PairSlotInterval(1, pair - 1);
    interval.from_minute = interval.to_minute - 40;
    return interval;
}
bool DateAvailable(const WorkSchedule& work, const Date& date) {
    for (int slot = 0; slot < SLOTS_PER_DAY; ++slot)
        if (WorkScheduleAllows(work, date, slot)) return true;
    return false;
}
bool Required(const ScheduleInputData& data, const GroupData& group, const Date& date) {
    if (DayOfWeek(date) != 1 || !group.class_hour_enabled || !IsAvailable(date, group.id, data.unavailable)) return false;
    const auto special = data.unavailable_day_texts.find(group.id);
    if (special != data.unavailable_day_texts.end() && special->second.count(date)) return false;
    return DateAvailable(group.work_schedule, date) && (data.require_class_hours || group.curator_teacher >= 0);
}
std::vector<AcademicEvent> Academic(const ScheduleInputData& data, const JsonValue& schedule) {
    std::vector<AcademicEvent> events;
    for (const auto& group : schedule.At("groups").array_value)
        for (const auto& day : group.At("days").array_value) {
            Date date{};
            if (!ParseDateIso(JsonString(day, "date_iso", ""), date)) continue;
            for (const auto& slot : day.At("slots").array_value) {
                int pair = JsonInt(slot, "slot", -1);
                if (pair < 1 || pair > 7) continue;
                for (const auto& event : slot.At("lessons").array_value) {
                    if (JsonBool(event, "is_class_hour", false)) continue;
                    const auto* lesson = Find(data.lessons, JsonInt(event, "id", -1));
                    if (!lesson) continue;
                    const auto* room = Find(data.rooms, JsonInt(event, "room_id", -1));
                    auto time = PairSlotInterval(DayOfWeek(date), pair - 1);
                    if (lesson->is_block && IsAllowedUpStartSlot(date, pair - 1))
                        time = UpIntervalForStartSlot(date, pair - 1);
                    events.push_back({date, lesson->group, lesson->teacher,
                        room ? room->id : -1, pair, room ? room->campus : -1, time, lesson->subgroup});
                }
            }
        }
    return events;
}
bool ContinuousStudentDay(const std::vector<AcademicEvent>& events,
                          const GroupData& group, const Date& date, int pair) {
    for (int part = 0; part < group.parts; ++part) {
        std::set<int> occupied{pair};
        for (const auto& event : events)
            if (event.date == date && event.group == group.id &&
                (event.subgroup < 0 || event.subgroup == group.id * PARTS_PER_GROUP + part))
                occupied.insert(event.pair);
        if (*occupied.rbegin() - *occupied.begin() + 1 != static_cast<int>(occupied.size())) return false;
    }
    return true;
}
bool Fits(const ScheduleInputData& data, const std::vector<AcademicEvent>& events,
          const GroupData& group, const TeacherData& teacher, const RoomData& room,
          const Date& date, int pair) {
    if (pair != 0 && (pair < 2 || pair > 7)) return false;
    const bool curator_exception = teacher.class_hour_available_dates.count(date) > 0;
    if (!teacher.scheduling_active) return false;
    if (!curator_exception && (!IsAvailable(date, teacher.id, data.teacher_unavailable) || !DateAvailable(teacher.work_schedule, date))) return false;
    if (pair && ((!curator_exception && !WorkScheduleAllows(teacher.work_schedule, date, pair - 1)) ||
                 !WorkScheduleAllows(group.work_schedule, date, pair - 1))) return false;
    if (!room.class_hour_open && (!room.active || room.access_mode != "general" ||
        (room.room_type != 0 && room.room_type != 1) || !room.purpose.empty())) return false;
    if (pair == 0 && room.class_hour_zero_blocked) return false;
    if (!WorkScheduleAllows(room.work_schedule, date, std::max(0, pair - 1))) return false;
    if (pair && !room.available_slots.empty() && !room.available_slots.count(pair)) return false;
    if (room.access_mode == "exclusive" && !room.responsible_teacher_ids.count(teacher.id)) return false;
    if (room.capacity > 0 && group.size > room.capacity) return false;
    if (group.class_hour_campus >= 0 && group.class_hour_campus != room.campus) return false;
    int first = 8, last = 0;
    for (const auto& event : events) {
        if (!(event.date == date)) continue;
        if (event.group == group.id) {
            first = std::min(first, event.pair); last = std::max(last, event.pair);
            if (event.campus >= 0 && event.campus != room.campus) return false;
            // Both halves are reserved, including the intentionally free first half.
            if (event.pair == pair) return false;
        }
        if (pair && event.teacher == teacher.id && event.campus >= 0 && event.campus != room.campus) return false;
        if ((event.group == group.id || event.teacher == teacher.id || event.room == room.id) &&
            IntervalsOverlap(event.time, ClassTime(pair))) return false;
    }
    // Do not create a gap in a student's day before a late class hour.
    if (pair && last && (pair < first - 1 || pair > last + 1)) return false;
    // Zero lesson is also part of the student's day. A zero class hour followed
    // by pair 2 (or later) is a window, even when ordinary pairs are contiguous.
    return ContinuousStudentDay(events, group, date, pair);
}
void Error(JsonValue& issues, const std::string& code, const std::string& message, int group, const Date& date) {
    auto issue = JsonValue::MakeObject();
    issue.At("code") = JsonValue::MakeString(code);
    issue.At("message") = JsonValue::MakeString(message);
    issue.At("group") = JsonValue::MakeNumber(group);
    issue.At("date") = JsonValue::MakeString(DateToIso(date));
    issues.array_value.push_back(issue);
}
bool Write(const std::filesystem::path& path, const JsonValue& value) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << ToJson(value, 2);
    stream.close();
    return bool(stream);
}
bool WriteDerivedText(const ScheduleInputData& data, const JsonValue& schedule, const std::filesystem::path& directory) {
    std::ostringstream all, csv;
    std::map<int, std::map<std::pair<std::string, int>, std::vector<std::string>>> by_teacher;
    auto quote = [](std::string value) {
        for (size_t pos = 0; (pos = value.find('"', pos)) != std::string::npos; pos += 2) value.insert(pos, 1, '"');
        return "\"" + value + "\"";
    };
    auto write_text = [](const auto& path, const std::string& text) {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream << text; stream.close(); return bool(stream);
    };
    csv << "Группа;Дата;Пара;Время;Занятия\r\n";
    for (const auto& group : schedule.At("groups").array_value) {
        std::ostringstream text;
        const auto name = JsonString(group, "group_name", "");
        text << name << "\n";
        for (const auto& day : group.At("days").array_value) {
            const auto date = JsonString(day, "date_iso", "");
            text << "\n" << date << "\n";
            if (JsonString(day, "weekday", "") == "ПН") text << "07:55 — поднятие флага\n";
            for (const auto& slot : day.At("slots").array_value) {
                const int pair = JsonInt(slot, "slot", -1);
                const auto time = JsonString(slot, "time", "");
                const auto label = JsonString(slot, "text", "-");
                text << pair << ". " << time << " — " << label << "\n";
                csv << quote(name) << ';' << quote(date) << ';' << pair << ';' << quote(time) << ';' << quote(label) << "\r\n";
                for (const auto& lesson : slot.At("lessons").array_value) {
                    const auto* source = Find(data.lessons, JsonInt(lesson, "id", -1));
                    int teacher = JsonInt(lesson, "teacher_id", source ? source->teacher : -1);
                    if (teacher >= 0) by_teacher[teacher][{date, pair}].push_back(
                        time + " — " + name + " — " + JsonString(lesson, "name", "") + " — " + JsonString(lesson, "room_name", ""));
                }
            }
        }
        all << text.str() << "\n";
        if (!write_text(directory / "groups" / ("raspisanie_group_" + std::to_string(JsonInt(group, "group_index", -1)) + ".txt"), text.str())) return false;
    }
    std::ostringstream teachers;
    for (const auto& entry : by_teacher) {
        const auto* teacher = Find(data.teachers, entry.first);
        teachers << (teacher ? teacher->name : std::to_string(entry.first)) << "\n";
        for (const auto& session : entry.second) for (const auto& line : session.second)
            teachers << session.first.first << " " << line << "\n";
        teachers << "\n";
    }
    return write_text(directory / "raspisanie_all.txt", all.str()) &&
        write_text(directory / "raspisanie_groups.csv", csv.str()) &&
        write_text(directory / "raspisanie_teachers.txt", teachers.str());
}
}

void AddClassHourTimeConstraints(
    operations_research::sat::CpModelBuilder& model,
    const ScheduleInputData& data, const std::vector<Date>& days,
    const std::vector<std::vector<std::vector<operations_research::sat::BoolVar>>>& part_busy,
    const std::vector<std::vector<operations_research::sat::BoolVar>>& teacher_busy,
    const std::vector<std::vector<operations_research::sat::IntVar>>& group_campus,
    const std::vector<std::vector<operations_research::sat::IntVar>>& teacher_campus
) {
    using namespace operations_research::sat;
    for (int day = 0; day < static_cast<int>(days.size()); ++day) {
        if (DayOfWeek(days[day]) != 1) continue;
        std::map<std::tuple<int,int,int>, std::vector<BoolVar>> sessions;
        for (const auto& group : data.groups) {
            if (!Required(data, group, days[day])) continue;
            const auto* teacher = Find(data.teachers, group.curator_teacher);
            std::vector<BoolVar> choices;
            if (teacher && group.id >= 0 && group.id < static_cast<int>(part_busy.size()) &&
                teacher->id >= 0 && teacher->id < static_cast<int>(teacher_busy.size())) {
                for (int pair : {0,2,3,4,5,6,7}) {
                    std::set<int> campuses;
                    for (const auto& room : data.rooms)
                        if (Fits(data, {}, group, *teacher, room, days[day], pair)) campuses.insert(room.campus);
                    for (int campus : campuses) {
                        auto choice = model.NewBoolVar();
                        choices.push_back(choice);
                        sessions[{teacher->id,pair,campus}].push_back(choice);
                        model.AddEquality(group_campus[group.id][day], campus).OnlyEnforceIf(choice);
                        if (pair) {
                            model.AddEquality(teacher_busy[teacher->id][day*SLOTS_PER_DAY+pair-1],0).OnlyEnforceIf(choice);
                            model.AddEquality(teacher_campus[teacher->id][day], campus).OnlyEnforceIf(choice);
                        }
                        for (int part=0; part<std::min(group.parts,static_cast<int>(part_busy[group.id].size())); ++part) {
                            const auto& busy = part_busy[group.id][part];
                            if (pair) model.AddEquality(busy[day*SLOTS_PER_DAY+pair-1],0).OnlyEnforceIf(choice);
                            // Every full pair between class hour and any regular
                            // pair must be occupied by this physical subgroup.
                            for (int other=1; other<=SLOTS_PER_DAY; ++other)
                                for (int between=std::min(pair,other)+1; between<std::max(pair,other); ++between)
                                    model.AddLessOrEqual(busy[day*SLOTS_PER_DAY+other-1],busy[day*SLOTS_PER_DAY+between-1]).OnlyEnforceIf(choice);
                        }
                    }
                }
            }
            model.AddEquality(LinearExpr::Sum(choices),1);
        }
        std::map<std::pair<int,int>,std::vector<BoolVar>> teacher_sessions;
        for (const auto& entry : sessions) {
            const auto [teacher,pair,campus] = entry.first;
            auto session = model.NewBoolVar();
            model.AddLessOrEqual(LinearExpr::Sum(entry.second),session*2);
            model.AddGreaterOrEqual(LinearExpr::Sum(entry.second),session);
            teacher_sessions[{teacher,pair}].push_back(session);
        }
        for (const auto& entry : teacher_sessions) model.AddLessOrEqual(LinearExpr::Sum(entry.second),1);
    }
}

bool PlanClassHours(const ScheduleInputData& data, JsonValue& schedule, std::string& error) {
    using namespace operations_research::sat;
    JsonValue candidate = schedule;
    // Discard the legacy decorative entries; none count as an allocation.
    for (auto& group : candidate.At("groups").array_value)
        for (auto& day : group.At("days").array_value) {
            auto& slots = day.At("slots").array_value;
            for (auto& slot : slots) {
                auto& lessons = slot.At("lessons").array_value;
                const auto old_size = lessons.size();
                lessons.erase(std::remove_if(lessons.begin(), lessons.end(), [](const auto& lesson) {
                    return JsonBool(lesson, "is_class_hour", false);
                }), lessons.end());
                if (lessons.size() != old_size && lessons.empty()) slot.At("text") = JsonValue::MakeString("-");
            }
            slots.erase(std::remove_if(slots.begin(), slots.end(), [](const auto& slot) {
                return JsonInt(slot, "slot", -1) == 0 && slot.At("lessons").array_value.empty();
            }), slots.end());
        }
    const auto events = Academic(data, candidate);
    for (const auto& date : GenerateSchoolDays(data.start_date, data.end_date)) {
        if (DayOfWeek(date) != 1) continue;
        CpModelBuilder model;
        struct Option { int group; int teacher; int room; int pair; BoolVar var; };
        std::vector<Option> choices;
        std::map<std::tuple<int, int, int>, std::vector<BoolVar>> sessions;
        std::map<std::tuple<int, int, int>, LinearExpr> students;
        LinearExpr objective;
        std::map<int, int> assumption_groups;
        for (const auto& group : data.groups) {
            if (!Required(data, group, date)) continue;
            const auto* teacher = Find(data.teachers, group.curator_teacher);
            if (!teacher) { error = "Не определён куратор группы " + group.name; return false; }
            std::vector<BoolVar> group_choices;
            for (int pair : {0, 2, 3, 4, 5, 6, 7}) for (const auto& room : data.rooms) {
                if (!Fits(data, events, group, *teacher, room, date, pair)) continue;
                auto var = model.NewBoolVar();
                choices.push_back({group.id, teacher->id, room.id, pair, var});
                group_choices.push_back(var);
                auto key = std::make_tuple(teacher->id, pair, room.id);
                sessions[key].push_back(var);
                students[key] += var * std::max(0, group.size);
                objective += var * ((room.class_hour_fallback ? 100000 : 0) + (pair ? 1000 + pair : 0) +
                    (group.class_hour_room >= 0 && group.class_hour_room != room.id ? 10 : 0));
            }
            if (group_choices.empty()) { error = "Нет допустимого времени/кабинета для классного часа: " + group.name + " (" + teacher->name + ")"; return false; }
            const auto required = model.NewBoolVar();
            assumption_groups[required.index()] = group.id;
            model.AddAssumption(required);
            model.AddEquality(LinearExpr::Sum(group_choices), 1).OnlyEnforceIf(required);
        }
        std::map<std::pair<int, int>, std::vector<BoolVar>> teacher_rooms, room_teachers;
        for (const auto& entry : sessions) {
            const auto [teacher, pair, room_id] = entry.first;
            auto session = model.NewBoolVar();
            model.AddLessOrEqual(LinearExpr::Sum(entry.second), session * 2);
            model.AddGreaterOrEqual(LinearExpr::Sum(entry.second), session);
            const auto* room = Find(data.rooms, room_id);
            if (room && room->capacity > 0) model.AddLessOrEqual(students[entry.first], room->capacity);
            teacher_rooms[{teacher, pair}].push_back(session);
            room_teachers[{room_id, pair}].push_back(session);
        }
        for (const auto& entry : teacher_rooms) model.AddLessOrEqual(LinearExpr::Sum(entry.second), 1);
        for (const auto& entry : room_teachers) model.AddLessOrEqual(LinearExpr::Sum(entry.second), 1);
        if (choices.empty()) continue;
        model.Minimize(objective);
        Model solver;
        SatParameters parameters;
        parameters.set_max_time_in_seconds(15);
        parameters.set_num_search_workers(4);
        solver.Add(NewSatParameters(parameters));
        const auto solution = SolveCpModel(model.Build(), &solver);
        if (solution.status() != CpSolverStatus::OPTIMAL && solution.status() != CpSolverStatus::FEASIBLE) {
            error = "Классные часы не размещены без конфликтов. Нужны свободные кабинеты/время кураторов; обычные пары не изменены.";
            for (int literal : solution.sufficient_assumptions_for_infeasibility()) {
                const int index = literal >= 0 ? literal : -literal - 1;
                if (!assumption_groups.count(index)) continue;
                const auto* group = Find(data.groups, assumption_groups[index]);
                error += "\n" + group->name + ": ";
                std::set<std::pair<int, int>> times;
                for (const auto& option : choices) if (option.group == group->id)
                    times.insert({option.pair, Find(data.rooms, option.room)->campus});
                for (const auto& time : times) error += std::to_string(time.first) + "@" + std::to_string(time.second) + " ";
            }
            return false;
        }
        for (const auto& choice : choices) {
            if (!SolutionBooleanValue(solution, choice.var)) continue;
            const auto* teacher = Find(data.teachers, choice.teacher);
            const auto* room = Find(data.rooms, choice.room);
            auto lesson = JsonValue::MakeObject();
            lesson.At("id") = JsonValue::MakeNumber(-1);
            lesson.At("uid") = JsonValue::MakeString("class-hour-" + std::to_string(choice.group) + "-" + DateToIso(date));
            lesson.At("name") = JsonValue::MakeString("Классный час");
            lesson.At("teacher_id") = JsonValue::MakeNumber(choice.teacher);
            lesson.At("teacher_name") = JsonValue::MakeString(teacher->name);
            lesson.At("room_id") = JsonValue::MakeNumber(choice.room);
            lesson.At("room_name") = JsonValue::MakeString(room->name);
            lesson.At("campus") = JsonValue::MakeNumber(room->campus);
            lesson.At("subgroup") = JsonValue::MakeNumber(-1);
            lesson.At("is_class_hour") = JsonValue::MakeBool(true);
            lesson.At("half") = JsonValue::MakeNumber(choice.pair ? 2 : 0);
            const auto time = ClassTime(choice.pair);
            lesson.At("start_time") = JsonValue::MakeString(MinuteToString(time.from_minute));
            lesson.At("end_time") = JsonValue::MakeString(MinuteToString(time.to_minute));
            for (auto& group : candidate.At("groups").array_value) {
                if (JsonInt(group, "group_index", -1) != choice.group) continue;
                for (auto& day : group.At("days").array_value) {
                    if (JsonString(day, "date_iso", "") != DateToIso(date)) continue;
                    auto& slots = day.At("slots").array_value;
                    auto it = std::find_if(slots.begin(), slots.end(), [&](const auto& s) { return JsonInt(s, "slot", -1) == choice.pair; });
                    if (it == slots.end()) { slots.push_back(JsonValue::MakeObject()); it = slots.end() - 1; }
                    it->At("slot") = JsonValue::MakeNumber(choice.pair);
                    it->At("time") = JsonValue::MakeString("Классный час (" + IntervalToString(time) + ")");
                    it->At("text") = JsonValue::MakeString("Классный час — " + teacher->name + ", " + room->name + ", " + (room->campus == 0 ? "Лесная" : "Кривоусова"));
                    it->At("lessons") = JsonValue::MakeArray();
                    it->At("lessons").array_value.push_back(lesson);
                    std::sort(slots.begin(), slots.end(), [](const auto& a, const auto& b) { return JsonInt(a, "slot", -1) < JsonInt(b, "slot", -1); });
                }
            }
        }
    }
    const auto issues = ValidateClassHours(data, candidate);
    if (!issues.array_value.empty()) { error = ToJson(issues); return false; }
    schedule = std::move(candidate);
    return true;
}

JsonValue ValidateClassHours(const ScheduleInputData& data, const JsonValue& schedule) {
    auto issues = JsonValue::MakeArray();
    const auto academic = Academic(data, schedule);
    std::map<std::pair<int, Date>, int> counts;
    std::map<std::tuple<Date, int, int>, std::vector<int>> teacher_rooms, room_teachers;
    std::map<std::tuple<Date, int, int>, int> room_students;
    for (const auto& output_group : schedule.At("groups").array_value) {
        const auto* group = Find(data.groups, JsonInt(output_group, "group_index", -1));
        if (!group) continue;
        for (const auto& day : output_group.At("days").array_value) {
            Date date{};
            if (!ParseDateIso(JsonString(day, "date_iso", ""), date)) continue;
            for (const auto& slot : day.At("slots").array_value) for (const auto& item : slot.At("lessons").array_value) {
                if (!JsonBool(item, "is_class_hour", false)) continue;
                const int pair = JsonInt(slot, "slot", -1);
                const int teacher_id = JsonInt(item, "teacher_id", -1);
                const int room_id = JsonInt(item, "room_id", -1);
                counts[{group->id, date}]++;
                const auto* teacher = Find(data.teachers, teacher_id);
                const auto* room = Find(data.rooms, room_id);
                if (pair >= 0 && pair <= 7 && !ContinuousStudentDay(academic, *group, date, pair))
                    Error(issues, "class_hour_student_window", "Между классным часом и занятиями подгруппы есть окно", group->id, date);
                if (!Required(data, *group, date) || !teacher || teacher_id != group->curator_teacher || !room ||
                    !Fits(data, academic, *group, *teacher, *room, date, pair)) {
                    Error(issues, "class_hour_invalid_assignment", "Недопустимый куратор, кабинет или время классного часа", group->id, date);
                    continue;
                }
                const auto time = ClassTime(pair);
                if (JsonInt(item, "id", -1) >= 0 || JsonInt(item, "half", -1) != (pair ? 2 : 0) ||
                    JsonString(item, "start_time", "") != MinuteToString(time.from_minute) ||
                    JsonString(item, "end_time", "") != MinuteToString(time.to_minute))
                    Error(issues, "class_hour_interval_invalid", "Классный час должен иметь точные часы и не учитываться как учебная пара", group->id, date);
                teacher_rooms[{date, pair, teacher_id}].push_back(room_id);
                room_teachers[{date, pair, room_id}].push_back(teacher_id);
                room_students[{date, pair, room_id}] += std::max(0, group->size);
            }
        }
    }
    for (const auto& date : GenerateSchoolDays(data.start_date, data.end_date)) for (const auto& group : data.groups)
        if (Required(data, group, date) && counts[{group.id, date}] != 1)
            Error(issues, "class_hour_count_mismatch", "У группы должен быть ровно один классный час в понедельник", group.id, date);
    for (const auto& entry : counts) if (entry.second > 1)
        Error(issues, "duplicate_class_hour", "Повторный классный час группы", entry.first.first, entry.first.second);
    for (const auto& entry : teacher_rooms)
        if (entry.second.size() > 2 || std::set<int>(entry.second.begin(), entry.second.end()).size() > 1)
            Error(issues, "class_hour_teacher_conflict", "Куратор может вести не более двух групп одновременно в одном кабинете", -1, std::get<0>(entry.first));
    for (const auto& entry : room_teachers) {
        const auto* room = Find(data.rooms, std::get<2>(entry.first));
        if (std::set<int>(entry.second.begin(), entry.second.end()).size() > 1 ||
            (room && room->capacity > 0 && room_students[entry.first] > room->capacity))
            Error(issues, "class_hour_room_conflict", "Конфликт занятий или превышение вместимости кабинета", -1, std::get<0>(entry.first));
    }
    return issues;
}

bool FinalizeSchedule(const ScheduleInputData& data, const std::string& output_dir, std::string& error, bool draft_semester_risk) {
    const std::filesystem::path directory(output_dir);
    std::ifstream stream(directory / "schedule_all.json", std::ios::binary);
    std::ostringstream buffer; buffer << stream.rdbuf();
    auto parsed = ParseJson(buffer.str());
    if (!stream || !parsed.ok) { error = "Не записан корректный итоговый JSON"; return false; }
    if (!PlanClassHours(data, parsed.value, error)) return false;
    ScheduleValidationOptions options;
    options.draft_semester_risk = draft_semester_risk;
    if (draft_semester_risk) {
        parsed.value.At("status") = JsonValue::MakeString("draft_semester_risk");
        parsed.value.At("semester_readout") = data.semester_readout_report;
    }
    auto validation = ValidateScheduleJson(data, g_solver_config, parsed.value, options);
    if (!Write(directory / "strict_audit.json", validation.report)) { error = "Не записан отчёт строгой проверки"; return false; }
    if (!validation.ok) { error = "Итоговая независимая проверка не пройдена; см. strict_audit.json"; return false; }
    if (!Write(directory / "schedule_all.json", parsed.value)) { error = "Ошибка записи общего расписания"; return false; }
    for (const auto& group : parsed.value.At("groups").array_value)
        if (!Write(directory / "groups" / ("group_" + std::to_string(JsonInt(group, "group_index", -1)) + ".json"), group)) {
            error = "Ошибка записи расписания группы"; return false;
        }
    if (!WriteDerivedText(data, parsed.value, directory)) { error = "Не удалось обновить текстовые выгрузки из проверенного JSON"; return false; }
    return true;
}
}
