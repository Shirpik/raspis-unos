#pragma once

#include <vector>

#include "types.h"

namespace timetable {

// Итоговая нагрузка считается в академических часах. Обычная пара и ПП
// занимают 2 часа, один старт УП — 4 часа (два модельных pair-slot).
struct ScheduleLoadSummary {
    int planned_hours = 0;
    int scheduled_hours = 0;
    // Суммы считаются по каждой строке нагрузки, поэтому недобор одного урока
    // не может скрыться за лишним размещением другого урока.
    int missing_hours = 0;
    int excess_hours = 0;
    int mismatched_lessons = 0;
    int remaining_hours = 0;

    bool complete() const {
        return missing_hours == 0 && excess_hours == 0 &&
            mismatched_lessons == 0 && remaining_hours == 0 &&
            scheduled_hours == planned_hours;
    }
};

ScheduleLoadSummary ComputeScheduleLoadSummary(
    const std::vector<Lesson>& lessons,
    const std::vector<std::vector<int>>& x_values
);

// Возвращает недельную нагрузку каждой физической части группы в занятых
// pair-slot. Параллельные занятия разных подгрупп не складываются в нагрузку
// одной части; общее занятие добавляется каждой существующей части.
std::vector<std::vector<int>> ComputeGroupPartWeeklyOccupiedPairs(
    const std::vector<Lesson>& lessons,
    const std::vector<int>& quotas,
    int group_count,
    const std::vector<int>& group_part_count
);

int EffectiveStudentDailyMinimum(int requested_minimum, int weekly_part_pairs);

// Число обязательных учебных дней не может требовать больше pair-slot, чем
// реально выделено этой физической части группы на неделю.
int EffectiveStudentStudyDays(
    int requested_days,
    int available_days,
    int daily_minimum,
    int weekly_part_pairs
);

// 0 означает «индивидуальный лимит не задан». Значения выше физического
// числа pair-slot дня эквивалентны полному семипарному дню.
int EffectiveTeacherMaxPairsPerDay(int configured_limit);

}  // namespace timetable
