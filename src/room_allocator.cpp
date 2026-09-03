#include "room_allocator.h"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>

#include "config.h"
#include "date_utils.h"
#include "format_utils.h"
#include "room_policy.h"

namespace timetable {
namespace {

struct Event {
    int lesson = -1;
    int teacher = -1;
    int start = 0;
    int duration = 1;
    int campus = LESNAYA;
    int required_capacity = 0;
    std::vector<int> candidates;
};

bool IsAvailableAt(
    const RoomData& room,
    const std::vector<Date>& all_days,
    int global_slot,
    int duration = 1
) {
    if (!room.active || room.access_mode == "blocked") return false;
    for (int dt = 0; dt < duration; ++dt) {
        const int occupied_slot = global_slot + dt;
        const int day = occupied_slot / SLOTS_PER_DAY;
        const int zero_based_pair = occupied_slot % SLOTS_PER_DAY;
        if (day < 0 || day >= static_cast<int>(all_days.size())) return false;
        if (!WorkScheduleAllows(room.work_schedule, all_days[day], zero_based_pair)) return false;
        const int pair_number = zero_based_pair + 1;
        if (!room.available_slots.empty() && !room.available_slots.count(pair_number)) return false;
    }
    return true;
}

bool CanTeacherUseRoom(const RoomData& room, int teacher) {
    if (!room.active || room.access_mode == "blocked") return false;
    if (room.access_mode != "exclusive") return true;
    return teacher >= 0 && room.responsible_teacher_ids.count(teacher) > 0;
}

bool MatchesRoomPurpose(const RoomData& room, const Lesson& lesson) {
    return room.purpose == lesson.required_room_purpose;
}

bool MatchesRoomFeatures(const RoomData& room, const Lesson& lesson) {
    if (lesson.required_room_type > 0 && room.room_type != lesson.required_room_type)
        return false;
    for (const std::string& required : lesson.required_equipment) {
        if (!room.equipment.count(required)) return false;
    }
    return true;
}

std::string ConflictReason(
    const Event& event,
    const Lesson& lesson,
    const std::vector<RoomData>& rooms,
    const std::vector<Date>& all_days
) {
    bool has_campus = false;
    bool has_capacity = false;
    bool has_access = false;
    bool has_purpose = false;
    bool has_features = false;
    bool has_operational_policy = false;
    bool has_fixed = lesson.fixed_room < 0;
    for (const RoomData& room : rooms) {
        if (!IsAvailableAt(room, all_days, event.start, event.duration) || room.campus != event.campus) continue;
        has_campus = true;
        if (!CanTeacherUseRoom(room, event.teacher)) continue;
        has_access = true;
        const Date& event_date = all_days[event.start / SLOTS_PER_DAY];
        if (!OperationalRoomPolicyAllows(room, lesson, event_date)) continue;
        has_operational_policy = true;
        if (!MatchesRoomPurpose(room, lesson)) continue;
        has_purpose = true;
        if (!MatchesRoomFeatures(room, lesson)) continue;
        has_features = true;
        if (lesson.fixed_room >= 0 && room.id == lesson.fixed_room) has_fixed = true;
        if (lesson.fixed_room >= 0 && !lesson.allow_room_substitution && room.id != lesson.fixed_room) continue;
        if (room.capacity > 0 && event.required_capacity > room.capacity) continue;
        has_capacity = true;
    }
    if (!has_campus) return "В выбранном корпусе нет активных аудиторий";
    if (!has_access) return "В выбранном корпусе нет аудитории, разрешённой этому преподавателю";
    if (!has_operational_policy) return "Нет аудитории, разрешённой оперативным правилом для этого вида занятия";
    if (!has_purpose) return "В выбранном корпусе нет аудитории нужного назначения";
    if (!has_features) return "Нет аудитории нужного типа или с требуемым оборудованием";
    if (!has_fixed && !lesson.allow_room_substitution) return "Закреплённая аудитория не найдена или находится в другом корпусе";
    if (!has_capacity) return "Нет аудитории подходящей вместимости";
    return "Все доступные аудитории корпуса заняты в это время";
}

}  // namespace

std::int64_t RoomAssignmentKey(int lesson_index, int global_slot) {
    return (static_cast<std::int64_t>(lesson_index) << 32) |
        static_cast<std::uint32_t>(global_slot);
}

RoomAllocationResult AllocateRooms(
    const std::vector<Lesson>& lessons,
    const std::vector<GroupData>& groups,
    const std::vector<RoomData>& rooms,
    const std::vector<Date>& all_days,
    const std::vector<std::vector<int>>& x_values,
    const std::vector<std::vector<int>>& group_day_campus
) {
    RoomAllocationResult result;
    std::vector<int> active_rooms;
    for (int r = 0; r < static_cast<int>(rooms.size()); r++) {
        if (rooms[r].active && rooms[r].access_mode != "blocked") active_rooms.push_back(r);
    }
    result.inventory_configured = !active_rooms.empty();

    std::map<int, int> group_sizes;
    for (const GroupData& group : groups) group_sizes[group.id] = group.size;

    // Не занимать закреплённый кабинет другого преподавателя, пока есть общий.
    // Это предотвращает каскад автозамен ещё до обработки его собственных пар.
    std::map<int, std::set<int>> room_owner_teachers;
    for (const RoomData& room : rooms) {
        if (!room.responsible_teacher_ids.empty())
            room_owner_teachers[room.id].insert(
                room.responsible_teacher_ids.begin(), room.responsible_teacher_ids.end());
    }
    for (const Lesson& lesson : lessons) {
        if (lesson.teacher >= 0 && lesson.preferred_room >= 0)
            room_owner_teachers[lesson.preferred_room].insert(lesson.teacher);
    }

    const int total_slots = static_cast<int>(all_days.size()) * SLOTS_PER_DAY;
    std::vector<Event> events;
    for (int l = 0; l < static_cast<int>(lessons.size()) && l < static_cast<int>(x_values.size()); l++) {
        for (int t = 0; t < total_slots && t < static_cast<int>(x_values[l].size()); t++) {
            if (!x_values[l][t]) continue;
            const bool grouped_pair = lessons[l].is_block || lessons[l].consecutive_pairs == 2;
            if (grouped_pair && t > 0 && t % SLOTS_PER_DAY > 0 && x_values[l][t - 1]) continue;

            Event event;
            event.lesson = l;
            event.teacher = lessons[l].teacher;
            event.start = t;
            event.duration = 1;
            if (grouped_pair) {
                while (t + event.duration < total_slots &&
                       (t + event.duration) / SLOTS_PER_DAY == t / SLOTS_PER_DAY &&
                       x_values[l][t + event.duration]) {
                    event.duration++;
                }
            }
            const int day = t / SLOTS_PER_DAY;
            const int group = lessons[l].group;
            if (group >= 0 && group < static_cast<int>(group_day_campus.size()) &&
                day < static_cast<int>(group_day_campus[group].size())) {
                event.campus = group_day_campus[group][day];
            } else if (lessons[l].allowed_campuses.size() == 1) {
                event.campus = static_cast<int>(*lessons[l].allowed_campuses.begin());
            }
            event.required_capacity = lessons[l].required_capacity;
            if (event.required_capacity <= 0 && group_sizes.count(group)) {
                event.required_capacity = group_sizes[group];
            }

            for (int r : active_rooms) {
                const RoomData& room = rooms[r];
                if (!CanTeacherUseRoom(room, event.teacher)) continue;
                if (!OperationalRoomPolicyAllows(
                        room, lessons[l], all_days[event.start / SLOTS_PER_DAY])) continue;
                if (!MatchesRoomPurpose(room, lessons[l])) continue;
                if (!MatchesRoomFeatures(room, lessons[l])) continue;
                if (!IsAvailableAt(room, all_days, event.start, event.duration)) continue;
                if (room.campus != event.campus) continue;
                if (lessons[l].fixed_room >= 0 && !lessons[l].allow_room_substitution && room.id != lessons[l].fixed_room) continue;
                if (room.capacity > 0 && event.required_capacity > room.capacity) continue;
                event.candidates.push_back(r);
            }
            std::sort(event.candidates.begin(), event.candidates.end(), [&](int a, int b) {
                const bool fixed_a = rooms[a].id == lessons[l].fixed_room;
                const bool fixed_b = rooms[b].id == lessons[l].fixed_room;
                if (fixed_a != fixed_b) return fixed_a;
                const bool preferred_a = rooms[a].id == lessons[l].preferred_room;
                const bool preferred_b = rooms[b].id == lessons[l].preferred_room;
                if (preferred_a != preferred_b) return preferred_a;
                const auto owned_by_teacher = [&](int room_index) {
                    const auto it = room_owner_teachers.find(rooms[room_index].id);
                    return it != room_owner_teachers.end() && it->second.count(lessons[l].teacher);
                };
                const bool own_a = owned_by_teacher(a);
                const bool own_b = owned_by_teacher(b);
                if (own_a != own_b) return own_a;
                const auto owned_by_other = [&](int room_index) {
                    const auto it = room_owner_teachers.find(rooms[room_index].id);
                    return it != room_owner_teachers.end() && !it->second.empty() &&
                        !it->second.count(lessons[l].teacher);
                };
                const bool foreign_a = owned_by_other(a);
                const bool foreign_b = owned_by_other(b);
                if (foreign_a != foreign_b) return !foreign_a;
                const int need = event.required_capacity;
                const int waste_a = rooms[a].capacity > 0 ? rooms[a].capacity - need : 1000000;
                const int waste_b = rooms[b].capacity > 0 ? rooms[b].capacity - need : 1000000;
                return waste_a != waste_b ? waste_a < waste_b : rooms[a].id < rooms[b].id;
            });
            events.push_back(std::move(event));
        }
    }

    result.event_count = static_cast<int>(events.size());

    // Глобально подбираем преподавателю один кабинет на весь период в каждом
    // корпусе. Два преподавателя могут делить кабинет, только если их занятия
    // никогда не идут одновременно. Это существенно сильнее локального greedy
    // и выполняет требование «одна аудитория, кроме неизбежных конфликтов».
    using TeacherCampusKey = std::pair<int, int>;
    std::map<TeacherCampusKey, std::set<int>> teacher_occupied_slots;
    std::map<TeacherCampusKey, int> teacher_required_capacity;
    std::map<TeacherCampusKey, int> teacher_preferred_room_id;
    std::map<TeacherCampusKey, int> teacher_hard_room_id;
    std::map<TeacherCampusKey, std::set<int>> teacher_requested_room_ids;
    std::map<TeacherCampusKey, std::set<std::string>> teacher_room_purposes;
    std::map<TeacherCampusKey, std::vector<int>> teacher_events;
    for (const Event& event : events) {
        if (event.teacher < 0) continue;
        const TeacherCampusKey key{event.teacher, event.campus};
        for (int dt = 0; dt < event.duration; ++dt)
            teacher_occupied_slots[key].insert(event.start + dt);
        teacher_required_capacity[key] = std::max(
            teacher_required_capacity[key], event.required_capacity);
        const Lesson& lesson = lessons[event.lesson];
        teacher_events[key].push_back(event.lesson);
        teacher_room_purposes[key].insert(lesson.required_room_purpose);
        if (lesson.preferred_room >= 0)
            teacher_preferred_room_id.emplace(key, lesson.preferred_room);
        if (lesson.fixed_room >= 0 && !lesson.allow_room_substitution)
            teacher_hard_room_id.emplace(key, lesson.fixed_room);
        const int requested_room = lesson.fixed_room >= 0
            ? lesson.fixed_room : lesson.preferred_room;
        if (requested_room >= 0)
            teacher_requested_room_ids[key].insert(requested_room);
    }

    std::vector<TeacherCampusKey> teacher_keys;
    for (const auto& [key, slots] : teacher_occupied_slots) teacher_keys.push_back(key);
    std::sort(teacher_keys.begin(), teacher_keys.end(), [&](const TeacherCampusKey& a, const TeacherCampusKey& b) {
        const bool preferred_a = teacher_preferred_room_id.count(a) || teacher_hard_room_id.count(a);
        const bool preferred_b = teacher_preferred_room_id.count(b) || teacher_hard_room_id.count(b);
        if (preferred_a != preferred_b) return preferred_a;
        if (teacher_occupied_slots[a].size() != teacher_occupied_slots[b].size())
            return teacher_occupied_slots[a].size() > teacher_occupied_slots[b].size();
        return a < b;
    });

    std::map<TeacherCampusKey, int> teacher_stable_room;
    std::map<int, std::set<int>> stable_room_busy_slots;
    std::map<int, int> stable_room_teacher_count;
    for (const TeacherCampusKey& key : teacher_keys) {
        std::vector<int> candidates;
        // Разные явные кабинеты у одного преподавателя означают разные виды
        // занятий (например, теория в аудитории и ЛПЗ в мастерской). В этом
        // случае нельзя распространять один «стабильный» кабинет на все пары.
        if (teacher_requested_room_ids[key].size() > 1) continue;
        // Если один преподаватель ведёт предметы с разным назначением
        // аудитории, единый стабильный кабинет ему назначать нельзя.
        if (teacher_room_purposes[key].size() != 1) continue;
        const std::string required_purpose = *teacher_room_purposes[key].begin();
        for (int r : active_rooms) {
            const RoomData& room = rooms[r];
            if (!CanTeacherUseRoom(room, key.first)) continue;
            if (room.purpose != required_purpose) continue;
            if (room.campus != key.second) continue;
            bool matches_all_features = true;
            for (int lesson_index : teacher_events[key]) {
                if (!MatchesRoomFeatures(room, lessons[lesson_index])) {
                    matches_all_features = false;
                    break;
                }
            }
            if (!matches_all_features) continue;
            const auto hard = teacher_hard_room_id.find(key);
            if (hard != teacher_hard_room_id.end() && room.id != hard->second) continue;
            if (room.capacity > 0 && teacher_required_capacity[key] > room.capacity) continue;
            bool available = true;
            for (int slot : teacher_occupied_slots[key]) {
                if (!IsAvailableAt(room, all_days, slot)) { available = false; break; }
                if (stable_room_busy_slots[r].count(slot)) { available = false; break; }
            }
            if (available) candidates.push_back(r);
        }
        const int preferred_id = teacher_hard_room_id.count(key)
            ? teacher_hard_room_id[key]
            : (teacher_preferred_room_id.count(key) ? teacher_preferred_room_id[key] : -1);
        std::sort(candidates.begin(), candidates.end(), [&](int a, int b) {
            const bool preferred_a = rooms[a].id == preferred_id;
            const bool preferred_b = rooms[b].id == preferred_id;
            if (preferred_a != preferred_b) return preferred_a;
            const auto own_room = [&](int room_index) {
                const auto owner = room_owner_teachers.find(rooms[room_index].id);
                return owner != room_owner_teachers.end() && owner->second.count(key.first);
            };
            const bool own_a = own_room(a);
            const bool own_b = own_room(b);
            if (own_a != own_b) return own_a;
            const auto foreign_owner = [&](int room_index) {
                const auto owner = room_owner_teachers.find(rooms[room_index].id);
                return owner != room_owner_teachers.end() && !owner->second.empty() &&
                    !owner->second.count(key.first);
            };
            const bool foreign_a = foreign_owner(a);
            const bool foreign_b = foreign_owner(b);
            if (foreign_a != foreign_b) return !foreign_a;
            const auto room_load = [&](int room_index) {
                const auto it = stable_room_teacher_count.find(room_index);
                return it == stable_room_teacher_count.end() ? 0 : it->second;
            };
            if (room_load(a) != room_load(b)) return room_load(a) < room_load(b);
            return rooms[a].id < rooms[b].id;
        });
        if (candidates.empty()) continue;
        const int chosen = candidates.front();
        teacher_stable_room[key] = chosen;
        stable_room_teacher_count[chosen]++;
        stable_room_busy_slots[chosen].insert(
            teacher_occupied_slots[key].begin(), teacher_occupied_slots[key].end());
    }

    std::map<int, std::set<int>> protected_rooms_by_start;
    for (const Event& event : events) {
        const Lesson& lesson = lessons[event.lesson];
        const int requested = lesson.fixed_room >= 0 ? lesson.fixed_room : lesson.preferred_room;
        if (requested >= 0) protected_rooms_by_start[event.start].insert(requested);
    }
    std::sort(events.begin(), events.end(), [&](const Event& a, const Event& b) {
        if (a.start != b.start) return a.start < b.start;
        const Lesson& lesson_a = lessons[a.lesson];
        const Lesson& lesson_b = lessons[b.lesson];
        const bool owned_a = lesson_a.fixed_room >= 0 || lesson_a.preferred_room >= 0;
        const bool owned_b = lesson_b.fixed_room >= 0 || lesson_b.preferred_room >= 0;
        if (owned_a != owned_b) return owned_a;
        if (a.candidates.size() != b.candidates.size()) return a.candidates.size() < b.candidates.size();
        if (a.teacher != b.teacher) return a.teacher < b.teacher;
        return a.duration > b.duration;
    });

    // room_busy[room index] хранит занятые глобальные слоты. Двухпарные УП
    // резервируют одну и ту же аудиторию сразу на оба слота.
    std::vector<std::set<int>> room_busy(rooms.size());
    // Один основной кабинет на преподавателя и корпус. Если преподаватель
    // работает в двух корпусах, в каждом сохраняется свой стабильный кабинет.
    std::map<std::pair<int, int>, int> teacher_primary_room;
    std::map<int, std::set<int>> room_primary_teachers;
    for (const Event& event : events) {
        int chosen = -1;
        std::vector<int> ranked_candidates = event.candidates;
        const Lesson& event_lesson = lessons[event.lesson];
        const TeacherCampusKey teacher_campus{event.teacher, event.campus};
        const auto primary_it = teacher_primary_room.find(teacher_campus);
        const auto stable_it = teacher_stable_room.find(teacher_campus);
        const int primary_room = stable_it != teacher_stable_room.end()
            ? stable_it->second
            : (primary_it == teacher_primary_room.end() ? -1 : primary_it->second);
        std::stable_sort(ranked_candidates.begin(), ranked_candidates.end(), [&](int a, int b) {
            const auto rank = [&](int room_index) {
                const int room_id = rooms[room_index].id;
                if (event_lesson.fixed_room >= 0 && room_id == event_lesson.fixed_room) return 0;
                if (primary_room >= 0 && room_index == primary_room) return 1;
                if (event_lesson.preferred_room >= 0 && room_id == event_lesson.preferred_room) return 2;
                const auto documented_owner = room_owner_teachers.find(room_id);
                if (documented_owner != room_owner_teachers.end() &&
                    documented_owner->second.count(event.teacher)) return 3;
                const auto dynamic_owner = room_primary_teachers.find(room_index);
                if (dynamic_owner != room_primary_teachers.end() && !dynamic_owner->second.empty() &&
                    !dynamic_owner->second.count(event.teacher)) return 6;
                const auto owner = room_owner_teachers.find(room_id);
                if (owner != room_owner_teachers.end() && !owner->second.empty() &&
                    !owner->second.count(event.teacher)) return 5;
                return 4;
            };
            return rank(a) < rank(b);
        });
        const int own_requested = event_lesson.fixed_room >= 0
            ? event_lesson.fixed_room : event_lesson.preferred_room;
        const auto choose_free = [&](bool protect_active_owner_rooms) {
            for (int r : ranked_candidates) {
                if (protect_active_owner_rooms && rooms[r].id != own_requested &&
                    protected_rooms_by_start[event.start].count(rooms[r].id)) continue;
                bool free = true;
                for (int dt = 0; dt < event.duration; dt++) {
                    if (room_busy[r].count(event.start + dt)) { free = false; break; }
                }
                if (free) return r;
            }
            return -1;
        };
        chosen = choose_free(true);
        // Защита — сильный приоритет, а не причина оставить занятие без кабинета.
        if (chosen < 0) chosen = choose_free(false);

        if (chosen >= 0) {
            const RoomData& room = rooms[chosen];
            const Lesson& lesson = lessons[event.lesson];
            // The teacher's default room is a soft allocation preference, not
            // an explicit room request for every lesson.  Report an automatic
            // replacement only when a dispatcher fixed this lesson to a room.
            const int requested_room_id = lesson.fixed_room;
            std::string requested_room_name;
            int requested_index = -1;
            for (int r = 0; r < static_cast<int>(rooms.size()); ++r) {
                if (rooms[r].id == requested_room_id) {
                    requested_index = r;
                    requested_room_name = rooms[r].name;
                    break;
                }
            }
            const bool substituted = requested_room_id >= 0 && room.id != requested_room_id;
            std::string substitution_reason;
            if (substituted) {
                const bool requested_is_candidate = requested_index >= 0 &&
                    std::find(event.candidates.begin(), event.candidates.end(), requested_index) != event.candidates.end();
                bool requested_is_busy = false;
                if (requested_is_candidate) {
                    for (int dt = 0; dt < event.duration; ++dt) {
                        if (room_busy[requested_index].count(event.start + dt)) {
                            requested_is_busy = true;
                            break;
                        }
                    }
                }
                substitution_reason = requested_is_busy
                    ? "Закреплённый кабинет занят в это время"
                    : "Закреплённый кабинет недоступен в этом корпусе или не вмещает группу";
            }
            AssignedRoom assigned;
            assigned.id = room.id;
            assigned.name = room.name;
            assigned.campus = room.campus;
            assigned.capacity = room.capacity;
            assigned.room_type = room.room_type;
            assigned.substituted = substituted;
            assigned.requested_room_id = requested_room_id;
            assigned.requested_room_name = requested_room_name;
            assigned.substitution_reason = substitution_reason;
            for (int dt = 0; dt < event.duration; dt++) {
                room_busy[chosen].insert(event.start + dt);
                result.assignments[RoomAssignmentKey(event.lesson, event.start + dt)] = assigned;
            }
            if (event.teacher >= 0 && stable_it == teacher_stable_room.end() &&
                primary_it == teacher_primary_room.end()) {
                teacher_primary_room[teacher_campus] = chosen;
                room_primary_teachers[chosen].insert(event.teacher);
            }
            result.assigned_events++;
            if (substituted) {
                result.substituted_events++;
                const int day = event.start / SLOTS_PER_DAY;
                JsonValue change = JsonValue::MakeObject();
                change.At("lesson_id") = JsonValue::MakeNumber(lesson.id);
                change.At("lesson_uid") = JsonValue::MakeString(lesson.uid);
                change.At("lesson_name") = JsonValue::MakeString(lesson.name);
                change.At("group_id") = JsonValue::MakeNumber(lesson.group);
                change.At("date") = JsonValue::MakeString(day < static_cast<int>(all_days.size()) ? DateToIso(all_days[day]) : "");
                change.At("slot") = JsonValue::MakeNumber(event.start % SLOTS_PER_DAY + 1);
                change.At("requested_room_id") = JsonValue::MakeNumber(requested_room_id);
                change.At("requested_room_name") = JsonValue::MakeString(requested_room_name);
                change.At("assigned_room_id") = JsonValue::MakeNumber(room.id);
                change.At("assigned_room_name") = JsonValue::MakeString(room.name);
                change.At("room_type") = JsonValue::MakeNumber(room.room_type);
                change.At("reason") = JsonValue::MakeString(substitution_reason);
                result.substitutions.array_value.push_back(change);
            }
            continue;
        }

        result.unassigned_events++;
        const Lesson& lesson = lessons[event.lesson];
        const int day = event.start / SLOTS_PER_DAY;
        JsonValue issue = JsonValue::MakeObject();
        issue.At("lesson_id") = JsonValue::MakeNumber(lesson.id);
        issue.At("lesson_uid") = JsonValue::MakeString(lesson.uid);
        issue.At("lesson_name") = JsonValue::MakeString(lesson.name);
        issue.At("group_id") = JsonValue::MakeNumber(lesson.group);
        issue.At("date") = JsonValue::MakeString(day < static_cast<int>(all_days.size())
            ? DateToIso(all_days[day]) : "");
        issue.At("slot") = JsonValue::MakeNumber(event.start % SLOTS_PER_DAY + 1);
        issue.At("campus") = JsonValue::MakeNumber(event.campus);
        issue.At("reason") = JsonValue::MakeString(ConflictReason(event, lesson, rooms, all_days));
        result.conflicts.array_value.push_back(issue);
    }

    return result;
}

JsonValue RoomAllocationToJson(const RoomAllocationResult& result) {
    JsonValue root = JsonValue::MakeObject();
    root.At("inventory_configured") = JsonValue::MakeBool(result.inventory_configured);
    root.At("events") = JsonValue::MakeNumber(result.event_count);
    root.At("assigned") = JsonValue::MakeNumber(result.assigned_events);
    root.At("unassigned") = JsonValue::MakeNumber(result.unassigned_events);
    root.At("substituted") = JsonValue::MakeNumber(result.substituted_events);
    root.At("conflicts") = result.conflicts;
    root.At("substitutions") = result.substitutions;
    return root;
}

}  // namespace timetable
