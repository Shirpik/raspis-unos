#include "types.h"

#include "config.h"

namespace timetable {

bool LessonCalendarAllows(const Lesson& lesson, const Date& date) {
    if (lesson.is_block || lesson.is_pp || !lesson.calendar_restricted) return true;
    for (const auto& range : lesson.teaching_windows)
        if (range.first <= date && date <= range.second) return true;
    return false;
}

std::string SubjectFamilyKey(const Lesson& lesson) {
    if (lesson.subject_id >= 0) {
        return "subject_" + std::to_string(lesson.subject_id);
    }

    return lesson.name;
}

bool LessonAffectsPart(const Lesson& lesson, int group, int part) {
    if (lesson.group != group) {
        return false;
    }

    if (lesson.subgroup == -1) {
        return true;
    }

    int base_subgroup = group * PARTS_PER_GROUP;
    return lesson.subgroup == base_subgroup + part;
}

}  // namespace timetable
