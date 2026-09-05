#pragma once

#include "data_store.h"

namespace timetable {
// Reserve continuous student timelines including the zero class hour before
// ordinary placement; concrete room allocation is independently validated later.
void AddClassHourTimeConstraints(
    operations_research::sat::CpModelBuilder& model,
    const ScheduleInputData& data, const std::vector<Date>& days,
    const std::vector<std::vector<std::vector<operations_research::sat::BoolVar>>>& part_busy,
    const std::vector<std::vector<operations_research::sat::BoolVar>>& teacher_busy,
    const std::vector<std::vector<operations_research::sat::IntVar>>& group_campus,
    const std::vector<std::vector<operations_research::sat::IntVar>>& teacher_campus);
// Plans on a copy; caller publishes only after independent validation.
bool PlanClassHours(const ScheduleInputData& data, JsonValue& schedule, std::string& error);
JsonValue ValidateClassHours(const ScheduleInputData& data, const JsonValue& schedule);
bool FinalizeSchedule(const ScheduleInputData& data, const std::string& output_dir, std::string& error, bool draft_semester_risk = false);
}
