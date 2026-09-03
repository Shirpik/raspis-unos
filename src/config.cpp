#include "config.h"

namespace timetable {

std::vector<std::string> GROUP_NAME = {
    "ИСП-3304",
    "ИСП-3305п"
};

std::vector<std::string> TEACHER_NAME = {
    "Новосёлова",
    "Давыдова",
    "Нуров",
    "Потапова",
    "Серянина",
    "Гобов",
    "Самцова",
    "Гарбузов"
};

std::vector<int> GROUP_CURATOR_TEACHER(GROUP_NAME.size(), -1);
std::vector<int> GROUP_HOME_CAMPUS(GROUP_NAME.size(), -1);
std::vector<int> GROUP_CLASS_HOUR_CAMPUS(GROUP_NAME.size(), -1);
std::vector<bool> GROUP_CLASS_HOUR_ENABLED(GROUP_NAME.size(), true);

const std::vector<std::string> WEEKDAY_NAME = {
    "ПН", "ВТ", "СР", "ЧТ", "ПТ", "СБ", "ВС"
};

namespace {
const std::string kUnknownGroup = "Неизвестная группа";
const std::string kUnknownTeacher = "Неизвестный преподаватель";
}

int GroupCount() {
    return static_cast<int>(GROUP_NAME.size());
}

int TeacherCount() {
    return static_cast<int>(TEACHER_NAME.size());
}

const std::string& GroupName(int index) {
    if (index < 0 || index >= static_cast<int>(GROUP_NAME.size())) {
        return kUnknownGroup;
    }
    return GROUP_NAME[index];
}

const std::string& TeacherName(int index) {
    if (index < 0 || index >= static_cast<int>(TEACHER_NAME.size())) {
        return kUnknownTeacher;
    }
    return TEACHER_NAME[index];
}

void SetRuntimeNames(const std::vector<std::string>& groups, const std::vector<std::string>& teachers) {
    GROUP_NAME = groups;
    TEACHER_NAME = teachers;
}

void SetRuntimeGroupMetadata(
    const std::vector<int>& curator_teachers,
    const std::vector<int>& home_campuses,
    const std::vector<int>& class_hour_campuses,
    const std::vector<bool>& class_hour_enabled
) {
    GROUP_CURATOR_TEACHER = curator_teachers;
    GROUP_HOME_CAMPUS = home_campuses;
    GROUP_CLASS_HOUR_CAMPUS = class_hour_campuses;
    GROUP_CLASS_HOUR_ENABLED = class_hour_enabled;
}

}  // namespace timetable
