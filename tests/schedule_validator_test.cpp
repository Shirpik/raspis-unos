#include <cstdlib>
#include <iostream>
#include <string>

#include "schedule_validator.h"
#include "class_hours.h"
#include "semester_plan.h"
#include "date_utils.h"
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
    auto repeated = input;
    repeated.lessons[0].subgroup = 0;
    repeated.lessons[0].total_slots = 4;
    auto repeated_schedule = Schedule();
    auto& repeated_slots = repeated_schedule.At("groups").array_value[0].At("days").array_value[0].At("slots").array_value;
    for (int slot = 1; slot < 4; ++slot) repeated_slots[slot].At("lessons").array_value.push_back(RenderedLesson());
    Require(HasCode(timetable::ValidateScheduleJson(repeated, config, repeated_schedule).report, "physical_subgroup_same_subject_daily_limit"),
        "four same-subject subgroup pairs need an explicit exception");
    repeated.teachers[0].date_same_subject_maximum[{2026,9,3}] = 4;
    Require(!HasCode(timetable::ValidateScheduleJson(repeated, config, repeated_schedule).report, "physical_subgroup_same_subject_daily_limit"),
        "matching teacher/date exception allows four subject pairs");
    repeated.teachers[0].date_same_subject_maximum.clear();
    repeated.teachers[0].date_same_subject_maximum[{2026,9,4}] = 4;
    Require(HasCode(timetable::ValidateScheduleJson(repeated, config, repeated_schedule).report, "physical_subgroup_same_subject_daily_limit"),
        "exception on another date must not relax this date");

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
    const auto& semester_row = semester_data.semester_readout_report.At("rows").array_value[0];
    Require(!HasCode(semester_data.semester_readout_report, "semester_quota_shortfall") &&
            timetable::JsonInt(semester_row, "selected_period_pairs", -1) >=
                timetable::JsonInt(semester_row, "minimum_period_pairs", 999) &&
            timetable::JsonInt(semester_row, "automatic_quota_added_pairs", 0) > 0,
        "automatic quota calculation must raise the current period to the required readout pace");
    semester_root.At("teaching_ledger").array_value.push_back(semester_root.At("teaching_ledger").array_value[0]);
    timetable::PrepareSemesterRequirements(semester_root, semester_data);
    Require(HasCode(semester_data.semester_readout_report, "ledger_record_invalid"), "duplicate confirmed occurrence must fail");
    Require(semester_data.load_requirements[0].minimum_pairs == 5, "duplicate ledger must never double credit hours");
    const auto hours = timetable::BuildHoursReport(semester_root, "nonexistent-schedule-test.json");
    Require(timetable::JsonInt(hours.At("lessons").array_value[0], "planned_hours", -1) == 12,
        "API full curriculum cannot disappear when period plan_active is false");
    Require(timetable::JsonInt(hours.At("lessons").array_value[0], "scheduled_hours", -1) == 2 &&
            timetable::JsonInt(hours.At("lessons").array_value[0], "projected_hours", -1) == 0,
        "confirmed ledger hours and generated projection must be separated");

    auto completed_lesson_root = timetable::ParseJson(R"({
      "settings":{"start_date":"2026-09-09","end_date":"2026-09-11","semester_start_date":"2026-09-02","semester_end_date":"2026-09-11","automatic_period_quotas":true},
      "teachers":[{"id":0,"name":"Teacher"}],
      "groups":[{"id":0,"name":"GROUP-1605"}],
      "rooms":[],
      "lessons":[
        {"id":10,"teacher":0,"group":0,"name":"Theory","total_hours":10,"total_slots":2,"generation_active":true,"curriculum_active":true},
        {"id":11,"teacher":0,"group":0,"name":"Practice","total_hours":10,"total_slots":2,"generation_active":true,"curriculum_active":true}
      ],
      "teaching_ledger":[
        {"id":0,"lesson_id":10,"date":"2026-09-02","slot":1,"hours":2,"status":"confirmed"},
        {"id":1,"lesson_id":10,"date":"2026-09-03","slot":1,"hours":2,"status":"confirmed"},
        {"id":2,"lesson_id":10,"date":"2026-09-04","slot":1,"hours":2,"status":"confirmed"},
        {"id":3,"lesson_id":10,"date":"2026-09-07","slot":1,"hours":2,"status":"confirmed"},
        {"id":4,"lesson_id":10,"date":"2026-09-08","slot":1,"hours":2,"status":"confirmed"}
      ]})").value;
    timetable::ScheduleInputData completed_lesson_data;
    std::string completed_lesson_error;
    Require(timetable::LoadScheduleInputDataFromRoot(
                completed_lesson_root, completed_lesson_data, completed_lesson_error, false),
        "automatic quotas must load completed-hours fixture");
    Require(std::none_of(completed_lesson_data.lessons.begin(), completed_lesson_data.lessons.end(),
                [](const auto& lesson) { return lesson.id == 10; }),
        "a lesson whose confirmed hours equal its curriculum must be removed from the solver");
    Require(std::any_of(completed_lesson_data.lessons.begin(), completed_lesson_data.lessons.end(),
                [](const auto& lesson) { return lesson.id == 11 && lesson.total_slots > 0; }),
        "unfinished lessons must remain available to the solver");

    auto practice_root = completed_lesson_root;
    practice_root.At("settings").At("semester_end_date") = JsonValue::MakeString("2026-12-19");
    practice_root.At("settings").At("start_date") = JsonValue::MakeString("2026-09-12");
    practice_root.At("settings").At("end_date") = JsonValue::MakeString("2026-09-12");
    practice_root.At("groups").array_value[0].At("practice_periods") = timetable::ParseJson(
        R"([{"from":"2026-09-14","to":"2026-10-04"},{"from":"2027-01-11","to":"2027-02-07"}])").value;
    timetable::ScheduleInputData practice_data;
    const int saved_student_max = timetable::g_solver_config.max_student_pairs_per_day;
    timetable::g_solver_config.max_student_pairs_per_day = 4;
    Require(timetable::LoadScheduleInputDataFromRoot(practice_root, practice_data, completed_lesson_error, false),
        "practice calendar fixture must load");
    const auto& practice_group = practice_data.semester_readout_report.At("groups").array_value[0];
    Require(timetable::JsonString(practice_group, "deadline", "") == "2026-09-13",
        "earliest practice in the semester must advance the readout deadline");
    Require(timetable::JsonInt(practice_group, "capacity_hours_per_subgroup", -1) == 8,
        "one available Saturday has at most four student pairs");
    Require(HasCode(practice_data.semester_readout_report, "group_deadline_shortfall"),
        "ten remaining hours cannot fit into eight available hours");
    Require(!timetable::IsAvailable({2026,9,14}, 0, practice_data.unavailable) &&
            !timetable::IsAvailable({2026,10,4}, 0, practice_data.unavailable) &&
            timetable::IsAvailable({2026,10,5}, 0, practice_data.unavailable),
        "practice must block both interval endpoints and allow return afterwards");
    practice_root.At("groups").array_value[0].At("practice_periods").array_value.erase(
        practice_root.At("groups").array_value[0].At("practice_periods").array_value.begin());
    Require(timetable::LoadScheduleInputDataFromRoot(practice_root, practice_data, completed_lesson_error, false),
        "next semester practice must be accepted");
    Require(timetable::JsonString(practice_data.semester_readout_report.At("groups").array_value[0], "deadline", "") == "2026-12-19",
        "next semester practice must not move the current semester deadline");
    auto calendar_root = practice_root;
    calendar_root.At("settings").At("solver_config").At("max_student_pairs_per_day") = JsonValue::MakeNumber(4);
    calendar_root.At("groups").array_value[0].At("academic_calendar") = timetable::ParseJson(R"([
      {"from":"2026-09-07","to":"2026-09-13","theory_hours":24,"up_hours":12},
      {"from":"2026-09-14","to":"2026-09-20","vacation":true},
      {"from":"2026-09-21","to":"2026-09-27","up_hours":36}
    ])").value;
    timetable::ScheduleInputData calendar_data;
    Require(timetable::LoadScheduleInputDataFromRoot(calendar_root, calendar_data, completed_lesson_error, false),
        "weekly theory, vacation and UP calendar must load");
    timetable::g_solver_config.max_student_pairs_per_day = 7;
    Require(timetable::GroupTeachingCapacity(calendar_data, calendar_data.groups[0], {2026,9,12}, {2026,9,12}) == 4,
        "read-only API forecast must use database daily limit even when global runtime differs");
    timetable::g_solver_config.max_student_pairs_per_day = 4;
    Require(!calendar_data.lessons.empty() && timetable::LessonCalendarAllows(calendar_data.lessons[0], {2026,9,12}) &&
        !timetable::LessonCalendarAllows(calendar_data.lessons[0], {2026,9,14}) &&
        !timetable::LessonCalendarAllows(calendar_data.lessons[0], {2026,9,21}) &&
        !timetable::LessonCalendarAllows(calendar_data.lessons[0], {2026,9,28}),
        "ordinary lessons allowed in mixed theory/UP weeks, forbidden in vacation, UP-only or uncovered dates");
    auto deadline_lesson = calendar_data.lessons[0];
    deadline_lesson.teaching_windows = {{{2026,9,7},{2026,9,13}}};
    Require(!timetable::LessonCalendarAllows(deadline_lesson, {2026,10,5}),
        "ordinary hours cannot be pushed beyond an early deadline after return from PP");
    deadline_lesson.is_block = true;
    Require(timetable::LessonCalendarAllows(deadline_lesson, {2026,9,21}), "ordinary calendar must not block UP itself");
    calendar_root.At("groups").array_value[0].At("academic_calendar").array_value[1].At("from") = JsonValue::MakeString("2026-09-07");
    Require(!timetable::LoadScheduleInputDataFromRoot(calendar_root, calendar_data, completed_lesson_error, false),
        "overlapping calendar weeks must be rejected");
    auto manual_completed_root = completed_lesson_root;
    manual_completed_root.At("settings").At("automatic_period_quotas") = JsonValue::MakeBool(false);
    Require(timetable::LoadScheduleInputDataFromRoot(manual_completed_root, calendar_data, completed_lesson_error, false) &&
        std::none_of(calendar_data.lessons.begin(), calendar_data.lessons.end(), [](const auto& l) { return l.id == 10; }),
        "manual quotas also cannot resurrect already credited theory");
    auto blocked_calendar = input;
    blocked_calendar.lessons[0].calendar_restricted = true;
    Require(HasCode(timetable::ValidateScheduleJson(blocked_calendar, config, Schedule()).report, "group_teaching_deadline"),
        "independent validator rejects ordinary lessons outside teaching windows");
    practice_root.At("groups").array_value[0].At("practice_periods").array_value[0].At("to") = JsonValue::MakeString("2026-01-01");
    Require(!timetable::LoadScheduleInputDataFromRoot(practice_root, practice_data, completed_lesson_error, false),
        "reversed practice dates must reject loading");
    timetable::g_solver_config.max_student_pairs_per_day = saved_student_max;

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
