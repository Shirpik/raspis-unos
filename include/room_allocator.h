#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "data_store.h"
#include "json_utils.h"
#include "types.h"

namespace timetable {

struct AssignedRoom {
    int id = -1;
    std::string name;
    int campus = LESNAYA;
    int capacity = 0;
    int room_type = 1;
    bool substituted = false;
    int requested_room_id = -1;
    std::string requested_room_name;
    std::string substitution_reason;
};

using RoomAssignmentMap = std::map<std::int64_t, AssignedRoom>;

struct RoomAllocationResult {
    bool inventory_configured = false;
    int event_count = 0;
    int assigned_events = 0;
    int unassigned_events = 0;
    int substituted_events = 0;
    RoomAssignmentMap assignments;
    JsonValue conflicts = JsonValue::MakeArray();
    JsonValue substitutions = JsonValue::MakeArray();
};

std::int64_t RoomAssignmentKey(int lesson_index, int global_slot);

RoomAllocationResult AllocateRooms(
    const std::vector<Lesson>& lessons,
    const std::vector<GroupData>& groups,
    const std::vector<RoomData>& rooms,
    const std::vector<Date>& all_days,
    const std::vector<std::vector<int>>& x_values,
    const std::vector<std::vector<int>>& group_day_campus
);

JsonValue RoomAllocationToJson(const RoomAllocationResult& result);

}  // namespace timetable
