#include "scheduler_rules.h"

#include <algorithm>

#include "config.h"

namespace timetable {

ScheduleLoadSummary ComputeScheduleLoadSummary(
    const std::vector<Lesson>& lessons,
    const std::vector<std::vector<int>>& x_values
) {
    ScheduleLoadSummary result;
    for (int l = 0; l < static_cast<int>(lessons.size()); ++l) {
        const int planned_pairs = lessons[l].total_slots * (lessons[l].is_block ? 2 : 1);
        int scheduled_pairs = 0;
        if (l < static_cast<int>(x_values.size())) {
            scheduled_pairs = static_cast<int>(std::count_if(
                x_values[l].begin(), x_values[l].end(), [](int value) { return value != 0; }));
        }
        const int planned_lesson_hours = planned_pairs * 2;
        const int scheduled_lesson_hours = scheduled_pairs * 2;
        result.planned_hours += planned_lesson_hours;
        result.scheduled_hours += scheduled_lesson_hours;
        if (scheduled_lesson_hours < planned_lesson_hours) {
            result.missing_hours += planned_lesson_hours - scheduled_lesson_hours;
            result.mismatched_lessons++;
        } else if (scheduled_lesson_hours > planned_lesson_hours) {
            result.excess_hours += scheduled_lesson_hours - planned_lesson_hours;
            result.mismatched_lessons++;
        }
    }
    // remaining_hours — публичное поле обратной совместимости. Оно отражает
    // реальный недобор, а не скомпенсированную разницу общих сумм.
    result.remaining_hours = result.missing_hours;
    return result;
}

std::vector<std::vector<int>> ComputeGroupPartWeeklyOccupiedPairs(
    const std::vector<Lesson>& lessons,
    const std::vector<int>& quotas,
    int group_count,
    const std::vector<int>& group_part_count
) {
    const int safe_group_count = std::max(0, group_count);
    std::vector<std::vector<int>> result(
        safe_group_count, std::vector<int>(PARTS_PER_GROUP, 0));

    const int lesson_count = std::min(
        static_cast<int>(lessons.size()), static_cast<int>(quotas.size()));
    for (int l = 0; l < lesson_count; ++l) {
        const Lesson& lesson = lessons[l];
        if (quotas[l] <= 0 || lesson.group < 0 || lesson.group >= safe_group_count) continue;

        const int parts = lesson.group < static_cast<int>(group_part_count.size())
            ? std::clamp(group_part_count[lesson.group], 1, PARTS_PER_GROUP)
            : PARTS_PER_GROUP;
        const int occupied_pairs = quotas[l] * (lesson.is_block ? 2 : 1);
        for (int part = 0; part < parts; ++part) {
            if (LessonAffectsPart(lesson, lesson.group, part)) {
                result[lesson.group][part] += occupied_pairs;
            }
        }
    }
    return result;
}

int EffectiveStudentDailyMinimum(int requested_minimum, int weekly_part_pairs) {
    if (requested_minimum <= 0 || weekly_part_pairs <= 0) return 0;
    return std::min(requested_minimum, weekly_part_pairs);
}

int EffectiveTeacherMaxPairsPerDay(int configured_limit) {
    if (configured_limit <= 0) return 0;
    return std::min(configured_limit, SLOTS_PER_DAY);
}

}  // namespace timetable
