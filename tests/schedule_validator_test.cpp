#include <cstdlib>
#include <iostream>
#include <string>

#include "schedule_validator.h"

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
