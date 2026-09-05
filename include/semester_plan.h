#pragma once
#include "data_store.h"
namespace timetable {
// Derives load from curriculum hours and confirmed journal records, never from API totals.
void PrepareSemesterRequirements(const JsonValue& root, ScheduleInputData& data);
std::string LoadRequirementError(const ScheduleInputData& data);
bool CheckSemesterPreflight(const ScheduleInputData& data, const std::string& output_dir, std::string& error);
}
