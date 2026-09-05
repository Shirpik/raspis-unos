#include <cstdlib>
#include <iostream>
#include <string>

#include "schedule_validator.h"
#include "class_hours.h"
#include "semester_plan.h"
#include "ortools/sat/cp_model_solver.h"

namespace {

using timetable::Date;
using timetable::GroupData;
using timetable::JsonValue;
using timetable::Lesson;
using timetable::RoomData;
using timetable::RuntimeSolverConfig;
using timetable::ScheduleInputData;
using timetable::ScheduleValidationOptions;
using timetable::TeacherData;

void Require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << "\n";
        std::exit(1);
    }
}

void AllowAll(timetable::WorkSchedule& schedule) {
    schedule.has_period = false;
    for (auto& day : schedule.days) {
        day.enabled = true;
        day.start_slot = 1;
        day.end_slot = 7;
        for (int slot = 1; slot <= 7; ++slot) day.slots.insert(slot);
    }
}

ScheduleInputData Input() {
    ScheduleInputData data;
    data.start_date = {2026, 9, 3};
    data.end_date = {2026, 9, 3};

    GroupData group;
    group.id = 0;
    group.name = "TEST";
    group.parts = 2;
    AllowAll(group.work_schedule);
    data.groups.push_back(group);

    TeacherData teacher;
    teacher.id = 0;
    teacher.name = "Teacher";
    teacher.allowed_campuses = {0};
    teacher.campus_priority = {0};
    AllowAll(teacher.work_schedule);
    data.teachers.push_back(teacher);

    RoomData room;
    room.id = 0;
    room.name = "101";
    room.campus = 0;
    room.active = true;
    AllowAll(room.work_schedule);
    data.rooms.push_back(room);

    Lesson lesson;
    lesson.id = 0;
    lesson.uid = "lesson-0";
    lesson.group = 0;
    lesson.subgroup = -1;
    lesson.teacher = 0;
    lesson.total_slots = 1;
    lesson.name = "Математика";
    lesson.subject_id = 0;
    lesson.is_lab = false;
    lesson.is_block = false;
    lesson.is_pp = false;
    lesson.allowed_campuses = {timetable::LESNAYA};
    data.lessons.push_back(lesson);
    return data;
}

JsonValue RenderedLesson(int room = 0) {
    JsonValue lesson = JsonValue::MakeObject();
    lesson.At("id") = JsonValue::MakeNumber(0);
    lesson.At("teacher_id") = JsonValue::MakeNumber(0);
    lesson.At("room_id") = room < 0 ? JsonValue::MakeNull() : JsonValue::MakeNumber(room);
    return lesson;
}

JsonValue Schedule(bool duplicate = false, int room = 0) {
    JsonValue root = JsonValue::MakeObject();
    JsonValue groups = JsonValue::MakeArray();
    JsonValue group = JsonValue::MakeObject();
    group.At("group_index") = JsonValue::MakeNumber(0);
    group.At("group_name") = JsonValue::MakeString("TEST");
    JsonValue days = JsonValue::MakeArray();
    JsonValue day = JsonValue::MakeObject();
    day.At("date_iso") = JsonValue::MakeString("2026-09-03");
    JsonValue slots = JsonValue::MakeArray();
    for (int pair = 1; pair <= 7; ++pair) {
        JsonValue slot = JsonValue::MakeObject();
        slot.At("slot") = JsonValue::MakeNumber(pair);
        JsonValue lessons = JsonValue::MakeArray();
        if (pair == 1) {
            lessons.array_value.push_back(RenderedLesson(room));
            if (duplicate) lessons.array_value.push_back(RenderedLesson(room));
        }
        slot.At("lessons") = lessons;
        slots.array_value.push_back(slot);
    }
    day.At("slots") = slots;
    days.array_value.push_back(day);
    group.At("days") = days;
    groups.array_value.push_back(group);
    root.At("groups") = groups;
    return root;
}

bool HasCode(const JsonValue& report, const std::string& code) {
    for (const JsonValue& issue : report.At("issues").array_value) {
        if (timetable::JsonString(issue, "code", "") == code) return true;
    }
    return false;
}

}  // namespace

