#include "json_utils.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "ortools/sat/cp_model.h"
#include "ortools/sat/cp_model_solver.h"
#include "ortools/sat/cp_model.pb.h"
#include "ortools/sat/model.h"
#include "ortools/sat/sat_parameters.pb.h"

namespace {

using operations_research::Domain;
using operations_research::sat::BoolVar;
using operations_research::sat::CpModelBuilder;
using operations_research::sat::CpSolverResponse;
using operations_research::sat::CpSolverStatus;
using operations_research::sat::CpSolverStatus_Name;
using operations_research::sat::IntVar;
using operations_research::sat::LinearExpr;
using operations_research::sat::Model;
using operations_research::sat::NewSatParameters;
using operations_research::sat::SatParameters;
using operations_research::sat::SolutionIntegerValue;
using operations_research::sat::SolveCpModel;
using timetable::JsonInt;
using timetable::JsonBool;
using timetable::JsonParseResult;
using timetable::JsonString;
using timetable::JsonValue;
using timetable::ParseJson;
using timetable::ToJson;

struct VariableData {
    int id = -1;
    int minimum = 0;
    int maximum = 0;
    int semester_total = 0;
    int distribution_periods = 0;
    int priority_weight = 1;
    std::map<int, int> daily_subject_limits;
    int teacher = -1;
    int group = -1;
    int part_weight = 1;
    bool whole_group = false;
    bool sports_room = false;
    bool computer_room = false;
    std::set<int> block_start_slots;
    int consecutive_pairs = 1;
    int restricted_room = -1;
    bool avoid_lunch_split = false;
    std::string subject;
    std::vector<int> parts;
    std::set<int> allowed_slots;
    std::set<int> allowed_campuses;
};

std::string ReadAll(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    std::ostringstream buffer;
    buffer << input.rdbuf();
    std::string text = buffer.str();
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);
    }
    return text;
}

void WriteResult(const JsonValue& value) {
    std::cout << ToJson(value, 2) << "\n";
}

