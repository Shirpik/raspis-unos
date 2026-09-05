#pragma once

#include <array>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "config.h"
#include "json_utils.h"
#include "types.h"

namespace timetable {

struct WorkDayWindow {
    bool enabled = true;
    int start_slot = 1;  // Номера пар в JSON и UI — 1..7.
    int end_slot = SLOTS_PER_DAY;
    // Точный набор допустимых пар. Он позволяет задавать, например, только
    // 2-ю и 4-ю пары без ошибочного разрешения 3-й пары между ними.
    std::set<int> slots;
};

struct WorkSchedule {
    bool has_period = false;
    Date from{0, 0, 0};
    Date to{0, 0, 0};
    std::array<WorkDayWindow, 7> days;
    // Точечная доступность на конкретную дату. Номера пар хранятся как 1..7;
    // пустой набор означает, что преподаватель/группа в эту дату не работает.
    std::map<Date, std::set<int>> date_slot_overrides;
};

struct GroupData {
    int id = 0;
    std::string uid;
    std::string name;
    int parts = 2;
    int size = 0;
    int home_campus = -1;
    int curator_teacher = -1;
    bool class_hour_enabled = true;
    int class_hour_campus = -1;
    int class_hour_room = -1;
    WorkSchedule work_schedule;
};

struct TeacherData {
    int id = 0;
    std::string uid;
    std::string name;
    WorkSchedule work_schedule;
    int default_room = -1;
    std::vector<int> campus_priority;
    // Пустой набор означает, что преподаватель может работать на обеих
    // площадках. Непустой набор является жёстким ограничением, а не весом
    // целевой функции. Это исключает перенос преподавателя с закреплённым
    // кабинетом в другой корпус ради более красивой сетки.
    std::set<int> allowed_campuses;
    std::string room_responsibility;
    // 0 = без дополнительного ограничения. Например, 1 позволяет собрать
    // небольшую недельную нагрузку преподавателя в один выбранный решателем день.
    int max_work_days_per_week = 0;
    // 0 = без ограничения; иначе жёсткий максимум проведённых пар за день.
    int max_pairs_per_day = 0;
    std::map<Date, int> date_minimum_pairs;
    bool scheduling_active = true;
    std::set<Date> class_hour_available_dates;
};

struct RoomData {
    int id = 0;
    std::string uid;
    std::string name;
    int campus = LESNAYA;
    int capacity = 0;
    int room_type = 0;
    std::set<std::string> equipment;
    // Пустой набор означает обычную доступность на всех парах. Если список
    // задан, кабинет можно назначать только на перечисленные номера пар (1–7).
    std::set<int> available_slots;
    // Недельная и календарная доступность кабинета. Нужна, когда аудитория
    // занята школьными уроками или другим постоянным расписанием только в
    // отдельные дни/пары. available_slots остаётся дополнительным глобальным
    // фильтром для всех дней.
    WorkSchedule work_schedule;
    // general — обычная аудитория; exclusive — только для ответственных
    // преподавателей; blocked — помещение не входит в учебный фонд.
    std::string access_mode = "general";
    std::set<int> responsible_teacher_ids;
    // Пустая строка = обычная аудитория; sports_hall = только физкультура.
    std::string purpose;
    bool active = true;
    // An explicit class-hour opening never opens the room for regular lessons.
    bool class_hour_open = false;
    bool class_hour_zero_blocked = false;
    bool class_hour_fallback = false;
};

struct SpecialDayData {
    int id = 0;
    bool all_groups = false;
    int group = -1;
    std::vector<Date> dates;
    std::string text;
};

struct LoadRequirement {
    int teacher = -1;
    std::set<int> lesson_ids;
    int minimum_pairs = 0;
    std::string label;
};

struct ScheduleInputData {
    Date start_date{2026, 1, 12};
    Date end_date{2026, 6, 19};
    bool require_class_hours = false;
    std::vector<LoadRequirement> load_requirements;
    JsonValue semester_readout_report;
    // Минимальная физическая нагрузка преподавателя (в парах) на весь
    // выбранный период генерации. Это не недельная цель.
    std::map<int, int> teacher_period_targets;
    // Теоретические пары, уже проведённые до start_date, по ключу
    // (group, subject_id). Они разблокируют ЛПЗ в новом периоде.
    std::map<std::pair<int, int>, int> prior_theory_pairs;
    std::vector<GroupData> groups;
    std::vector<TeacherData> teachers;
    std::vector<RoomData> rooms;
    std::vector<Lesson> lessons;
    std::map<int, std::vector<std::pair<Date, Date>>> unavailable;
    std::map<int, std::vector<std::pair<Date, Date>>> teacher_unavailable;
    std::map<int, std::map<Date, std::string>> unavailable_day_texts;
    std::vector<SpecialDayData> special_days;
};

std::string DataFilePath();
void EnsureDataFileExists();
JsonValue DefaultDataJson();

bool LoadScheduleInputData(ScheduleInputData& data, std::string& error);
bool LoadScheduleInputDataFromRoot(const JsonValue& root, ScheduleInputData& data, std::string& error, bool update_runtime = true);
bool SaveDataJson(const JsonValue& root, std::string& error, const std::string& reason = "Изменение данных");
std::string ReadDataJsonText();

void NormalizeDataRoot(JsonValue& root);
JsonValue BuildDataAudit(const JsonValue& root);
JsonValue BuildHoursReport(const JsonValue& root, const std::string& schedule_file);
JsonValue BuildTeacherOccupancyReport(const JsonValue& root, const std::string& schedule_file);
std::string BuildSubstitutionsCsv(const JsonValue& root);
JsonValue ListDataVersions();
bool RestoreDataVersion(const std::string& filename, std::string& error);

bool ParseDateIso(const std::string& text, Date& date);
std::string DateToIso(const Date& date);
bool WorkScheduleAllows(const WorkSchedule& schedule, const Date& date, int zero_based_slot);

int NextId(const JsonValue& array_value);
JsonValue* FindObjectById(JsonValue& array_value, int id);
bool RemoveObjectById(JsonValue& array_value, int id);

}  // namespace timetable
