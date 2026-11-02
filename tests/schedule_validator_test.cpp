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

int main(int argc, char** argv) {
    RuntimeSolverConfig config = timetable::DefaultSolverConfig();
    config.min_student_pairs_per_study_day = 1;
    config.max_student_pairs_per_day = 7;
    config.hard_min_study_days_per_week = false;
    config.hard_no_student_windows = true;

    {
    // UP's displayed row is not its bell interval. On Saturday ordinary
    // pair 3 ends before afternoon UP, while on Thursday it overlaps UP.
    auto up_interval_input = Input();
    auto second_group = up_interval_input.groups[0]; second_group.id = 1;
    up_interval_input.groups.push_back(second_group);
    auto second_lesson = up_interval_input.lessons[0]; second_lesson.id = 1; second_lesson.group = 1;
    up_interval_input.lessons.push_back(second_lesson);
    up_interval_input.teachers.resize(1);
    up_interval_input.lessons[1].teacher = 0;
    up_interval_input.lessons[1].is_block = true;
    up_interval_input.lessons[1].name = "УП.01";
    auto up_interval_schedule = Schedule();
    auto second_row = up_interval_schedule.At("groups").array_value[0];
    second_row.At("group_index") = JsonValue::MakeNumber(1);
    second_row.At("days").array_value[0].At("slots").array_value[0].At("lessons").array_value[0].At("id") = JsonValue::MakeNumber(1);
    up_interval_schedule.At("groups").array_value.push_back(second_row);
    for (auto& row : up_interval_schedule.At("groups").array_value) {
        auto& day = row.At("days").array_value[0];
        day.At("date_iso") = JsonValue::MakeString("2026-10-24");
        auto& slots = day.At("slots").array_value;
        slots[2].At("lessons") = slots[0].At("lessons");
        slots[0].At("lessons") = JsonValue::MakeArray();
        slots[2].At("lessons").array_value[0].At("teacher_id") = JsonValue::MakeNumber(0);
    }
    up_interval_input.start_date = up_interval_input.end_date = {2026,10,24};
    Require(!HasCode(timetable::ValidateScheduleJson(up_interval_input, config, up_interval_schedule).report, "teacher_conflict"),
        "Saturday pair 3 and afternoon UP are disjoint despite sharing a display row");
    up_interval_input.start_date = up_interval_input.end_date = {2026,10,22};
    for (auto& row : up_interval_schedule.At("groups").array_value)
        row.At("days").array_value[0].At("date_iso") = JsonValue::MakeString("2026-10-22");
    Require(HasCode(timetable::ValidateScheduleJson(up_interval_input, config, up_interval_schedule).report, "teacher_conflict"),
        "weekday pair 3 overlaps afternoon UP and must fail");
    auto& up_slots = up_interval_schedule.At("groups").array_value[1].At("days").array_value[0].At("slots").array_value;
    up_slots[0].At("lessons") = up_slots[2].At("lessons");
    up_slots[2].At("lessons") = JsonValue::MakeArray();
    Require(HasCode(timetable::ValidateScheduleJson(up_interval_input, config, up_interval_schedule).report, "teacher_conflict"),
        "morning UP also conflicts with weekday pair 3 in a different display row");

    }
    if (argc == 2 && std::string(argv[1]) == "--teacher-intervals") {
        std::cout << "teacher interval regression passed\n"; return 0;
    }
    ScheduleInputData input = Input();
    auto valid = timetable::ValidateScheduleJson(input, config, Schedule());
    Require(valid.ok, "valid schedule must pass");

    auto strict_daily_config = config;
    strict_daily_config.min_student_pairs_per_study_day = 2;
    strict_daily_config.allow_single_pair_day_fallback = false;
    auto strict_daily = timetable::ValidateScheduleJson(input, strict_daily_config, Schedule());
    Require(!strict_daily.ok && HasCode(strict_daily.report, "student_daily_minimum_fallback"),
        "daily minimum must remain strict for a full-period validation");
    ScheduleValidationOptions scoped_options;
    scoped_options.allow_daily_minimum_fallback = true;
    scoped_options.require_weekly_study_days = false;
    scoped_options.require_selected_study_days = true;
    auto scoped_daily = timetable::ValidateScheduleJson(
        input, strict_daily_config, Schedule(), scoped_options);
    Require(scoped_daily.ok &&
            HasCode(scoped_daily.report, "student_daily_minimum_fallback") &&
            !HasCode(scoped_daily.report, "student_daily_minimum_missing_day"),
        "scoped generation reports the fallback without rejecting the selected day");
    auto spare_day = Schedule();
    auto empty_day = spare_day.At("groups").array_value[0].At("days").array_value[0];
    empty_day.At("date_iso") = JsonValue::MakeString("2026-09-04");
    empty_day.At("slots") = JsonValue::MakeArray();
    spare_day.At("groups").array_value[0].At("days").array_value.push_back(empty_day);
    Require(!HasCode(timetable::ValidateScheduleJson(input, config, spare_day).report,
                     "student_daily_minimum_missing_day"),
        "full-period quotas may leave an unused day without inventing extra curriculum hours");
    Require(!HasCode(timetable::ValidateScheduleJson(input, config, spare_day, scoped_options).report,
                     "student_daily_minimum_missing_day"),
        "scoped fallback permits a selected day with no ordinary pair");

    auto up_input = Input();
    up_input.lessons[0].is_block = true;
    up_input.lessons[0].subgroup = -1;
    auto one_slot_up = timetable::ValidateScheduleJson(up_input, config, Schedule());
    Require(one_slot_up.ok, "one UP occurrence on pair 1 must occupy exactly one displayed slot");

    auto adjacent_up_input = up_input;
    adjacent_up_input.lessons[0].subgroup = 0;
    Lesson afternoon_up = adjacent_up_input.lessons[0];
    afternoon_up.id = 1;
    afternoon_up.uid = "lesson-1";
    afternoon_up.subgroup = 1;
    adjacent_up_input.lessons.push_back(afternoon_up);
    auto adjacent_up_schedule = Schedule();
    JsonValue afternoon_up_rendered = RenderedLesson();
    afternoon_up_rendered.At("id") = JsonValue::MakeNumber(1);
    adjacent_up_schedule.At("groups").array_value[0].At("days").array_value[0]
        .At("slots").array_value[2].At("lessons").array_value.push_back(afternoon_up_rendered);
    auto adjacent_up_result = timetable::ValidateScheduleJson(
        adjacent_up_input, config, adjacent_up_schedule);
    Require(adjacent_up_result.ok && !HasCode(adjacent_up_result.report, "teacher_window"),
        "adjacent morning and afternoon UP shifts for one teacher are continuous, not a window");

    auto wrong_up_slot = Schedule();
    auto& wrong_slots = wrong_up_slot.At("groups").array_value[0]
        .At("days").array_value[0].At("slots").array_value;
    wrong_slots[1].At("lessons") = wrong_slots[0].At("lessons");
    wrong_slots[0].At("lessons") = JsonValue::MakeArray();
    auto wrong_up_result = timetable::ValidateScheduleJson(up_input, config, wrong_up_slot);
    Require(!wrong_up_result.ok && HasCode(wrong_up_result.report, "up_display_slot"),
        "UP outside pair 1 or pair 3 must be rejected");

    auto after_up_input = up_input;
    Lesson after_up = after_up_input.lessons[0];
    after_up.id = 1;
    after_up.uid = "lesson-1";
    after_up.name = "After UP";
    after_up.subject_id = 1;
    after_up.is_block = false;
    after_up_input.lessons.push_back(after_up);
    auto after_up_schedule = Schedule();
    JsonValue after_up_rendered = RenderedLesson();
    after_up_rendered.At("id") = JsonValue::MakeNumber(1);
    after_up_schedule.At("groups").array_value[0].At("days").array_value[0]
        .At("slots").array_value[1].At("lessons").array_value.push_back(after_up_rendered);
    auto after_up_result = timetable::ValidateScheduleJson(after_up_input, config, after_up_schedule);
    Require(!after_up_result.ok && HasCode(after_up_result.report, "lesson_after_up"),
        "a subgroup must not have another lesson after its UP");
    auto before_up_schedule = Schedule();
    before_up_schedule.At("groups").array_value[0].At("days").array_value[0]
        .At("slots").array_value[0].At("lessons") = JsonValue::MakeArray();
    before_up_schedule.At("groups").array_value[0].At("days").array_value[0]
        .At("slots").array_value[2].At("lessons").array_value.push_back(RenderedLesson());
    before_up_schedule.At("groups").array_value[0].At("days").array_value[0]
        .At("slots").array_value[0].At("lessons").array_value.push_back(after_up_rendered);
    auto before_up_result = timetable::ValidateScheduleJson(after_up_input, config, before_up_schedule);
    Require(!before_up_result.ok && HasCode(before_up_result.report, "lesson_after_up"),
        "a subgroup must not have another lesson before its UP");

    auto transferred_up_input = up_input;
    transferred_up_input.teachers[0].name = "вынесена на ПП";
    transferred_up_input.lessons[0].is_block = false;
    transferred_up_input.lessons[0].name = "УП.02";
    auto transferred_up = timetable::ValidateScheduleJson(transferred_up_input, config, Schedule());
    Require(!transferred_up.ok && HasCode(transferred_up.report, "up_transferred_to_pp"),
        "UP transferred to PP must be rejected even when its is_block flag is malformed");
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
    monday.groups[0].class_hour_room_required = true;
    monday.groups[0].class_hour_room = 0;
    monday.rooms[0].class_hour_open = true;
    for (int id : {1, 2}) {
        auto group = monday.groups[0]; group.id = id; group.name = "G" + std::to_string(id); monday.groups.push_back(group);
        auto lesson = monday.lessons[0]; lesson.id = id; lesson.uid = "lesson-" + std::to_string(id); lesson.group = id; monday.lessons.push_back(lesson);
    }
    // Two groups start with pair one; the third starts with pair two so its
    // curator can hold a separate first-pair class hour. Academic teachers and
    // rooms are distinct from the shared curator/class-hour room.
    for (int id = 0; id < 3; ++id) {
        auto teacher = monday.teachers[0]; teacher.id = id + 1; monday.teachers.push_back(teacher);
        auto room = monday.rooms[0]; room.id = id + 1; room.class_hour_open = false;
        monday.rooms.push_back(room);
        monday.lessons[id].teacher = id + 1;
    }
    auto monday_schedule = Schedule();
    auto& original_group = monday_schedule.At("groups").array_value[0];
    original_group.At("days").array_value[0].At("date_iso") = JsonValue::MakeString("2026-09-07");
    original_group.At("days").array_value[0].At("slots").array_value[0]
        .At("lessons").array_value[0].At("teacher_id") = JsonValue::MakeNumber(1);
    original_group.At("days").array_value[0].At("slots").array_value[0]
        .At("lessons").array_value[0].At("room_id") = JsonValue::MakeNumber(1);
    auto empty_group = original_group;
    for (int id : {1, 2}) {
        empty_group.At("group_index") = JsonValue::MakeNumber(id);
        empty_group.At("days").array_value[0].At("slots").array_value[0]
            .At("lessons").array_value[0].At("id") = JsonValue::MakeNumber(id);
        auto& slots = empty_group.At("days").array_value[0].At("slots").array_value;
        slots[0].At("lessons").array_value[0].At("teacher_id") = JsonValue::MakeNumber(id + 1);
        slots[0].At("lessons").array_value[0].At("room_id") = JsonValue::MakeNumber(id + 1);
        if (id == 2) {
            slots[1].At("lessons") = slots[0].At("lessons");
            slots[0].At("lessons") = JsonValue::MakeArray();
        }
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
    Require(zeros == 2 && late == 1, "two groups at zero, third immediately before its second academic pair");
    auto absent_curator = monday;
    absent_curator.teacher_unavailable[0].push_back({monday.start_date, monday.end_date});
    Require(timetable::PlanClassHours(absent_curator, monday_schedule, class_error), "class hour keeps the assigned curator during absence");
    Require(timetable::ValidateClassHours(absent_curator, monday_schedule).array_value.empty(), "class-hour validation permits an absent curator");
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
    Require(!HasCode(gap_validation.report, "student_window") && !HasCode(gap_validation.report, "class_hour_student_window"), "class hour must not create a student window before teaching begins");
    auto late_schedule = gap_schedule;
    auto& late_slots = late_schedule.At("groups").array_value[0].At("days").array_value[0].At("slots").array_value;
    late_slots[2].At("lessons") = late_slots[0].At("lessons");
    late_slots[0].At("lessons") = JsonValue::MakeArray();
    auto& late_class = late_slots[2].At("lessons").array_value[0];
    late_class.At("half") = JsonValue::MakeNumber(2);
    late_class.At("start_time") = JsonValue::MakeString("11:35");
    late_class.At("end_time") = JsonValue::MakeString("12:15");
    Require(timetable::ValidateClassHours(continuous_input, late_schedule).array_value.empty(), "late class followed by the very next pair must pass");
    auto delayed_schedule = late_schedule;
    auto& delayed_slots = delayed_schedule.At("groups").array_value[0].At("days").array_value[0].At("slots").array_value;
    delayed_slots[4].At("lessons") = delayed_slots[3].At("lessons");
    delayed_slots[3].At("lessons") = JsonValue::MakeArray();
    auto delayed_issues = JsonValue::MakeObject();
    delayed_issues.At("issues") = timetable::ValidateClassHours(continuous_input, delayed_schedule);
    Require(HasCode(delayed_issues, "class_hour_post_window"), "the first academic activity must follow the class hour immediately");
    auto before_schedule = late_schedule;
    auto& before_slots = before_schedule.At("groups").array_value[0].At("days").array_value[0].At("slots").array_value;
    before_slots[1].At("lessons") = before_slots[3].At("lessons");
    before_slots[3].At("lessons") = JsonValue::MakeArray();
    auto before_issues = JsonValue::MakeObject();
    before_issues.At("issues") = timetable::ValidateClassHours(continuous_input, before_schedule);
    Require(HasCode(before_issues, "teaching_before_class_hour"), "no subgroup may have teaching before its class hour");

    for (int pair : {1,5}) {
        auto allowed_schedule = late_schedule;
        auto& slots = allowed_schedule.At("groups").array_value[0].At("days").array_value[0].At("slots").array_value;
        const auto academic = slots[3].At("lessons");
        auto class_lesson = slots[2].At("lessons").array_value[0];
        for (auto& slot : slots) slot.At("lessons") = JsonValue::MakeArray();
        const auto interval = timetable::PairSlotInterval(1,pair-1);
        class_lesson.At("start_time") = JsonValue::MakeString(timetable::MinuteToString(interval.to_minute-40));
        class_lesson.At("end_time") = JsonValue::MakeString(timetable::MinuteToString(interval.to_minute));
        slots[pair].At("lessons").array_value.push_back(class_lesson);
        slots[pair+1].At("lessons") = academic;
        Require(timetable::ValidateClassHours(continuous_input, allowed_schedule).array_value.empty(), "first and fifth pair second halves are permitted");
    }

    const auto model_with_class = [&](int class_pair, int first_pair, int second_pair = -1) {
        using namespace operations_research::sat;
        auto model_input = continuous_input;
        model_input.groups[0].class_hour_fixed_pair = class_pair;
        CpModelBuilder model;
        std::vector<std::vector<std::vector<BoolVar>>> pb(1, std::vector<std::vector<BoolVar>>(2));
        std::vector<std::vector<BoolVar>> tb(1);
        for (int slot=0; slot<7; ++slot) {
            auto t = model.NewBoolVar(); model.AddEquality(t,0); tb[0].push_back(t);
            for (int part=0; part<2; ++part) { auto p = model.NewBoolVar(); model.AddEquality(p,slot+1==((part==1 && second_pair>0)?second_pair:first_pair)); pb[0][part].push_back(p); }
        }
        std::vector<std::vector<IntVar>> gc{{model.NewIntVar(operations_research::Domain(0,1))}}, tc{{model.NewIntVar(operations_research::Domain(0,1))}};
        timetable::AddClassHourTimeConstraints(model, model_input, {monday.start_date},pb,tb,gc,tc);
        return Solve(model.Build()).status();
    };
    Require(model_with_class(0,1) == operations_research::sat::CpSolverStatus::OPTIMAL, "integrated model permits zero then pair one");
    Require(model_with_class(0,2,3) == operations_research::sat::CpSolverStatus::INFEASIBLE, "at least one subgroup must start immediately after zero class hour");
    Require(model_with_class(1,2,3) == operations_research::sat::CpSolverStatus::OPTIMAL, "subgroups may start independently after first-pair class hour");
    Require(model_with_class(5,6,7) == operations_research::sat::CpSolverStatus::OPTIMAL, "fifth-pair class hour permits later subgroup starts");
    Require(model_with_class(3,2,4) == operations_research::sat::CpSolverStatus::INFEASIBLE, "teaching in either subgroup before class hour is forbidden");
    Require(model_with_class(2,2) == operations_research::sat::CpSolverStatus::INFEASIBLE, "academic pair cannot overlap class hour");

    const auto model_with_up = [&](int class_pair, int up_start, bool selected = true) {
        using namespace operations_research::sat;
        auto model_input = continuous_input;
        model_input.groups[0].class_hour_fixed_pair = class_pair;
        model_input.lessons[0].is_block = true;
        CpModelBuilder model;
        std::vector<std::vector<std::vector<BoolVar>>> pb(1, std::vector<std::vector<BoolVar>>(2));
        std::vector<std::vector<BoolVar>> tb(1), placements(1);
        for (int slot=0; slot<7; ++slot) {
            auto t=model.NewBoolVar(); model.AddEquality(t,0); tb[0].push_back(t);
            auto x=model.NewBoolVar(); model.AddEquality(x,selected && slot==up_start); placements[0].push_back(x);
            for(int part=0;part<2;++part){auto p=model.NewBoolVar();model.AddEquality(p,0);pb[0][part].push_back(p);}
        }
        std::vector<std::vector<IntVar>> gc{{model.NewIntVar(operations_research::Domain(0,1))}}, tc{{model.NewIntVar(operations_research::Domain(0,1))}};
        timetable::AddClassHourTimeConstraints(model,model_input,{monday.start_date},pb,tb,gc,tc,&model_input.lessons,&placements);
        return Solve(model.Build()).status();
    };
    Require(model_with_up(1,2) == operations_research::sat::CpSolverStatus::OPTIMAL, "afternoon UP may follow first-pair class hour");
    Require(model_with_up(3,2) == operations_research::sat::CpSolverStatus::INFEASIBLE, "UP may not begin before class hour ends, even at its displayed pair");
    Require(model_with_up(0,0) == operations_research::sat::CpSolverStatus::INFEASIBLE, "08:30 UP cannot overlap zero class hour ending at 08:55");
    Require(model_with_up(0,0,false) == operations_research::sat::CpSolverStatus::INFEASIBLE, "an unused UP cannot satisfy the required immediate activity");

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

    auto accounting_root = timetable::ParseJson(R"({
      "settings":{"start_date":"2026-09-07","end_date":"2026-09-12","semester_start_date":"2026-09-07","semester_end_date":"2026-10-04","accounting_as_of_date":"2026-09-14"},
      "groups":[{"id":0,"name":"TEST","parts":1,"accounting_order":4}],"teachers":[{"id":0,"name":"Teacher"}],
      "lessons":[{"id":10,"teacher":0,"group":0,"name":"Theory","total_hours":12,"total_slots":6,"workload_order":7}],
      "teaching_ledger":[
        {"lesson_id":10,"date":"2026-09-07","slot":1,"hours":2,"status":"confirmed"},
        {"lesson_id":10,"date":"2026-09-07","slot":1,"hours":1,"status":"planned"},
        {"lesson_id":10,"date":"2026-09-15","slot":2,"hours":2,"status":"planned"},
        {"lesson_id":10,"date":"2026-09-15","slot":2,"hours":2,"status":"planned"},
        {"lesson_id":10,"date":"2026-09-07","slot":3,"hours":2,"status":"confirmed","is_class_hour":true}
      ]})").value;
    const auto accounting = timetable::BuildHoursReport(accounting_root, "nonexistent-schedule-test.json");
    const auto& accounting_lesson = accounting.At("lessons").array_value[0];
    Require(timetable::JsonInt(accounting_lesson, "workload_order", -1) == 7 &&
            timetable::JsonInt(accounting_lesson, "group_order", -1) == 4 &&
            timetable::JsonInt(accounting.At("groups").array_value[0], "accounting_order", -1) == 4,
        "hours report preserves group-tab and lesson-row order from the workload workbook");
    Require(timetable::JsonInt(accounting_lesson, "scheduled_hours", -1) == 2 &&
            timetable::JsonInt(accounting_lesson, "reserved_hours", -1) == 2 &&
            timetable::JsonInt(accounting_lesson, "remaining_hours", -1) == 10 &&
            timetable::JsonInt(accounting_lesson, "available_hours", -1) == 8,
        "journal reservations deduplicate, confirmed wins, and class hours never consume curriculum");
    const auto& accounting_teacher = accounting.At("teachers").array_value[0];
    Require(timetable::JsonInt(accounting_teacher, "required_weekly_hours", -1) == 5 &&
            timetable::JsonInt(accounting_teacher, "remaining_weeks", -1) == 2,
        "weekly readout pace uses remaining hours and only complete future semester weeks");

    auto merged_teacher_root = timetable::ParseJson(R"({
      "settings":{"semester_start_date":"2026-09-01","semester_end_date":"2026-09-19","accounting_as_of_date":"2026-09-14"},
      "groups":[{"id":0,"name":"TEST","parts":1}],
      "teachers":[{"id":1,"name":"Former name","merged_into_teacher_id":2},{"id":2,"name":"Current name"}],
      "lessons":[{"id":10,"teacher":1,"group":0,"name":"Theory","total_hours":12,"total_slots":6}],
      "teaching_ledger":[
        {"lesson_id":10,"date":"2026-09-02","slot":1,"hours":2,"status":"confirmed","actual_teacher":1},
        {"lesson_id":10,"date":"2026-09-02","slot":2,"hours":2,"status":"confirmed","actual_teacher":1},
        {"lesson_id":10,"date":"2026-09-02","slot":3,"hours":2,"status":"confirmed","actual_teacher":1}
      ]})").value;
    const auto merged_teacher_accounting = timetable::BuildHoursReport(
        merged_teacher_root, "nonexistent-schedule-test.json");
    Require(merged_teacher_accounting.At("teachers").array_value.size() == 1 &&
            timetable::JsonInt(merged_teacher_accounting.At("teachers").array_value[0], "teacher_id", -1) == 2 &&
            timetable::JsonInt(merged_teacher_accounting.At("teachers").array_value[0], "planned_hours", -1) == 12 &&
            timetable::JsonInt(merged_teacher_accounting.At("teachers").array_value[0], "credited_hours", -1) == 6,
        "merged teacher hours and confirmed credits are consolidated under the current teacher");
    const auto& merged_occurrences = merged_teacher_accounting.At("teachers").array_value[0].At("credited_occurrences").array_value;
    Require(merged_occurrences.size() == 3 &&
            timetable::JsonString(merged_occurrences[0], "date", "") == "2026-09-02" &&
            timetable::JsonInt(merged_occurrences[0], "actual_teacher_id", -1) == 2,
        "merged teacher confirmed dates and identities are reported under the current teacher");
    Require(timetable::JsonInt(merged_teacher_accounting.At("lessons").array_value[0], "teacher_id", -1) == 2,
        "lesson accounting displays the canonical teacher identity after a merge");

    auto pp_accounting_root = timetable::ParseJson(R"({
      "settings":{"start_date":"2026-09-07","end_date":"2026-09-12","semester_start_date":"2026-09-07","semester_end_date":"2026-09-19","accounting_as_of_date":"2026-09-07"},
      "groups":[{"id":0,"name":"TEST","parts":1}],"teachers":[{"id":0,"name":"Teacher"}],
      "lessons":[
        {"id":20,"teacher":0,"group":0,"source_index":"1","name":"Theory","total_hours":12,"total_slots":6},
        {"id":21,"teacher":0,"group":0,"source_index":"ПП.04","name":"ПП.04 Производственная практика","total_hours":144,"total_slots":72}
      ],
      "teaching_ledger":[
        {"lesson_id":20,"date":"2026-09-07","slot":1,"hours":2,"status":"confirmed"},
        {"lesson_id":21,"date":"2026-09-07","slot":2,"hours":6,"status":"confirmed"},
        {"lesson_id":21,"date":"2026-09-14","slot":2,"hours":6,"status":"planned"}
      ]})").value;
    const auto pp_accounting = timetable::BuildHoursReport(pp_accounting_root, "nonexistent-schedule-test.json");
    Require(pp_accounting.At("lessons").array_value.size() == 1 &&
            timetable::JsonInt(pp_accounting.At("lessons").array_value[0], "lesson_id", -1) == 20,
        "production-practice lessons are omitted from hour-accounting details");
    Require(timetable::JsonInt(pp_accounting.At("groups").array_value[0], "planned_hours", -1) == 12 &&
            timetable::JsonInt(pp_accounting.At("groups").array_value[0], "scheduled_hours", -1) == 2 &&
            timetable::JsonInt(pp_accounting.At("groups").array_value[0], "projected_hours", -1) == 0,
        "production-practice planned, confirmed, and reserved hours do not affect group accounting");
    Require(timetable::JsonInt(pp_accounting.At("teachers").array_value[0], "planned_hours", -1) == 12 &&
            timetable::JsonInt(pp_accounting.At("teachers").array_value[0], "scheduled_hours", -1) == 2,
        "production-practice hours do not affect teacher accounting");
    Require(pp_accounting_root.At("lessons").array_value.size() == 2,
        "excluding production practice from accounting does not remove source curriculum data");

    auto transferred_pp_root = timetable::ParseJson(R"({
      "settings":{"semester_start_date":"2026-09-07","semester_end_date":"2026-09-19","accounting_as_of_date":"2026-09-14"},
      "groups":[{"id":0,"name":"TEST","parts":1}],
      "teachers":[{"id":0,"name":"Teacher"},{"id":88,"name":"вынесена на ПП"}],
      "lessons":[
        {"id":30,"teacher":0,"group":0,"name":"Theory","total_hours":12,"total_slots":6},
        {"id":31,"teacher":88,"group":0,"source_index":"УП.02","name":"УП.02","total_hours":120,"total_slots":60}
      ],
      "teaching_ledger":[
        {"lesson_id":30,"date":"2026-09-07","slot":1,"hours":2,"status":"confirmed"},
        {"lesson_id":31,"date":"2026-09-07","slot":2,"hours":6,"status":"confirmed"},
        {"lesson_id":31,"date":"2026-09-15","slot":2,"hours":6,"status":"planned"}
      ]})").value;
    const auto transferred_pp = timetable::BuildHoursReport(transferred_pp_root, "nonexistent-schedule-test.json");
    Require(transferred_pp.At("lessons").array_value.size() == 1 &&
            timetable::JsonInt(transferred_pp.At("lessons").array_value[0], "lesson_id", -1) == 30,
        "UP rows assigned to the transferred-to-PP service teacher are absent from accounting details");
    Require(transferred_pp.At("teachers").array_value.size() == 1 &&
            timetable::JsonInt(transferred_pp.At("teachers").array_value[0], "teacher_id", -1) == 0 &&
            timetable::JsonInt(transferred_pp.At("teachers").array_value[0], "planned_hours", -1) == 12,
        "the transferred-to-PP service teacher and its hours are absent from teacher accounting");
    Require(timetable::JsonInt(transferred_pp.At("groups").array_value[0], "planned_hours", -1) == 12 &&
            timetable::JsonInt(transferred_pp.At("groups").array_value[0], "scheduled_hours", -1) == 2 &&
            timetable::JsonInt(transferred_pp.At("groups").array_value[0], "projected_hours", -1) == 0,
        "transferred-to-PP planned, confirmed, and reserved hours are excluded from group accounting");
    Require(transferred_pp_root.At("lessons").array_value.size() == 2,
        "transferred-to-PP curriculum remains in source data");

    accounting_root.At("settings").At("accounting_as_of_date") = JsonValue::MakeString("2026-10-05");
    const auto expired_accounting = timetable::BuildHoursReport(accounting_root, "nonexistent-schedule-test.json");
    Require(expired_accounting.At("teachers").array_value[0].At("required_weekly_hours").IsNull() &&
            timetable::JsonInt(expired_accounting.At("teachers").array_value[0], "remaining_weeks", -1) == 0,
        "an expired semester must not invent another available week");

    auto completed_lesson_root = timetable::ParseJson(R"({
      "settings":{"start_date":"2026-09-09","end_date":"2026-09-11","semester_start_date":"2026-09-02","semester_end_date":"2026-09-11","automatic_period_quotas":true},
      "teachers":[{"id":0,"name":"Teacher"}],
      "groups":[{"id":0,"name":"GROUP-1605"}],
      "rooms":[],
      "lessons":[
        {"id":10,"teacher":0,"group":0,"name":"Theory","total_hours":10,"total_slots":2,"generation_active":true,"curriculum_active":true},
        {"id":11,"teacher":0,"group":0,"name":"Practice","total_hours":10,"total_slots":2,"generation_active":true,"curriculum_active":true},
        {"id":12,"teacher":0,"group":0,"name":"УП.02","total_hours":6,"total_slots":1,"is_block":false,"generation_active":true,"curriculum_active":true}
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
    Require(std::any_of(completed_lesson_data.lessons.begin(), completed_lesson_data.lessons.end(),
                [](const auto& lesson) { return lesson.id == 12 && lesson.is_block; }),
        "UP name must restore block semantics when imported is_block is malformed");
    auto completed_up_root = completed_lesson_root;
    auto replacement_root = completed_lesson_root;
    replacement_root.At("teachers").array_value[0].At("scheduling_active") = JsonValue::MakeBool(false);
    replacement_root.At("teachers").array_value.push_back(timetable::ParseJson(
        R"({"id":1,"name":"Substitute","scheduling_active":true,"max_pairs_per_day":5})").value);
    replacement_root.At("lessons").array_value[1].At("scheduling_teacher") = JsonValue::MakeNumber(1);
    ScheduleInputData replacement_input;
    Require(timetable::LoadScheduleInputDataFromRoot(replacement_root, replacement_input, completed_lesson_error, false),
        "replacement workload must load while its owner is on sick leave");
    Require(std::any_of(replacement_input.lessons.begin(), replacement_input.lessons.end(),
                [](const auto& lesson) { return lesson.id == 11 && lesson.teacher == 1; }),
        "solver must assign replacement workload to the actual teacher");
    Require(timetable::JsonInt(replacement_root.At("lessons").array_value[1], "teacher", -1) == 0,
        "loading a replacement must not rewrite the source workload teacher");
    completed_up_root.At("teaching_ledger").array_value.push_back(timetable::ParseJson(
        R"({"id":5,"lesson_id":12,"date":"2026-09-08","slot":2,"hours":6,"status":"confirmed"})").value);
    Require(timetable::LoadScheduleInputDataFromRoot(completed_up_root, completed_lesson_data, completed_lesson_error, false) &&
            std::none_of(completed_lesson_data.lessons.begin(), completed_lesson_data.lessons.end(),
                [](const auto& lesson) { return lesson.id == 12; }),
        "a credited six-hour UP block cannot be assigned again");

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
    auto dated_load = input;
    dated_load.teachers[0].date_minimum_pairs[dated_load.start_date] = 2;
    Require(HasCode(timetable::ValidateScheduleJson(dated_load, config, Schedule()).report,
                    "teacher_date_minimum_not_met"),
            "active teacher's date minimum remains mandatory");
    dated_load.teachers[0].scheduling_active = false;
    Require(!HasCode(timetable::ValidateScheduleJson(dated_load, config, Schedule()).report,
                     "teacher_date_minimum_not_met"),
            "paused teacher's saved date minimum does not block generation");
    dated_load.teachers[0].scheduling_active = true;
    for (auto& day : dated_load.teachers[0].work_schedule.days) {
        day.enabled = false;
        day.slots.clear();
    }
    ScheduleValidationOptions dated_scope_options;
    dated_scope_options.allow_daily_minimum_fallback = true;
    Require(!HasCode(timetable::ValidateScheduleJson(
                         dated_load, config, Schedule(), dated_scope_options).report,
                     "teacher_date_minimum_not_met"),
            "saved date minimum is clamped to the teacher's actual date availability");

    auto dated_up = input;
    dated_up.lessons[0].is_block = true;
    dated_up.lessons[0].name = "УП.01";
    dated_up.teachers[0].date_minimum_pairs[dated_up.start_date] = 3;
    Require(!HasCode(timetable::ValidateScheduleJson(dated_up, config, Schedule()).report,
                     "teacher_date_minimum_not_met"),
            "date minimum counts all three occupied pair slots of a rendered UP shift");
    dated_up.teachers[0].date_minimum_pairs[dated_up.start_date] = 4;
    Require(HasCode(timetable::ValidateScheduleJson(dated_up, config, Schedule()).report,
                    "teacher_date_minimum_not_met"),
            "UP shift alone does not satisfy a four-pair date target");

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

    auto reservation_root = timetable::ParseJson(R"({"teaching_ledger":[
        {"lesson_id":10,"date":"2026-09-14","slot":1,"hours":2,"status":"confirmed"},
        {"lesson_id":10,"date":"2026-09-14","slot":1,"hours":2,"status":"planned"},
        {"lesson_id":10,"date":"2026-09-17","slot":2,"hours":2,"status":"planned"},
        {"lesson_id":10,"date":"2026-09-17","slot":2,"hours":2,"status":"planned"},
        {"lesson_id":10,"date":"2026-09-18","slot":3,"hours":2,"status":"planned"},
        {"lesson_id":10,"date":"2026-09-17","slot":0,"hours":2,"status":"confirmed"}
    ]})").value;
    auto balance = timetable::ReadTeachingBalances(reservation_root, {2026,9,18});
    Require(balance.confirmed[10] == 2 && balance.reserved[10] == 2,
        "planned prior dates reserve hours once; regeneration dates and class hours do not consume balance");
    balance = timetable::ReadTeachingBalances(reservation_root, {2026,9,20}, {2026,9,17}, {2026,9,18});
    Require(balance.confirmed[10] == 2 && balance.reserved[10] == 0,
        "scoped replacement releases planned reservations inside the selected range without deleting facts");
    GroupData course_one;
    course_one.semester_end_date = "2026-12-26";
    Require(timetable::GroupTeachingDeadline(course_one, {2026,9,1}, {2026,12,19}) == Date{2026,12,26},
        "first-year semester extends beyond senior default");
    course_one.teaching_deadline = "2026-12-05";
    Require(timetable::GroupTeachingDeadline(course_one, {2026,9,1}, {2026,12,19}) == Date{2026,12,5},
        "manual earlier deadline takes precedence");
    GroupData before_up;
    before_up.semester_end_date = "2026-12-19";
    before_up.academic_calendar = {{{2026,10,26},{2026,11,1},30,6,0,0,false},
        {{2026,11,2},{2026,11,8},0,36,0,0,false}, {{2026,11,9},{2026,12,20},0,0,36,0,false}};
    Require(timetable::GroupTeachingDeadline(before_up, {2026,9,1}, {2026,12,19}) == Date{2026,11,1},
        "ordinary lessons must finish before final full-time UP and PP interval");
    auto window_input = Input();
    window_input.lessons[0].total_slots = 2;
    auto window_schedule = Schedule();
    window_schedule.At("groups").array_value[0].At("days").array_value[0].At("slots").array_value[2].At("lessons").array_value.push_back(RenderedLesson());
    auto window_config = config;
    window_config.hard_no_teacher_windows = true;
    window_config.hard_no_student_windows = false;
    const auto hard_window = timetable::ValidateScheduleJson(window_input, window_config, window_schedule);
    Require(!hard_window.ok && HasCode(hard_window.report, "teacher_window"), "teacher windows remain hard by default");
    window_input.teachers[0].allowed_window_dates.insert(window_input.start_date);
    const auto allowed_window = timetable::ValidateScheduleJson(window_input, window_config, window_schedule);
    Require(allowed_window.ok && HasCode(allowed_window.report, "teacher_window"), "explicit teacher date exception is a visible warning");
    window_input.teachers[0].allowed_window_dates = {{2026, 9, 4}};
    Require(!timetable::ValidateScheduleJson(window_input, window_config, window_schedule).ok, "window exception cannot relax another date");
    auto calendar_input = Input();
    auto& calendar_rule = calendar_input.group_day_rules[{0, calendar_input.start_date}];
    calendar_rule.common_slot = 2;
    Require(HasCode(timetable::ValidateScheduleJson(calendar_input, config, Schedule()).report, "common_slot_missing"),
        "calendar rule rejects a missing whole-group second pair");
    calendar_rule.common_slot = 1;
    calendar_rule.minimum_pairs = 2;
    Require(HasCode(timetable::ValidateScheduleJson(calendar_input, config, Schedule()).report, "date_minimum_pairs"),
        "calendar minimum is validated independently of solver success");
    calendar_input.teachers[0].allowed_campuses = {1};
    calendar_input.teachers[0].date_campuses[calendar_input.start_date] = 0;
    Require(!HasCode(timetable::ValidateScheduleJson(calendar_input, config, Schedule()).report, "teacher_campus_mismatch"),
        "explicit teacher calendar override applies only to its date");

    auto venue_input = Input();
    auto other_group = venue_input.groups[0]; other_group.id = 1;
    venue_input.groups.push_back(other_group);
    auto other_teacher = venue_input.teachers[0]; other_teacher.id = 1;
    venue_input.teachers.push_back(other_teacher);
    auto other_lesson = venue_input.lessons[0]; other_lesson.id = 1; other_lesson.group = 1; other_lesson.teacher = 1; other_lesson.uid = "lesson-1";
    venue_input.lessons.push_back(other_lesson);
    auto venue_schedule = Schedule();
    auto other_row = venue_schedule.At("groups").array_value[0];
    other_row.At("group_index") = JsonValue::MakeNumber(1);
    auto& other_rendered = other_row.At("days").array_value[0].At("slots").array_value[0].At("lessons").array_value[0];
    other_rendered.At("id") = JsonValue::MakeNumber(1);
    other_rendered.At("teacher_id") = JsonValue::MakeNumber(1);
    venue_schedule.At("groups").array_value.push_back(other_row);
    Require(HasCode(timetable::ValidateScheduleJson(venue_input, config, venue_schedule).report, "room_conflict"),
        "ordinary room rejects two simultaneous independent classes");
    venue_input.rooms[0].parallel_classes = 2;
    Require(!HasCode(timetable::ValidateScheduleJson(venue_input, config, venue_schedule).report, "room_conflict"),
        "venue with two classrooms allows two simultaneous independent classes");
    std::cout << "schedule validator regression passed\n";
    return 0;
}