JsonValue ErrorResult(const std::string& status, const std::string& message) {
    JsonValue result = JsonValue::MakeObject();
    result.At("success") = JsonValue::MakeBool(false);
    result.At("status") = JsonValue::MakeString(status);
    result.At("message") = JsonValue::MakeString(message);
    return result;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        WriteResult(ErrorResult("INVALID_ARGUMENT", "Usage: quota_optimizer <model.json>"));
        return 2;
    }

    const std::string input_text = ReadAll(argv[1]);
    if (input_text.empty()) {
        WriteResult(ErrorResult("INPUT_ERROR", "Cannot read quota model"));
        return 2;
    }
    const JsonParseResult parsed = ParseJson(input_text);
    if (!parsed.ok || !parsed.value.IsObject()) {
        WriteResult(ErrorResult("JSON_ERROR", parsed.error));
        return 2;
    }

    const JsonValue& root = parsed.value;
    const int day_count = std::max(1, JsonInt(root, "day_count", 1));
    const int slots_per_day = std::max(1, JsonInt(root, "slots_per_day", 7));
    const int total_time_slots = day_count * slots_per_day;
    const JsonValue& variable_json = root.At("variables");
    if (!variable_json.IsArray()) {
        WriteResult(ErrorResult("MODEL_ERROR", "variables must be an array"));
        return 2;
    }

    CpModelBuilder model;
    std::vector<VariableData> variables;
    std::vector<IntVar> quota;
    std::map<int, int> index_by_id;
    variables.reserve(variable_json.array_value.size());
    quota.reserve(variable_json.array_value.size());

    for (const JsonValue& item : variable_json.array_value) {
        VariableData data;
        data.id = JsonInt(item, "id", -1);
        data.minimum = JsonInt(item, "minimum", 0);
        data.maximum = JsonInt(item, "maximum", 0);
        data.semester_total = JsonInt(item, "semester_total", 0);
        data.distribution_periods = std::max(0, JsonInt(item, "distribution_weeks", 0));
        data.priority_weight = std::clamp(JsonInt(item, "priority_weight", 1), 1, 1000);
        for (const auto& override_value : item.At("daily_subject_limits").array_value)
            data.daily_subject_limits[JsonInt(override_value, "day", -1)] = std::clamp(JsonInt(override_value, "maximum", 3), 1, 7);
        data.teacher = JsonInt(item, "teacher", -1);
        data.group = JsonInt(item, "group", -1);
        data.part_weight = std::max(1, JsonInt(item, "part_weight", 1));
        data.whole_group = JsonBool(item, "whole_group", false);
        data.sports_room = JsonBool(item, "sports_room", false);
        data.computer_room = JsonBool(item, "computer_room", false);
        data.consecutive_pairs = JsonInt(item, "consecutive_pairs", 1);
        for (const auto& v : item.At("block_start_slots").array_value) data.block_start_slots.insert(static_cast<int>(v.number_value));
        data.restricted_room = JsonInt(item, "restricted_room", -1);
        data.avoid_lunch_split = JsonBool(item, "avoid_lunch_split", false);
        data.subject = JsonString(item, "subject", "-1");
        const JsonValue& parts_json = item.At("parts");
        const JsonValue& allowed_slots_json = item.At("allowed_slots");
        const JsonValue& allowed_campuses_json = item.At("allowed_campuses");
        if (!parts_json.IsArray() || !allowed_slots_json.IsArray() ||
            !allowed_campuses_json.IsArray()) {
            WriteResult(ErrorResult("MODEL_ERROR", "Invalid variable placement metadata"));
            return 2;
        }
        for (const JsonValue& value : parts_json.array_value) {
            data.parts.push_back(static_cast<int>(value.number_value));
        }
        for (const JsonValue& value : allowed_slots_json.array_value) {
            const int slot = static_cast<int>(value.number_value);
            if (slot >= 0 && slot < total_time_slots) data.allowed_slots.insert(slot);
        }
        for (const JsonValue& value : allowed_campuses_json.array_value) {
            const int campus = static_cast<int>(value.number_value);
            if (campus == 0 || campus == 1) data.allowed_campuses.insert(campus);
        }
        if (data.id < 0 || data.minimum < 0 || data.maximum < data.minimum || index_by_id.count(data.id)) {
            WriteResult(ErrorResult("MODEL_ERROR", "Invalid or duplicate lesson variable"));
            return 2;
        }
        index_by_id[data.id] = static_cast<int>(variables.size());
        variables.push_back(data);
        quota.push_back(model.NewIntVar(Domain(data.minimum, data.maximum)));
    }

    // A quota is accepted only when it has an actual conflict-free placement
    // witness for the short period.  This closes the gap between arithmetic
    // teacher/group totals and a timetable that can really be laid out.
    std::vector<std::vector<BoolVar>> placed(
        variables.size(), std::vector<BoolVar>(total_time_slots));
    for (int index = 0; index < static_cast<int>(variables.size()); ++index) {
        LinearExpr placed_count;
        for (int time = 0; time < total_time_slots; ++time) {
            placed[index][time] = model.NewBoolVar();
            placed_count += placed[index][time];
            if (!variables[index].allowed_slots.count(time)) {
                model.AddEquality(placed[index][time], 0);
            }
        }
        model.AddEquality(quota[index], placed_count);
        if (variables[index].avoid_lunch_split && slots_per_day >= 3) {
            for (int day = 0; day < day_count; ++day)
                model.AddLessOrEqual(LinearExpr(placed[index][day * slots_per_day + 1]) + placed[index][day * slots_per_day + 2], 1);
        }
        if (variables[index].consecutive_pairs == 2) {
            for (int day = 0; day < day_count; ++day) {
                std::vector<std::vector<BoolVar>> cover(slots_per_day);
                for (int slot = 0; slot + 1 < slots_per_day; ++slot) {
                    if (variables[index].avoid_lunch_split && slot == 1) continue;
                    if (!variables[index].block_start_slots.empty() && !variables[index].block_start_slots.count(slot)) continue;
                    auto block = model.NewBoolVar();
                    cover[slot].push_back(block); cover[slot + 1].push_back(block);
                }
                for (int slot = 0; slot < slots_per_day; ++slot)
                    model.AddEquality(placed[index][day * slots_per_day + slot], LinearExpr::Sum(cover[slot]));
            }
        }
    }

    std::map<int, std::vector<int>> restricted_rooms;
    for (int index = 0; index < static_cast<int>(variables.size()); ++index)
        if (variables[index].restricted_room >= 0) restricted_rooms[variables[index].restricted_room].push_back(index);
    for (const auto& entry : restricted_rooms) for (int time = 0; time < total_time_slots; ++time) {
        LinearExpr occupied;
        for (int index : entry.second) occupied += placed[index][time];
        model.AddLessOrEqual(occupied, 1);
    }
    // Optional manual placements.  Quota selection is accepted only when the
    // exact fixed cells can coexist with every other hard rule; malformed
    // references fail closed instead of being ignored.
    const JsonValue& fixed = root.At("fixed");
    if (!fixed.IsNull() && !fixed.IsArray()) {
        WriteResult(ErrorResult("MODEL_ERROR", "fixed must be an array"));
        return 2;
    }
    if (fixed.IsArray()) {
        for (const JsonValue& assignment : fixed.array_value) {
            const int id = JsonInt(assignment, "lesson_id", -1);
            const int time = JsonInt(assignment, "time", -1);
            const auto found = index_by_id.find(id);
            if (found == index_by_id.end() || time < 0 || time >= total_time_slots ||
                !variables[found->second].allowed_slots.count(time)) {
                WriteResult(ErrorResult("MODEL_ERROR", "Invalid fixed placement"));
                return 2;
            }
            model.AddEquality(placed[found->second][time], 1);
        }
    }

    LinearExpr preferred_reward;
    const JsonValue& preferred = root.At("preferred");
    if (!preferred.IsNull() && !preferred.IsArray()) {
        WriteResult(ErrorResult("MODEL_ERROR", "preferred must be an array"));
        return 2;
    }
    if (preferred.IsArray()) {
        std::set<std::pair<int, int>> seen;
        for (const JsonValue& assignment : preferred.array_value) {
            const int id = JsonInt(assignment, "lesson_id", -1);
            const int time = JsonInt(assignment, "time", -1);
            const auto found = index_by_id.find(id);
            if (found == index_by_id.end() || time < 0 || time >= total_time_slots ||
                !variables[found->second].allowed_slots.count(time)) {
                continue;
            }
            if (!seen.insert({found->second, time}).second) continue;
            preferred_reward += placed[found->second][time];
        }
    }

    const JsonValue& parts = root.At("parts");
    if (!parts.IsArray()) {
        WriteResult(ErrorResult("MODEL_ERROR", "parts must be an array"));
        return 2;
    }
    LinearExpr total_part_load;
    const bool maximize_part_load = JsonBool(root, "maximize_part_load", false);
    for (const JsonValue& part : parts.array_value) {
        LinearExpr sum;
        const JsonValue& lesson_ids = part.At("lesson_ids");
        if (!lesson_ids.IsArray()) {
            WriteResult(ErrorResult("MODEL_ERROR", "part.lesson_ids must be an array"));
            return 2;
        }
        for (const JsonValue& lesson_id : lesson_ids.array_value) {
            const int id = static_cast<int>(lesson_id.number_value);
            const auto found = index_by_id.find(id);
            if (found == index_by_id.end()) {
                WriteResult(ErrorResult("MODEL_ERROR", "Unknown lesson in part constraint"));
                return 2;
            }
            sum += quota[found->second];
        }
        if (part.At("minimum_target").IsNumber() || part.At("maximum_target").IsNumber()) {
            const int minimum_target = std::max(0, JsonInt(part, "minimum_target", 0));
            const int maximum_target = std::max(
                minimum_target, JsonInt(part, "maximum_target", slots_per_day * day_count));
            model.AddGreaterOrEqual(sum, minimum_target);
            model.AddLessOrEqual(sum, maximum_target);
        } else {
            model.AddEquality(sum, JsonInt(part, "target", 0));
        }
        total_part_load += sum;
    }

    std::map<int, std::vector<int>> teacher_indices;
    std::map<int, std::vector<int>> part_indices;
    std::map<std::pair<int, std::string>, std::vector<int>> whole_subject_indices;
    std::map<std::pair<int, std::string>, std::vector<int>> part_subject_indices;
    for (int index = 0; index < static_cast<int>(variables.size()); ++index) {
        const VariableData& data = variables[index];
        teacher_indices[data.teacher].push_back(index);
        for (int part_key : data.parts) {
            part_indices[part_key].push_back(index);
            part_subject_indices[{part_key, data.subject}].push_back(index);
        }
        if (data.whole_group) {
            whole_subject_indices[{data.group, data.subject}].push_back(index);
        }
    }

    // A teacher and a physical student subgroup can each occupy at most one
    // lesson in one physical pair slot. Parallel subgroups remain independent.
    for (const auto& [teacher_id, indices] : teacher_indices) {
        if (teacher_id < 0) continue;
        for (int time = 0; time < total_time_slots; ++time) {
            LinearExpr load;
            for (int index : indices) load += placed[index][time];
            model.AddLessOrEqual(load, 1);
        }
    }

    const int min_student_pairs = std::max(
        0, JsonInt(root, "min_student_pairs_per_day", 0));
    const int max_student_pairs = std::clamp(
        JsonInt(root, "max_student_pairs_per_day", slots_per_day),
        1, slots_per_day);
    const bool no_student_windows = JsonBool(root, "hard_no_student_windows", false);
    LinearExpr student_day_shortfall;
    const int preferred_student_pairs = std::clamp(
        JsonInt(root, "preferred_student_pairs_per_day", 0), 0, max_student_pairs);
    std::map<std::pair<int, int>, BoolVar> part_active_days;
    for (const auto& [part_key, indices] : part_indices) {
        (void)part_key;
        for (int day = 0; day < day_count; ++day) {
            std::vector<LinearExpr> slot_load(slots_per_day);
            LinearExpr day_load;
            for (int slot = 0; slot < slots_per_day; ++slot) {
                const int time = day * slots_per_day + slot;
                for (int index : indices) slot_load[slot] += placed[index][time];
                model.AddLessOrEqual(slot_load[slot], 1);
                day_load += slot_load[slot];
            }
            auto active_day = model.NewBoolVar();
            part_active_days.emplace(std::make_pair(part_key, day), active_day);
            if (JsonBool(root, "require_all_student_days", false))
                model.AddEquality(active_day, 1);
            model.AddGreaterOrEqual(day_load, active_day * std::max(1, min_student_pairs));
            model.AddLessOrEqual(day_load, active_day * max_student_pairs);
            model.AddLessOrEqual(day_load, max_student_pairs);
            if (preferred_student_pairs > 0) {
                IntVar missing = model.NewIntVar(Domain(0, preferred_student_pairs));
                model.AddGreaterOrEqual(missing, preferred_student_pairs * active_day - day_load);
                student_day_shortfall += missing;
            }
            if (no_student_windows) {
                for (int left = 0; left < slots_per_day; ++left) {
                    for (int middle = left + 1; middle < slots_per_day; ++middle) {
                        for (int right = middle + 1; right < slots_per_day; ++right) {
                            LinearExpr no_gap = slot_load[left] + slot_load[right] - slot_load[middle];
                            model.AddLessOrEqual(no_gap, 1);
                        }
                    }
                }
            }
        }
    }

    if (JsonBool(root, "require_same_subgroup_study_days", true)) {
        for (const auto& [key, active] : part_active_days) {
            if (key.first % 2 != 0) continue;
            const auto sibling = part_active_days.find({key.first + 1, key.second});
            if (sibling != part_active_days.end()) model.AddEquality(active, sibling->second);
        }
    }

    const int whole_subject_limit = std::clamp(
        JsonInt(root, "whole_group_same_subject_limit", 2), 1, slots_per_day);
    const int part_subject_limit = std::clamp(
        JsonInt(root, "physical_part_same_subject_limit", 3), 1, slots_per_day);
    const auto add_subject_limits = [&](const auto& families, int limit, bool allow_override) {
        for (const auto& [key, indices] : families) {
            (void)key;
            for (int day = 0; day < day_count; ++day) {
                int day_limit = limit;
                if (allow_override) {
                    day_limit = slots_per_day;
                    for (int index : indices) {
                        const auto found = variables[index].daily_subject_limits.find(day);
                        day_limit = std::min(day_limit, found == variables[index].daily_subject_limits.end() ? limit : found->second);
                    }
                }
                LinearExpr load;
                for (int slot = 0; slot < slots_per_day; ++slot) {
                    const int time = day * slots_per_day + slot;
                    for (int index : indices) load += placed[index][time];
                }
                model.AddLessOrEqual(load, day_limit);
            }
        }
    };
    add_subject_limits(whole_subject_indices, whole_subject_limit, false);
    add_subject_limits(part_subject_indices, part_subject_limit, true);

    // Every group and teacher uses one campus during a day. Hard singleton
    // campus permissions therefore propagate through every selected lesson.
    std::map<std::pair<int, int>, BoolVar> group_day_campus;
    std::map<std::pair<int, int>, BoolVar> teacher_day_campus;
    const auto campus_var = [&](auto& values, int entity, int day) {
        const std::pair<int, int> key{entity, day};
        auto found = values.find(key);
        if (found != values.end()) return found->second;
        return values.emplace(key, model.NewBoolVar()).first->second;
    };
    for (int index = 0; index < static_cast<int>(variables.size()); ++index) {
        const VariableData& data = variables[index];
        for (int time = 0; time < total_time_slots; ++time) {
            const int day = time / slots_per_day;
            const BoolVar selected = placed[index][time];
            if (data.allowed_campuses.empty()) {
                model.AddEquality(selected, 0);
                continue;
            }
            const BoolVar group_campus = campus_var(group_day_campus, data.group, day);
            const BoolVar teacher_campus = campus_var(teacher_day_campus, data.teacher, day);
            model.AddEquality(group_campus, teacher_campus).OnlyEnforceIf(selected);
            if (data.allowed_campuses.size() == 1) {
                const int campus = *data.allowed_campuses.begin();
                model.AddEquality(group_campus, campus).OnlyEnforceIf(selected);
                model.AddEquality(teacher_campus, campus).OnlyEnforceIf(selected);
            }
        }
    }

    for (const JsonValue& rule : root.At("teacher_day_campuses").array_value) {
        const int teacher = JsonInt(rule, "teacher", -1);
        const int day = JsonInt(rule, "day", -1);
        const int campus = JsonInt(rule, "campus", -1);
        if (teacher < 0 || day < 0 || day >= day_count || campus < 0 || campus > 1) {
            WriteResult(ErrorResult("MODEL_ERROR", "Invalid teacher day campus"));
            return 2;
        }
        model.AddEquality(campus_var(teacher_day_campus, teacher, day), campus);
    }

    // Match the timetable solver's physical room capacity by campus.  The
    // earlier quota model could choose a conflict-free lesson set that still
    // overfilled one campus once concrete rooms were allocated.
    const auto capacity_pair = [&](const char* key) {
        std::vector<int> result{0, 0};
        const JsonValue& values = root.At(key);
        if (values.IsArray()) {
            for (int campus = 0; campus < 2 && campus < static_cast<int>(values.array_value.size()); ++campus) {
                result[campus] = std::max(0, static_cast<int>(values.array_value[campus].number_value));
            }
        }
        return result;
    };
    const std::vector<int> room_capacity = capacity_pair("room_capacity_by_campus");
    const std::vector<int> sports_capacity = capacity_pair("sports_capacity_by_campus");
    if (root.At("room_capacity_by_campus").IsArray() || root.At("room_capacity_by_time").IsArray()
        || root.At("sports_capacity_by_campus").IsArray() || root.At("sports_capacity_by_time").IsArray()) {
        for (int time = 0; time < total_time_slots; ++time) {
            LinearExpr general0;
            LinearExpr general1;
            LinearExpr sports0;
            LinearExpr sports1;
            LinearExpr computer0, computer1;
            for (int index = 0; index < static_cast<int>(variables.size()); ++index) {
                const VariableData& data = variables[index];
                const BoolVar selected = placed[index][time];
                const int day = time / slots_per_day;
                const BoolVar campus = campus_var(group_day_campus, data.group, day);
                const BoolVar at_campus1 = model.NewBoolVar();
                model.AddLessOrEqual(at_campus1, selected);
                model.AddLessOrEqual(at_campus1, campus);
                model.AddGreaterOrEqual(at_campus1, LinearExpr(selected) + campus - 1);
                LinearExpr& demand0 = data.sports_room ? sports0 : general0;
                LinearExpr& demand1 = data.sports_room ? sports1 : general1;
                demand1 += at_campus1;
                demand0 += selected;
                demand0 -= at_campus1;
                if (data.computer_room) { computer1 += at_campus1; computer0 += selected; computer0 -= at_campus1; }
            }
            if (root.At("computer_capacity_by_campus").IsArray()) {
                const auto computer_capacity = capacity_pair("computer_capacity_by_campus");
                model.AddLessOrEqual(computer0, computer_capacity[0]);
                model.AddLessOrEqual(computer1, computer_capacity[1]);
            }
            const auto& general_by_time = root.At("room_capacity_by_time");
            const bool has_general_time = general_by_time.IsArray() && time < static_cast<int>(general_by_time.array_value.size()) &&
                general_by_time.array_value[time].IsArray() && general_by_time.array_value[time].array_value.size() == 2;
            model.AddLessOrEqual(general0, has_general_time ? static_cast<int>(general_by_time.array_value[time].array_value[0].number_value) : room_capacity[0]);
            model.AddLessOrEqual(general1, has_general_time ? static_cast<int>(general_by_time.array_value[time].array_value[1].number_value) : room_capacity[1]);
            const JsonValue& by_time = root.At("sports_capacity_by_time");
            const bool has_time_capacity = by_time.IsArray() && time < static_cast<int>(by_time.array_value.size())
                && by_time.array_value[time].IsArray() && by_time.array_value[time].array_value.size() == 2;
            const int capacity0 = has_time_capacity ? static_cast<int>(by_time.array_value[time].array_value[0].number_value) : sports_capacity[0];
            const int capacity1 = has_time_capacity ? static_cast<int>(by_time.array_value[time].array_value[1].number_value) : sports_capacity[1];
            model.AddLessOrEqual(sports0, capacity0);
            model.AddLessOrEqual(sports1, capacity1);
        }
    }

    const bool allow_teacher_shortfalls = JsonBool(root, "allow_teacher_shortfalls", false);
    LinearExpr teacher_shortfall_objective;
    std::map<int, IntVar> teacher_deficits;
    const JsonValue& teachers = root.At("teachers");
    if (!teachers.IsArray()) {
        WriteResult(ErrorResult("MODEL_ERROR", "teachers must be an array"));
        return 2;
    }
    for (const JsonValue& teacher : teachers.array_value) {
        const int teacher_id = JsonInt(teacher, "id", -1);
        LinearExpr load;
        for (int index = 0; index < static_cast<int>(variables.size()); ++index) {
            if (variables[index].teacher == teacher_id) load += quota[index];
        }
        const int minimum = JsonInt(teacher, "minimum", 0);
        if (allow_teacher_shortfalls && minimum > 0) {
            model.AddGreaterOrEqual(load, JsonInt(teacher, "hard_minimum", 0));
            IntVar deficit = model.NewIntVar(Domain(0, minimum));
            teacher_deficits.emplace(teacher_id, deficit);
            model.AddGreaterOrEqual(load + deficit, minimum);
            teacher_shortfall_objective += deficit;
        } else {
            model.AddGreaterOrEqual(load, minimum);
        }
        model.AddLessOrEqual(load, JsonInt(teacher, "maximum", 0));
        for (int day = 0; day < day_count; ++day) {
            LinearExpr day_load;
            std::vector<LinearExpr> teacher_slots(slots_per_day);
            for (int index : teacher_indices[teacher_id])
                for (int slot = 0; slot < slots_per_day; ++slot) {
                    day_load += placed[index][day * slots_per_day + slot];
                    teacher_slots[slot] += placed[index][day * slots_per_day + slot];
                }
            if (JsonBool(root, "hard_no_teacher_windows", false)) {
                for (int left = 0; left < slots_per_day; ++left)
                    for (int middle = left + 1; middle < slots_per_day; ++middle)
                        for (int right = middle + 1; right < slots_per_day; ++right)
                            model.AddLessOrEqual(teacher_slots[left] + teacher_slots[right] - teacher_slots[middle], 1);
            }
            const int maximum_daily = JsonInt(teacher, "maximum_daily", slots_per_day);
            if (maximum_daily > 0) model.AddLessOrEqual(day_load, maximum_daily);
            for (const auto& target : teacher.At("day_targets").array_value)
                if (JsonInt(target, "day", -1) == day) model.AddGreaterOrEqual(day_load, JsonInt(target, "minimum", 0));
        }
        const int max_work_days = JsonInt(teacher, "max_work_days", 0);
        if (max_work_days > 0) {
            LinearExpr active_days;
            const auto found = teacher_indices.find(teacher_id);
            for (int day = 0; day < day_count; ++day) {
                BoolVar active = model.NewBoolVar();
                LinearExpr day_load;
                if (found != teacher_indices.end()) {
                    for (int slot = 0; slot < slots_per_day; ++slot) {
                        const int time = day * slots_per_day + slot;
                        for (int index : found->second) day_load += placed[index][time];
                    }
                }
                model.AddGreaterOrEqual(day_load, active);
                model.AddLessOrEqual(day_load, slots_per_day * active);
                active_days += active;
            }
            model.AddLessOrEqual(active_days, std::min(max_work_days, day_count));
        }
    }

    for (const auto& target : root.At("lesson_load_targets").array_value) {
        LinearExpr load;
        for (const auto& id : target.At("lesson_ids").array_value) {
            const auto found = index_by_id.find(static_cast<int>(id.number_value));
            if (found != index_by_id.end()) load += quota[found->second];
        }
        model.AddGreaterOrEqual(load, JsonInt(target, "minimum", 0));
    }
    const JsonValue& lab_rules = root.At("lab_rules");
    if (!lab_rules.IsArray()) {
        WriteResult(ErrorResult("MODEL_ERROR", "lab_rules must be an array"));
        return 2;
    }
    for (const JsonValue& rule : lab_rules.array_value) {
        LinearExpr theory_sum;
        const int prior_theory = std::max(0, JsonInt(rule, "prior_theory", 0));
        const JsonValue& theories = rule.At("theory_ids");
        const JsonValue& labs = rule.At("lab_ids");
        if (!theories.IsArray() || !labs.IsArray()) {
            WriteResult(ErrorResult("MODEL_ERROR", "Invalid LPZ rule"));
            return 2;
        }
        for (const JsonValue& theory_id : theories.array_value) {
            const auto found = index_by_id.find(static_cast<int>(theory_id.number_value));
            if (found != index_by_id.end()) theory_sum += quota[found->second];
        }
        for (const JsonValue& lab_id : labs.array_value) {
            const auto found = index_by_id.find(static_cast<int>(lab_id.number_value));
            if (found == index_by_id.end()) continue;
            const int index = found->second;
            BoolVar active = model.NewBoolVar();
            model.AddGreaterOrEqual(quota[index], active);
            model.AddLessOrEqual(quota[index], variables[index].maximum * active);
            model.AddGreaterOrEqual(theory_sum + prior_theory, 2 * active);
            for (int time = 0; time < total_time_slots; ++time) {
                LinearExpr theory_before;
                for (const JsonValue& theory_id : theories.array_value) {
                    const auto theory_found = index_by_id.find(
                        static_cast<int>(theory_id.number_value));
                    if (theory_found == index_by_id.end()) continue;
                    for (int earlier = 0; earlier < time; ++earlier) {
                        theory_before += placed[theory_found->second][earlier];
                    }
                }
                model.AddGreaterOrEqual(
                    theory_before + prior_theory, 2 * placed[index][time]);
            }
        }
    }

    // Compare weeks * quota with semester occurrences using integer arithmetic.
    // Hard teacher minima stay constraints. Optional weights let the caller
    // balance ordinary teacher targets against preferred student day loads.
    const int64_t teacher_shortfall_weight = root.At("teacher_shortfall_weight").IsNumber()
        ? static_cast<int64_t>(root.At("teacher_shortfall_weight").number_value) : 1000000000000LL;
    LinearExpr objective = teacher_shortfall_weight * teacher_shortfall_objective;
    objective += JsonInt(root, "student_day_shortfall_weight", 10000) * student_day_shortfall;
    const int teacher_overload_weight = JsonInt(root, "teacher_overload_weight", 0);
    if (teacher_overload_weight > 0) {
        for (const JsonValue& teacher : teachers.array_value) {
            LinearExpr load;
            for (int index : teacher_indices[JsonInt(teacher, "id", -1)]) load += quota[index];
            IntVar excess = model.NewIntVar(Domain(0, JsonInt(teacher, "maximum", 0)));
            model.AddGreaterOrEqual(excess, load - JsonInt(teacher, "minimum", 0));
            objective += teacher_overload_weight * excess;
        }
    }
    if (maximize_part_load) objective -= 1000000000LL * total_part_load;
    objective -= 1000000 * preferred_reward;
    const int kDistributionWeeks = std::max(1, JsonInt(root, "distribution_weeks", 16));
    for (int index = 0; index < static_cast<int>(variables.size()); ++index) {
        const int periods = variables[index].distribution_periods > 0
            ? variables[index].distribution_periods : kDistributionWeeks;
        const int upper = std::max(
            variables[index].semester_total,
            periods * variables[index].maximum);
        IntVar deviation = model.NewIntVar(Domain(0, upper));
        model.AddGreaterOrEqual(
            deviation, periods * quota[index] - variables[index].semester_total);
        model.AddGreaterOrEqual(
            deviation, variables[index].semester_total - periods * quota[index]);
        objective += variables[index].part_weight * variables[index].priority_weight * deviation;
    }
    model.Minimize(objective);

    SatParameters parameters;
    parameters.set_num_search_workers(std::clamp(JsonInt(root, "workers", 4), 1, 8));
    parameters.set_max_time_in_seconds(std::max(1, JsonInt(root, "time_limit_seconds", 30)));
    parameters.set_random_seed(JsonInt(root, "random_seed", 37));
    parameters.set_stop_after_first_solution(false);
    parameters.set_linearization_level(2);
    parameters.set_symmetry_level(2);

    Model sat_model;
    sat_model.Add(NewSatParameters(parameters));
    const CpSolverResponse response = SolveCpModel(model.Build(), &sat_model);
    const bool success = response.status() == CpSolverStatus::OPTIMAL ||
                         response.status() == CpSolverStatus::FEASIBLE;

    JsonValue result = JsonValue::MakeObject();
    result.At("success") = JsonValue::MakeBool(success);
    result.At("status") = JsonValue::MakeString(CpSolverStatus_Name(response.status()));
    result.At("solve_seconds") = JsonValue::MakeNumber(response.wall_time());
    result.At("objective") = JsonValue::MakeNumber(response.objective_value());
    result.At("variables") = JsonValue::MakeNumber(variables.size());
    result.At("constraints") = JsonValue::MakeNumber(model.Build().constraints_size());
    JsonValue quotas = JsonValue::MakeObject();
    if (success) {
        for (int index = 0; index < static_cast<int>(variables.size()); ++index) {
            quotas.At(std::to_string(variables[index].id)) = JsonValue::MakeNumber(
                SolutionIntegerValue(response, quota[index]));
        }
    }
    result.At("quotas") = quotas;
    JsonValue placement_result = JsonValue::MakeObject();
    if (success) {
        for (int index = 0; index < static_cast<int>(variables.size()); ++index) {
            JsonValue times = JsonValue::MakeArray();
            for (int time = 0; time < total_time_slots; ++time) {
                if (SolutionIntegerValue(response, placed[index][time]) != 0) {
                    times.array_value.push_back(JsonValue::MakeNumber(time));
                }
            }
            if (!times.array_value.empty()) {
                placement_result.At(std::to_string(variables[index].id)) = times;
            }
        }
    }
    result.At("placement_witness") = placement_result;
    JsonValue teacher_shortfalls = JsonValue::MakeObject();
    if (success) {
        for (const auto& [teacher_id, deficit] : teacher_deficits) {
            const int value = static_cast<int>(SolutionIntegerValue(response, deficit));
            if (value > 0) {
                teacher_shortfalls.At(std::to_string(teacher_id)) = JsonValue::MakeNumber(value);
            }
        }
    }
    result.At("teacher_shortfalls") = teacher_shortfalls;
    WriteResult(result);
    return success ? 0 : 1;
}
