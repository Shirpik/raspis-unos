#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "scheduler_rules.h"

namespace {

using timetable::Campus;
using timetable::Lesson;

Lesson MakeLesson(int id, int group, int subgroup, int total_slots, bool is_block = false) {
    Lesson lesson{};
    lesson.id = id;
    lesson.uid = "lesson-" + std::to_string(id);
    lesson.group = group;
    lesson.subgroup = subgroup;
    lesson.teacher = id;
    lesson.total_slots = total_slots;
    lesson.name = "lesson-" + std::to_string(id);
    lesson.subject_id = id;
    lesson.is_lab = false;
    lesson.is_block = is_block;
    lesson.is_pp = false;
    lesson.allowed_campuses = {static_cast<Campus>(0)};
    return lesson;
}

bool Expect(bool condition, const std::string& message) {
    if (condition) return true;
    std::cerr << "FAILED: " << message << "\n";
    return false;
}

}  // namespace

int main() {
    bool ok = true;

    // Две подгруппы могут иметь по три параллельные пары: физическая нагрузка
    // каждой части равна 3, хотя в schedule будет 6 событий.
    std::vector<Lesson> parallel = {
        MakeLesson(0, 0, 0, 3),
        MakeLesson(1, 0, 1, 3),
    };
    const auto parallel_load = timetable::ComputeGroupPartWeeklyOccupiedPairs(
        parallel, {3, 3}, 1, {2});
    ok &= Expect(parallel_load.size() == 1, "one group must be present");
    ok &= Expect(parallel_load[0][0] == 3, "part 1 must count its own three pairs, not six");
    ok &= Expect(parallel_load[0][1] == 3, "part 2 must count its own three pairs, not six");
    ok &= Expect(timetable::EffectiveStudentDailyMinimum(3, parallel_load[0][0]) == 3,
        "part 1 daily minimum must be derived from its own weekly load");
    ok &= Expect(timetable::EffectiveStudentDailyMinimum(3, parallel_load[0][1]) == 3,
        "part 2 daily minimum must be derived from its own weekly load");
    ok &= Expect(timetable::EffectiveStudentStudyDays(3, 3, 2, 5) == 2,
        "five weekly pairs at two per active day cannot require three study days");
    ok &= Expect(timetable::EffectiveStudentStudyDays(3, 3, 2, 6) == 3,
        "six weekly pairs can require three two-pair study days");
    ok &= Expect(timetable::EffectiveStudentStudyDays(3, 2, 2, 6) == 2,
        "study-day requirement must respect available dates");

    // Общее занятие потребляет слот каждой существующей части; УП — два слота
    // только той части, которой принадлежит.
    std::vector<Lesson> mixed = {
        MakeLesson(2, 0, -1, 2),
        MakeLesson(3, 0, 0, 2, true),
    };
    const auto mixed_load = timetable::ComputeGroupPartWeeklyOccupiedPairs(
        mixed, {2, 2}, 1, {2});
    ok &= Expect(mixed_load[0][0] == 6, "whole 2 + two UP starts * 2 must give part 1 load 6");
    ok &= Expect(mixed_load[0][1] == 2, "whole lesson must add two pairs to part 2 only");

    ok &= Expect(timetable::EffectiveTeacherMaxPairsPerDay(0) == 0,
        "zero teacher limit must mean not configured");
    ok &= Expect(timetable::EffectiveTeacherMaxPairsPerDay(7) == 7,
        "teacher hard limit must allow all seven physical pair slots");
    ok &= Expect(timetable::EffectiveTeacherMaxPairsPerDay(9) == 7,
        "teacher limit above the physical day must normalize to seven");

    Lesson ordinary = MakeLesson(4, 0, -1, 3);
    Lesson block = MakeLesson(5, 0, -1, 2, true);
    Lesson pp = MakeLesson(6, 0, -1, 4);
    pp.is_pp = true;
    std::vector<Lesson> load_lessons = {ordinary, block, pp};
    std::vector<std::vector<int>> incomplete_x = {
        {1, 1, 1},
        {1, 1, 1, 1},
        {1, 1, 1},
    };
    const auto incomplete = timetable::ComputeScheduleLoadSummary(load_lessons, incomplete_x);
    ok &= Expect(incomplete.planned_hours == 22, "planned hours must use four hours per UP start");
    ok &= Expect(incomplete.scheduled_hours == 20, "scheduled hours must count occupied pair slots");
    ok &= Expect(incomplete.remaining_hours == 2 && !incomplete.complete(),
        "truncated PP must be reported as an incomplete schedule");

    incomplete_x[2].push_back(1);
    const auto complete = timetable::ComputeScheduleLoadSummary(load_lessons, incomplete_x);
    ok &= Expect(complete.remaining_hours == 0 && complete.complete(),
        "full scheduled load must pass the final invariant");

    // Равные агрегатные суммы не означают полную вычитку: недобор одной строки
    // нельзя компенсировать лишним размещением другой строки.
    std::vector<Lesson> cancelling_lessons = {
        MakeLesson(7, 0, -1, 1),
        MakeLesson(8, 0, -1, 1),
    };
    const auto cancelling = timetable::ComputeScheduleLoadSummary(
        cancelling_lessons, {{}, {1, 1}});
    ok &= Expect(cancelling.planned_hours == cancelling.scheduled_hours,
        "cancelling regression must have equal aggregate totals");
    ok &= Expect(cancelling.missing_hours == 2 && cancelling.excess_hours == 2 &&
            cancelling.mismatched_lessons == 2 && !cancelling.complete(),
        "per-lesson invariant must reject compensated missing/excess load");

    if (!ok) return 1;
    std::cout << "scheduler_rules_cpp_regression: passed\n";
    return 0;
}