int main() {
    RuntimeSolverConfig config = timetable::DefaultSolverConfig();
    config.min_student_pairs_per_study_day = 1;
    config.max_student_pairs_per_day = 7;
    config.hard_min_study_days_per_week = false;
    config.hard_no_student_windows = true;

    ScheduleInputData input = Input();
    auto valid = timetable::ValidateScheduleJson(input, config, Schedule());
    Require(valid.ok, "valid schedule must pass");
    Require(valid.scheduled_occurrences == 1, "quota count must be exact");

    timetable::Date parsed_date{};
    Require(!timetable::ParseDateIso("2026-02-29", parsed_date), "invalid leap day must fail");
    Require(!timetable::ParseDateIso("2026-04-31", parsed_date), "April has no 31st day");
    Require(timetable::ParseDateIso("2028-02-29", parsed_date), "valid leap day must pass");
    auto targeted = input;
    targeted.teachers[0].date_minimum_pairs[{2026, 9, 3}] = 2;
    auto date_load = timetable::ValidateScheduleJson(targeted, config, Schedule());
    Require(!date_load.ok && HasCode(date_load.report, "teacher_date_minimum_not_met"),
        "date minimum cannot silently decrease to generated result");

    auto monday = input;
    monday.start_date = monday.end_date = {2026, 9, 7};
    monday.require_class_hours = true;
    monday.groups[0].curator_teacher = 0;
    monday.rooms[0].class_hour_open = true;
    for (int id : {1, 2}) { auto group = monday.groups[0]; group.id = id; group.name = "G" + std::to_string(id); monday.groups.push_back(group); }
    auto monday_schedule = Schedule();
    auto& original_group = monday_schedule.At("groups").array_value[0];
    original_group.At("days").array_value[0].At("date_iso") = JsonValue::MakeString("2026-09-07");
    auto empty_group = original_group;
    empty_group.At("days").array_value[0].At("slots") = JsonValue::MakeArray();
    for (int id : {1, 2}) {
        empty_group.At("group_index") = JsonValue::MakeNumber(id);
        monday_schedule.At("groups").array_value.push_back(empty_group);
    }
    std::string class_error;
    Require(timetable::PlanClassHours(monday, monday_schedule, class_error), "three groups must allocate into two distinct class-hour sessions: " + class_error);
    Require(timetable::ValidateClassHours(monday, monday_schedule).array_value.empty(), "planned class hours must validate");
    int zeros = 0, late = 0;
    for (const auto& group : monday_schedule.At("groups").array_value)
        for (const auto& slot : group.At("days").array_value[0].At("slots").array_value)
            for (const auto& lesson : slot.At("lessons").array_value)
                if (timetable::JsonBool(lesson, "is_class_hour", false)) {
                    if (timetable::JsonInt(slot, "slot", -1) == 0) zeros++; else late++;
                    Require(timetable::JsonInt(lesson, "room_id", -1) == 0, "class hour needs a real room");
                }
    Require(zeros == 2 && late == 1, "two groups simultaneously, third in second half of later pair");
    auto absent_curator = monday;
    absent_curator.teacher_unavailable[0].push_back({monday.start_date, monday.end_date});
    Require(!timetable::PlanClassHours(absent_curator, monday_schedule, class_error), "class hour cannot bypass teacher absence");
    Require(timetable::ValidateClassHours(absent_curator, monday_schedule).array_value.size() >= 3, "independent validator catches absent curator");
    absent_curator.teachers[0].class_hour_available_dates.insert(monday.start_date);
    Require(timetable::ValidateClassHours(absent_curator, monday_schedule).array_value.empty(), "explicit class-hour-only exception permits curator without changing ordinary absence");
    Require(!absent_curator.teacher_unavailable[0].empty(), "class-hour exception must preserve ordinary absence");
    auto cadet = monday;
    cadet.rooms[0].class_hour_zero_blocked = true;
    Require(!timetable::ValidateClassHours(cadet, monday_schedule).array_value.empty(), "occupied zero-slot room must fail validation");

    auto continuous_input = monday;
    continuous_input.groups.resize(1);
    auto continuous_schedule = Schedule();
    continuous_schedule.At("groups").array_value[0].At("days").array_value[0].At("date_iso") = JsonValue::MakeString("2026-09-07");
    Require(timetable::PlanClassHours(continuous_input, continuous_schedule, class_error), "zero class hour immediately followed by pair one is valid");
    auto gap_schedule = continuous_schedule;
    auto& gap_slots = gap_schedule.At("groups").array_value[0].At("days").array_value[0].At("slots").array_value;
    // Slots are sorted 0..7 after finalization: leave zero but move pair one to three.
    gap_slots[3].At("lessons") = gap_slots[1].At("lessons");
    gap_slots[1].At("lessons") = JsonValue::MakeArray();
    auto gap_validation = timetable::ValidateScheduleJson(continuous_input, config, gap_schedule);
    Require(!gap_validation.ok && HasCode(gap_validation.report, "class_hour_student_window"), "zero lesson must participate in student window validation");
    auto late_schedule = gap_schedule;
    auto& late_slots = late_schedule.At("groups").array_value[0].At("days").array_value[0].At("slots").array_value;
    late_slots[2].At("lessons") = late_slots[0].At("lessons");
    late_slots[0].At("lessons") = JsonValue::MakeArray();
    auto& late_class = late_slots[2].At("lessons").array_value[0];
    late_class.At("half") = JsonValue::MakeNumber(2);
    late_class.At("start_time") = JsonValue::MakeString("11:35");
    late_class.At("end_time") = JsonValue::MakeString("12:15");
    Require(timetable::ValidateClassHours(continuous_input, late_schedule).array_value.empty(), "late class followed by the very next pair must pass");
    late_slots[4].At("lessons") = late_slots[3].At("lessons");
    late_slots[3].At("lessons") = JsonValue::MakeArray();
    Require(!timetable::ValidateClassHours(continuous_input, late_schedule).array_value.empty(), "late class must not be separated from regular pairs");

    const auto model_with_zero = [&](int first_pair) {
        using namespace operations_research::sat;
        auto model_input = continuous_input;
        model_input.teachers[0].work_schedule.date_slot_overrides[monday.start_date] = {1};
        CpModelBuilder model;
        std::vector<std::vector<std::vector<BoolVar>>> pb(1, std::vector<std::vector<BoolVar>>(2));
        std::vector<std::vector<BoolVar>> tb(1);
        for (int slot=0; slot<7; ++slot) {
            auto t = model.NewBoolVar(); model.AddEquality(t,0); tb[0].push_back(t);
            for (int part=0; part<2; ++part) { auto p = model.NewBoolVar(); model.AddEquality(p,slot+1==first_pair); pb[0][part].push_back(p); }
        }
        std::vector<std::vector<IntVar>> gc{{model.NewIntVar(operations_research::Domain(0,1))}}, tc{{model.NewIntVar(operations_research::Domain(0,1))}};
        timetable::AddClassHourTimeConstraints(model, model_input, {monday.start_date},pb,tb,gc,tc);
        return Solve(model.Build()).status();
    };
    Require(model_with_zero(1) == operations_research::sat::CpSolverStatus::OPTIMAL, "integrated model permits zero then pair one");
    Require(model_with_zero(2) == operations_research::sat::CpSolverStatus::INFEASIBLE, "integrated model forbids zero then pair two without pair one");

    auto semester_root = timetable::ParseJson(R"({"settings":{"semester_start_date":"2026-09-07","semester_weeks":16},
      "teachers":[{"id":0,"desired_load_rules":[{"group_ids":[0],"course_year":2,"deadline":"2026-09-12"}]}],
      "groups":[{"id":0,"name":"TEST-2202"}],
      "lessons":[{"id":0,"teacher":0,"group":0,"total_hours":12,"plan_active":false,"generation_active":false}],
      "teaching_ledger":[{"lesson_id":0,"date":"2026-09-05","slot":1,"hours":2,"status":"confirmed"}]})").value;
    auto semester_data = input;
    semester_data.start_date = {2026, 9, 7}; semester_data.end_date = {2026, 9, 12};
    timetable::PrepareSemesterRequirements(semester_root, semester_data);
    Require(semester_data.load_requirements.size() == 1 && semester_data.load_requirements[0].minimum_pairs == 5,
        "deadline must use full curriculum minus confirmed hours, regardless of period-active flags");
    Require(HasCode(semester_data.semester_readout_report, "semester_quota_shortfall"), "low selected quota must block accelerated readout");
    semester_root.At("teaching_ledger").array_value.push_back(semester_root.At("teaching_ledger").array_value[0]);
    timetable::PrepareSemesterRequirements(semester_root, semester_data);
    Require(HasCode(semester_data.semester_readout_report, "ledger_record_invalid"), "duplicate confirmed occurrence must fail");
    Require(semester_data.load_requirements[0].minimum_pairs == 5, "duplicate ledger must never double credit hours");
    const auto hours = timetable::BuildHoursReport(semester_root, "nonexistent-schedule-test.json");
    Require(timetable::JsonInt(hours.At("lessons").array_value[0], "planned_hours", -1) == 12,
        "API full curriculum cannot disappear when period plan_active is false");
    semester_data.teachers[0].scheduling_active = false;
    timetable::PrepareSemesterRequirements(semester_root, semester_data);
    Require(semester_data.load_requirements.empty(), "paused teacher is not assigned a readout requirement");
    Require(timetable::JsonInt(semester_data.semester_readout_report.At("deferred_teachers").array_value[0], "remaining_hours", -1) == 10,
        "paused teacher hours are preserved, not credited or deleted");
    auto draft_schedule = Schedule();
    draft_schedule.At("status") = JsonValue::MakeString("draft_semester_risk");
    Require(!timetable::ValidateScheduleJson(input, config, draft_schedule).ok, "draft cannot pass normal publication validation");

    auto conflict = timetable::ValidateScheduleJson(input, config, Schedule(true));
    Require(!conflict.ok, "duplicate event must fail");
    Require(HasCode(conflict.report, "teacher_conflict"), "teacher conflict must be reported");
    Require(HasCode(conflict.report, "student_conflict"), "student conflict must be reported");
    Require(HasCode(conflict.report, "lesson_quota_mismatch"), "quota excess must be reported");

    auto no_room = timetable::ValidateScheduleJson(input, config, Schedule(false, -1));
    Require(!no_room.ok && HasCode(no_room.report, "room_unassigned"),
            "missing room must fail closed");

    input.teachers[0].allowed_campuses = {1};
    auto campus = timetable::ValidateScheduleJson(input, config, Schedule());
    Require(!campus.ok && HasCode(campus.report, "teacher_campus_mismatch"),
            "teacher campus restriction must be enforced");

    std::cout << "schedule validator regression passed\n";
    return 0;
}
