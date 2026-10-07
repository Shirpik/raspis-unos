#include <iostream>
#include <string>

#include "room_policy.h"

namespace {

bool Expect(bool condition, const std::string& message) {
    if (condition) return true;
    std::cerr << "FAILED: " << message << "\n";
    return false;
}

timetable::RoomData Room(int id, const std::string& name, int campus,
                         const std::string& access = "general") {
    timetable::RoomData room;
    room.id = id;
    room.name = name;
    room.campus = campus;
    room.access_mode = access;
    room.active = true;
    return room;
}

timetable::Lesson Lesson(int teacher, const std::string& name) {
    timetable::Lesson lesson{};
    lesson.teacher = teacher;
    lesson.name = name;
    return lesson;
}

}  // namespace

int main() {
    using timetable::OperationalRoomPolicyAllows;
    const timetable::Date friday{2026, 9, 4};
    const timetable::Date saturday{2026, 9, 5};
    const auto ordinary = Room(44, "405", timetable::LESNAYA);
    const auto room210 = Room(34, "210", timetable::LESNAYA);
    const auto limonova_workshop = Room(66, "120-121", timetable::LESNAYA, "exclusive");
    const auto samtsov_workshop = Room(64, "115-116", timetable::LESNAYA, "exclusive");
    const auto cpde = Room(68, "ЦПДЭ", timetable::LESNAYA, "exclusive");

    bool ok = true;
    auto pe = Lesson(8, "Физическая культура");
    pe.required_room_purpose = "sports_hall";
    pe.allowed_campuses = {timetable::KRIVOUSOVA};
    pe.classroom_theory_dates.insert({2026, 10, 10});
    ok &= Expect(pe.RoomPurposeOn({2026, 10, 10}).empty(), "dated PE theory uses a classroom");
    ok &= Expect(pe.CampusesOn({2026, 10, 10}).count(timetable::LESNAYA), "dated theory permits Lesnaya fallback");
    for (const auto date : {timetable::Date{2026, 10, 9}, timetable::Date{2026, 10, 17}}) {
        ok &= Expect(pe.RoomPurposeOn(date) == "sports_hall", "other dates retain the gym requirement");
        ok &= Expect(!pe.CampusesOn(date).count(timetable::LESNAYA), "other dates retain campus restrictions");
    }
    ok &= Expect(pe.teacher == 8, "theory exception retains teacher identity");
    ok &= Expect(OperationalRoomPolicyAllows(cpde, Lesson(57, "МДК лПз"), saturday),
        "Podchinennov LPZ must use CPDE irrespective of marker case");
    ok &= Expect(!OperationalRoomPolicyAllows(ordinary, Lesson(57, "МДК ЛПЗ"), saturday),
        "Podchinennov LPZ must not use an ordinary classroom");
    ok &= Expect(OperationalRoomPolicyAllows(
        limonova_workshop, Lesson(55, "Материаловедение"), friday),
        "approved Friday must stay unchanged");
    ok &= Expect(!OperationalRoomPolicyAllows(
        limonova_workshop, Lesson(55, "Материаловедение"), saturday),
        "Limonova theory must not use a workshop on Saturday");
    ok &= Expect(OperationalRoomPolicyAllows(
        ordinary, Lesson(55, "Материаловедение"), saturday),
        "Limonova theory must use an ordinary Lesnaya classroom");
    ok &= Expect(!OperationalRoomPolicyAllows(
        room210, Lesson(55, "Материаловедение"), saturday),
        "room 210 must remain forbidden");
    ok &= Expect(OperationalRoomPolicyAllows(
        limonova_workshop, Lesson(55, "лпз МДК"), saturday),
        "lower-case LPZ marker must select Limonova workshop");
    ok &= Expect(OperationalRoomPolicyAllows(
        samtsov_workshop, Lesson(59, "ВЛПЗ.04"), saturday),
        "LPZ marker anywhere in Samtsov lesson must select a workshop");
    ok &= Expect(!OperationalRoomPolicyAllows(
        samtsov_workshop, Lesson(59, "МДК.04"), saturday),
        "Samtsov theory must not use a workshop");
    ok &= Expect(OperationalRoomPolicyAllows(
        cpde, Lesson(49, "ЛПЗ аналитическая химия"), saturday),
        "Kalchevskaya LPZ must use CPDE");
    ok &= Expect(!OperationalRoomPolicyAllows(
        ordinary, Lesson(49, "ЛПЗ аналитическая химия"), saturday),
        "Kalchevskaya LPZ must not use an ordinary classroom");

    if (!ok) return 1;
    std::cout << "room_policy_cpp_regression: passed\n";
    return 0;
}
