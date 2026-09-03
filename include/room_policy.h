#pragma once

#include "data_store.h"
#include "types.h"

namespace timetable {

// Operational room rules requested for the timetable beginning on Saturday,
// 5 September 2026.  Friday is intentionally outside the effective range:
// it was approved by the dispatcher and must remain unchanged.
bool OperationalRoomPolicyAllows(
    const RoomData& room,
    const Lesson& lesson,
    const Date& date
);

}  // namespace timetable
