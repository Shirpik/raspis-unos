#include "room_policy.h"

#include <array>
#include <string>

#include "config.h"

namespace timetable {
namespace {

constexpr int kKalchevskaya = 49;
constexpr int kPodchinennov = 57;
constexpr int kLimonova = 55;
constexpr int kSamtsov = 59;
constexpr int kSamtsovWorkshopA = 64;
constexpr int kSamtsovWorkshopB = 65;
constexpr int kLimonovaWorkshop = 66;
constexpr int kCpde = 68;
const Date kEffectiveFrom{2026, 9, 5};

bool ContainsLpz(const std::string& name) {
    // UTF-8 is case-sensitive and the C locale is not reliable on every
    // Windows installation.  Cover every upper/lower-case spelling of ЛПЗ.
    static const std::array<const char*, 8> spellings = {
        "ЛПЗ", "ЛПз", "ЛпЗ", "Лпз",
        "лПЗ", "лПз", "лпЗ", "лпз",
    };
    for (const char* spelling : spellings) {
        if (name.find(spelling) != std::string::npos) return true;
    }
    return false;
}

bool IsOrdinaryLesnayaClassroom(const RoomData& room) {
    return room.campus == LESNAYA && room.name != "210" &&
        room.access_mode == "general" && room.active;
}

}  // namespace

bool OperationalRoomPolicyAllows(
    const RoomData& room,
    const Lesson& lesson,
    const Date& date
) {
    if (date < kEffectiveFrom) return true;

    const bool is_lpz = ContainsLpz(lesson.name);
    if (lesson.teacher == kLimonova) {
        return is_lpz
            ? room.id == kLimonovaWorkshop
            : IsOrdinaryLesnayaClassroom(room);
    }
    if (lesson.teacher == kSamtsov) {
        return is_lpz
            ? room.id == kSamtsovWorkshopA || room.id == kSamtsovWorkshopB
            : IsOrdinaryLesnayaClassroom(room);
    }
    if ((lesson.teacher == kKalchevskaya || lesson.teacher == kPodchinennov) && is_lpz) {
        return room.id == kCpde;
    }
    return true;
}

}  // namespace timetable
