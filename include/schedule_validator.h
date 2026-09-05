#pragma once

#include <string>

#include "data_store.h"
#include "json_utils.h"
#include "runtime_config.h"

namespace timetable {

struct ScheduleValidationOptions {
    bool draft_semester_risk = false;
    std::string source = "auto";
    bool require_full_period = true;
    bool require_exact_quotas = true;
    bool include_soft_warnings = true;
};

struct ScheduleValidationResult {
    bool checked = false;
    bool ok = false;
    int hard_error_count = 0;
    int warning_count = 0;
    int event_count = 0;
    int planned_occurrences = 0;
    int scheduled_occurrences = 0;
    long long remaining_hours = 0;
    long long excess_hours = 0;
    int incomplete_lessons = 0;
    int mismatched_lessons = 0;
    int unassigned_rooms = 0;
    std::string message;
    JsonValue report = JsonValue::MakeObject();
};

// Независимая от CP-SAT проверка уже сформированного schedule_all.json.
// Она намеренно повторяет жёсткие правила модели: статус FEASIBLE сам по себе
// не является доказательством корректности сериализованного результата.
ScheduleValidationResult ValidateScheduleJson(
    const ScheduleInputData& data,
    const RuntimeSolverConfig& config,
    const JsonValue& schedule,
    const ScheduleValidationOptions& options = {}
);

}  // namespace timetable
