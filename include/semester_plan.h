#pragma once
#include "data_store.h"
namespace timetable {
struct TeachingBalances {
    std::map<int, int> confirmed;
    std::map<int, int> reserved;
};
TeachingBalances ReadTeachingBalances(const JsonValue& root, Date before);
Date GroupTeachingDeadline(const GroupData& group, Date semester_first, Date semester_last);
bool GroupRegularCalendarAllows(const GroupData& group, const Date& date);
int GroupTeachingCapacity(const ScheduleInputData& data, const GroupData& group,
                          Date first, Date last, const TeacherData* teacher = nullptr, int student_streams = 1);
// Derives load from curriculum hours and confirmed journal records, never from API totals.
void PrepareSemesterRequirements(const JsonValue& root, ScheduleInputData& data);
std::string LoadRequirementError(const ScheduleInputData& data);
bool CheckSemesterPreflight(const ScheduleInputData& data, const std::string& output_dir, std::string& error);
}
