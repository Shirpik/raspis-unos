#include "scheduler.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "ortools/sat/cp_model.h"
#include "ortools/sat/cp_model_solver.h"
#include "ortools/sat/cp_model.pb.h"
#include "ortools/sat/model.h"
#include "ortools/sat/sat_parameters.pb.h"
#include "ortools/util/time_limit.h"

#include "config.h"
#include "data_store.h"
#include "date_utils.h"
#include "diagnostics.h"
#include "format_utils.h"
#include "lessons_data.h"
#include "model_utils.h"
#include "output_writers.h"
#include "room_allocator.h"
#include "runtime_config.h"
#include "scheduler_rules.h"
#include "types.h"

namespace timetable {

using operations_research::Domain;
using operations_research::sat::BoolVar;
using operations_research::sat::CpModelBuilder;
using operations_research::sat::CpModelProto;
using operations_research::sat::CpSolverResponse;
using operations_research::sat::CpSolverResponseStats;
using operations_research::sat::CpSolverStatus;
using operations_research::sat::CpSolverStatus_Name;
using operations_research::sat::IntVar;
using operations_research::sat::LinearExpr;
using operations_research::sat::NewSatParameters;
using operations_research::sat::SatParameters;
using operations_research::sat::SolveCpModel;

struct UpStartRef {
    int block_index;
    int start_index;
    int teacher;
    int start_t;
    TimeInterval interval;
};

// ── ПП (производственная практика): детерминированная расстановка до CP-SAT ──
// Один ПП-слот = одна пара. Урок ПП занимает последние ceil(total_slots/3)
// доступных дней группы по 3 пары/день (последний день может быть неполным).
// Несколько ПП одной группы выстраиваются в очередь по id: урок с меньшим id
// идёт раньше (занимает более ранние дни хвоста).
struct PpPlacement {
    int lesson_index;  // индекс в lessons
    int global_day;    // индекс дня в all_days
    int slot;          // слот внутри дня (0..SLOTS_PER_DAY-1)
};

struct PpPlan {
    std::vector<PpPlacement> placements;
    // group → однодневные диапазоны [d,d] дней, занятых ПП (блокируют УП и
    // исключают неделю из делителя Брезенхема, если занята целиком)
    std::map<int, std::vector<std::pair<Date, Date>>> pp_block;
};

static PpPlan ComputePpPlan(
    const std::vector<Lesson>& lessons,
    const std::vector<Date>& all_days,
    const std::map<int, std::vector<std::pair<Date, Date>>>& unavailable
) {
    const int num_lessons = static_cast<int>(lessons.size());
    const int num_days = static_cast<int>(all_days.size());

    PpPlan plan;

    for (int g = 0; g < GROUPS; g++) {
        // ПП-уроки группы, отсортированные по id (очередь)
        std::vector<int> pp_lessons;
        for (int l = 0; l < num_lessons; l++)
            if (lessons[l].is_pp && lessons[l].group == g) pp_lessons.push_back(l);
        if (pp_lessons.empty()) continue;
        std::sort(pp_lessons.begin(), pp_lessons.end(),
                  [&](int a, int b) { return lessons[a].id < lessons[b].id; });

        // Доступные дни группы по возрастанию (по исходной карте недоступности)
        std::vector<int> avail;
        for (int d = 0; d < num_days; d++)
            if (IsAvailable(all_days[d], g, unavailable)) avail.push_back(d);
        if (avail.empty()) continue;

        // Сколько дней нужно всем ПП группы (3 пары/день)
        int need_days = 0;
        for (int l : pp_lessons) need_days += CeilDiv(lessons[l].total_slots, 3);

        int avail_n = static_cast<int>(avail.size());
        int start_idx = std::max(0, avail_n - need_days);
        if (avail_n < need_days) {
            std::cout << "  [ПП] предупреждение: группе " << GROUP_NAME[g]
                      << " нужно " << need_days << " дней под ПП, доступно "
                      << avail_n << " — расстановка усечена\n";
        }

        // Раздаём дни хвоста урокам по очереди, по 3 пары/день
        int cursor = start_idx;  // позиция в avail
        for (int l : pp_lessons) {
            int remaining = lessons[l].total_slots;
            while (remaining > 0 && cursor < avail_n) {
                int d = avail[cursor++];
                int pairs_today = std::min(3, remaining);
                for (int s = 0; s < pairs_today; s++) {
                    plan.placements.push_back(PpPlacement{l, d, s});
                }
                plan.pp_block[g].push_back({all_days[d], all_days[d]});
                remaining -= pairs_today;
            }
        }
    }

    return plan;
}

// Объединяет исходную карту недоступности с однодневными ПП-диапазонами.
static std::map<int, std::vector<std::pair<Date, Date>>> MergeUnavailable(
    const std::map<int, std::vector<std::pair<Date, Date>>>& base,
    const std::map<int, std::vector<std::pair<Date, Date>>>& extra
) {
    std::map<int, std::vector<std::pair<Date, Date>>> merged = base;
    for (const auto& kv : extra) {
        auto& dst = merged[kv.first];
        dst.insert(dst.end(), kv.second.begin(), kv.second.end());
    }
    return merged;
}

static bool DateInUnavailableRanges(
    const Date& date,
    int entity_id,
    const std::map<int, std::vector<std::pair<Date, Date>>>& unavailable
) {
    auto it = unavailable.find(entity_id);
    if (it == unavailable.end()) return false;
    for (const auto& range : it->second) {
        if (range.first <= date && date <= range.second) return true;
    }
    return false;
}

static bool LessonAllowsWeek(const Lesson& lesson, int dense_week_index) {
    if (lesson.week_parity == "odd") return ((dense_week_index + 1) % 2) == 1;
    if (lesson.week_parity == "even") return ((dense_week_index + 1) % 2) == 0;
    return true;
}

// В течение одного дня действуют два одновременных предела одного предмета:
//   1) общегрупповых пар не больше whole_group_max_pairs;
//   2) в календаре каждой физической подгруппы не больше part_max_pairs.
// Второй предел включает общегрупповые пары, но не складывает параллельную
// вторую подгруппу. Поэтому допустим сценарий 3+3 по подгруппам, а три занятия
// всей группы запрещены. Предмет определяется по subject_id (с fallback на имя),
// так что теория и ЛПЗ одной дисциплины больше не обходят общий предел.
static void AddMaxSameSubjectPerDay(
    CpModelBuilder& model,
    const std::vector<Lesson>& lessons,
    const std::vector<std::vector<BoolVar>>& x,
    int num_days,
    const std::vector<bool>& active,
    int whole_group_max_pairs,
    int part_max_pairs
) {
    using PartSubjectKey = std::tuple<int, int, std::string>;
    using WholeSubjectKey = std::pair<int, std::string>;
    std::map<PartSubjectKey, std::vector<int>> part_subject_lessons;
    std::map<WholeSubjectKey, std::vector<int>> whole_subject_lessons;
    for (int l = 0; l < static_cast<int>(lessons.size()); l++) {
        if (l >= static_cast<int>(active.size()) || !active[l] || lessons[l].is_pp) continue;
        const int group = lessons[l].group;
        if (group < 0 || group >= GROUPS) continue;
        const std::string subject = SubjectFamilyKey(lessons[l]);
        if (lessons[l].subgroup == -1) {
            whole_subject_lessons[{group, subject}].push_back(l);
        }
        for (int part = 0; part < PARTS_PER_GROUP; part++) {
            if (LessonAffectsPart(lessons[l], group, part)) {
                part_subject_lessons[{group, part, subject}].push_back(l);
            }
        }
    }

    const auto add_daily_limits = [&](const auto& families, int configured_max) {
        const int limit = std::clamp(configured_max, 1, SLOTS_PER_DAY);
        for (const auto& [key, indices] : families) {
            (void)key;
            for (int day = 0; day < num_days; day++) {
                LinearExpr daily_subject_pairs;
                for (int slot = 0; slot < SLOTS_PER_DAY; slot++) {
                    const int t = day * SLOTS_PER_DAY + slot;
                    for (int l : indices) daily_subject_pairs += x[l][t];
                }
                model.AddLessOrEqual(daily_subject_pairs, limit);
            }
        }
    };

    add_daily_limits(whole_subject_lessons, whole_group_max_pairs);
    add_daily_limits(part_subject_lessons, part_max_pairs);
}

static void WriteBackendReports(
    const std::filesystem::path& out_dir,
    const std::vector<Lesson>& lessons,
    const std::vector<std::vector<int>>& x_values,
    int student_windows,
    int teacher_windows,
    int max_student_pairs,
    int student_days_at_daily_limit,
    int five_pair_days,
    int up_day_violations,
    int up_teacher_lock_violations,
    const RoomAllocationResult& room_allocation
) {
    {
        std::ofstream out(out_dir / "room_allocation.json", std::ios::binary | std::ios::trunc);
        if (out) out << ToJson(RoomAllocationToJson(room_allocation), 2);
    }

    const ScheduleLoadSummary load = ComputeScheduleLoadSummary(lessons, x_values);

    JsonValue quality = JsonValue::MakeObject();
    quality.At("student_windows") = JsonValue::MakeNumber(student_windows);
    quality.At("teacher_windows") = JsonValue::MakeNumber(teacher_windows);
    quality.At("max_student_pairs_per_day") = JsonValue::MakeNumber(max_student_pairs);
    quality.At("configured_student_daily_limit") = JsonValue::MakeNumber(MAX_STUDENT_PAIRS_PER_DAY);
    quality.At("student_days_at_daily_limit") = JsonValue::MakeNumber(student_days_at_daily_limit);
    quality.At("five_pair_student_days") = JsonValue::MakeNumber(five_pair_days);
    quality.At("up_day_rule_violations") = JsonValue::MakeNumber(up_day_violations);
    quality.At("up_teacher_lock_violations") = JsonValue::MakeNumber(up_teacher_lock_violations);
    quality.At("planned_hours") = JsonValue::MakeNumber(load.planned_hours);
    quality.At("scheduled_hours") = JsonValue::MakeNumber(load.scheduled_hours);
    quality.At("missing_hours") = JsonValue::MakeNumber(load.missing_hours);
    quality.At("excess_hours") = JsonValue::MakeNumber(load.excess_hours);
    quality.At("mismatched_lessons") = JsonValue::MakeNumber(load.mismatched_lessons);
    quality.At("remaining_hours") = JsonValue::MakeNumber(load.remaining_hours);
    quality.At("load_matches_plan_exactly") = JsonValue::MakeBool(load.complete());
    quality.At("completion_percent") = JsonValue::MakeNumber(
        load.planned_hours > 0
            ? 100.0 * (load.planned_hours - load.missing_hours) / load.planned_hours
            : (load.excess_hours == 0 ? 100.0 : 0.0));
    quality.At("rooms") = RoomAllocationToJson(room_allocation);

    std::ofstream out(out_dir / "quality_report.json", std::ios::binary | std::ios::trunc);
    if (out) out << ToJson(quality, 2);
}

static void WriteSolverMetricsReport(
    const std::filesystem::path& output_dir,
    const JsonValue& weeks,
    const std::string& state,
    double total_seconds
) {
    JsonValue metrics = JsonValue::MakeObject();
    metrics.At("mode") = JsonValue::MakeString("weekly");
    metrics.At("state") = JsonValue::MakeString(state);
    metrics.At("total_seconds") = JsonValue::MakeNumber(total_seconds);
    metrics.At("quality_improvement_seconds") = JsonValue::MakeNumber(
        g_solver_config.quality_improvement_seconds);
    metrics.At("weeks") = weeks;
    std::ofstream out(output_dir / "solver_metrics.json", std::ios::binary | std::ios::trunc);
    if (out) out << ToJson(metrics, 2);
}

GenerationResult GenerateSchedule(const std::string& output_dir) {
    GenerationOptions empty_opts;
    return GenerateSchedule(output_dir, empty_opts);
}

GenerationResult GenerateSchedule(const std::string& output_dir, const GenerationOptions& options) {
    ScheduleInputData input_data;
    std::string input_error;
    if (!LoadScheduleInputData(input_data, input_error)) {
        return {false, "INPUT_ERROR", "Не удалось загрузить data/timetable_data.json: " + input_error, output_dir};
    }

    Date start_date = input_data.start_date;
    Date end_date = input_data.end_date;
    std::map<int, std::vector<std::pair<Date, Date>>> unavailable = input_data.unavailable;
    std::map<int, std::vector<std::pair<Date, Date>>> teacher_unavailable = input_data.teacher_unavailable;
    const auto& unavailable_day_texts = input_data.unavailable_day_texts;

    auto all_days = GenerateSchoolDays(start_date, end_date);

    int num_days = static_cast<int>(all_days.size());
    int total_slots = num_days * SLOTS_PER_DAY;
    std::vector<const WorkSchedule*> group_work(GROUPS, nullptr);
    std::vector<int> group_part_count(GROUPS, PARTS_PER_GROUP);
    for (const GroupData& group : input_data.groups)
        if (group.id >= 0 && group.id < GROUPS) {
            group_work[group.id] = &group.work_schedule;
            group_part_count[group.id] = std::clamp(group.parts, 1, PARTS_PER_GROUP);
        }
    std::vector<const WorkSchedule*> teacher_work(TEACHERS, nullptr);
    for (const TeacherData& teacher : input_data.teachers)
        if (teacher.id >= 0 && teacher.id < TEACHERS) teacher_work[teacher.id] = &teacher.work_schedule;
    for (const GroupData& group : input_data.groups) {
        for (const Date& date : all_days) {
            bool any_slot = false;
            for (int slot = 0; slot < SLOTS_PER_DAY; slot++)
                any_slot = any_slot || WorkScheduleAllows(group.work_schedule, date, slot);
            if (!any_slot) unavailable[group.id].push_back({date, date});
        }
    }
    for (const TeacherData& teacher : input_data.teachers) {
        for (const Date& date : all_days) {
            bool any_slot = false;
            for (int slot = 0; slot < SLOTS_PER_DAY; slot++)
                any_slot = any_slot || WorkScheduleAllows(teacher.work_schedule, date, slot);
            if (!any_slot) teacher_unavailable[teacher.id].push_back({date, date});
        }
    }
    // ── Weekly structure ─────────────────────────────────────────────────────
    // week_index[d] = raw sequential week number for day d
    std::vector<int> week_index(num_days);
    for (int d = 0; d < num_days; d++) {
        week_index[d] = WeekIndexFromStart(start_date, all_days[d]);
    }
    // Sorted unique week numbers → dense position index
    std::set<int> wk_set_tmp;
    for (int wi : week_index) wk_set_tmp.insert(wi);
    const std::vector<int> weeks_list(wk_set_tmp.begin(), wk_set_tmp.end());
    const int num_weeks = static_cast<int>(weeks_list.size());
    std::map<int, int> week_pos;   // raw week → position in weeks_list
    for (int i = 0; i < num_weeks; i++) week_pos[weeks_list[i]] = i;
    // All global slot indices grouped by week position
    std::vector<std::vector<int>> week_slots(num_weeks);
    for (int d = 0; d < num_days; d++) {
        int w = week_pos[week_index[d]];
        for (int s = 0; s < SLOTS_PER_DAY; s++) {
            week_slots[w].push_back(d * SLOTS_PER_DAY + s);
        }
    }

    std::vector<Lesson> lessons = input_data.lessons;
    int num_lessons = static_cast<int>(lessons.size());

    std::vector<std::string> validation_errors;
    if (!ValidateInputLessonsDetailed(lessons, validation_errors)) {
        std::cerr << "\nВходные данные содержат ошибки (" << validation_errors.size() << "). Модель не построена.\n";
        std::string message = "Входные данные содержат ошибки (" + std::to_string(validation_errors.size()) + "): ";
        for (size_t i = 0; i < validation_errors.size() && i < 5; i++) {
            if (i > 0) message += " | ";
            message += validation_errors[i];
        }
        if (validation_errors.size() > 5) message += " | …";
        return {false, "INPUT_ERROR", message, output_dir};
    }

    std::filesystem::create_directories(output_dir);
    std::filesystem::create_directories(std::filesystem::path(output_dir) / "groups");

    PrintInputDiagnostics(lessons, all_days, unavailable, start_date);

    // ── ПП: детерминированная расстановка до CP-SAT ───────────────────────
    // ПП-уроки не участвуют в поиске: их слоты жёстко фиксируются ниже, а дни
    // добавляются в unavailable, чтобы УП их не занимала и они выпадали из
    // делителя Брезенхема (целиком-ПП недели → /15 вместо /18).
    const PpPlan pp_plan = ComputePpPlan(lessons, all_days, unavailable);
    unavailable = MergeUnavailable(unavailable, pp_plan.pp_block);

    CpModelBuilder model;
    LinearExpr objective;

    std::vector<std::vector<BoolVar>> x(
        num_lessons,
        std::vector<BoolVar>(total_slots)
    );

    for (int l = 0; l < num_lessons; l++) {
        for (int t = 0; t < total_slots; t++) {
            x[l][t] = model.NewBoolVar();
        }
    }

    std::vector<BlockInfo> blocks;

    for (int l = 0; l < num_lessons; l++) {
        if (!lessons[l].is_block) continue;

        BlockInfo bi;
        bi.lesson_id = l;

        for (int d = 0; d < num_days; d++) {
            if (!IsAvailable(all_days[d], lessons[l].group, unavailable)) {
                continue;
            }

            for (int s = 0; s < SLOTS_PER_DAY - 1; s++) {
                if (!IsAllowedUpStartSlot(all_days[d], s)) {
                    continue;
                }
                const int group = lessons[l].group;
                const int teacher = lessons[l].teacher;
                if (group_work[group] &&
                    (!WorkScheduleAllows(*group_work[group], all_days[d], s) ||
                     !WorkScheduleAllows(*group_work[group], all_days[d], s + 1))) continue;
                if (teacher >= 0 && teacher_work[teacher] &&
                    (!WorkScheduleAllows(*teacher_work[teacher], all_days[d], s) ||
                     !WorkScheduleAllows(*teacher_work[teacher], all_days[d], s + 1))) continue;

                int t = d * SLOTS_PER_DAY + s;
                bi.possible_starts.push_back(t);
            }
        }

        for (int i = 0; i < static_cast<int>(bi.possible_starts.size()); i++) {
            bi.start_vars.push_back(model.NewBoolVar());
        }

        blocks.push_back(bi);
    }

    // ── Per-lesson weekly quota via Bresenham distribution ─────────────────
    // For each lesson we know total_slots and how many "available weeks"
    // the group has. Quota for week w = Bresenham step, so that sum == total_slots.
    // Weeks where the group has zero available school days get quota 0 always.

    // group → sorted list of week positions that have ≥1 available day
    std::vector<std::vector<int>> group_avail_weeks(GROUPS);
    // group × week → list of available day indices in that week
    std::vector<std::map<int, std::vector<int>>> group_week_days(GROUPS);

    for (int g = 0; g < GROUPS; g++) {
        for (int d = 0; d < num_days; d++) {
            if (IsAvailable(all_days[d], g, unavailable)) {
                int w = week_pos[week_index[d]];
                group_week_days[g][w].push_back(d);
            }
        }
        for (auto& kv : group_week_days[g]) {
            group_avail_weeks[g].push_back(kv.first);
        }
    }

    // Bresenham distributor: distribute `total` slots across `active_weeks`
    // Returns quota[i] for i-th active week.
    auto bresenham_quota = [](int total, int active_weeks) -> std::vector<int> {
        if (active_weeks <= 0) return {};
        std::vector<int> q(active_weeks, 0);
        int acc = 0;
        for (int i = 0; i < active_weeks; i++) {
            acc += total;
            int slots_here = acc / active_weeks;
            acc -= slots_here * active_weeks;
            q[i] = slots_here;
        }
        return q;
    };

    // lesson_week_quota[l][w] = how many placements of lesson l belong in week w
    // (w is a dense week position; groups with no available days get 0)
    std::vector<std::vector<int>> lesson_week_quota(num_lessons, std::vector<int>(num_weeks, 0));

    for (int l = 0; l < num_lessons; l++) {
        int g = lessons[l].group;
        const auto& avail_weeks = group_avail_weeks[g];
        int active = static_cast<int>(avail_weeks.size());
        if (active == 0) continue;
        auto q = bresenham_quota(lessons[l].total_slots, active);
        for (int i = 0; i < active; i++) {
            lesson_week_quota[l][avail_weeks[i]] = q[i];
        }
    }

    std::cout << "Недель в расписании: " << num_weeks << "\n";
    std::cout << "Дней в расписании: " << num_days << "\n";

    for (int l = 0; l < num_lessons; l++) {
        if (lessons[l].is_block) continue;
        if (lessons[l].is_pp) continue;  // ПП фиксируется детерминированно, не суммой

        LinearExpr sum;
        for (int t = 0; t < total_slots; t++) sum += x[l][t];
        model.AddEquality(sum, lessons[l].total_slots);
    }

    // ── ПП: жёстко фиксируем заранее рассчитанные слоты, остальное у ПП = 0 ──
    {
        std::vector<std::set<int>> pp_fixed(num_lessons);
        for (const PpPlacement& p : pp_plan.placements)
            pp_fixed[p.lesson_index].insert(p.global_day * SLOTS_PER_DAY + p.slot);
        for (int l = 0; l < num_lessons; l++) {
            if (!lessons[l].is_pp) continue;
            for (int t = 0; t < total_slots; t++)
                model.AddEquality(x[l][t], pp_fixed[l].count(t) ? 1 : 0);
        }
    }

    int total_block_start_vars = 0;

    for (auto& blk : blocks) {
        int l = blk.lesson_id;

        int required_starts = lessons[l].total_slots;
        total_block_start_vars += static_cast<int>(blk.start_vars.size());

        if (static_cast<int>(blk.possible_starts.size()) < required_starts) {
            std::cerr << "Недостаточно возможных стартов для блока: "
                      << lessons[l].name
                      << ", группа " << GROUP_NAME[lessons[l].group]
                      << ", доступно стартов " << blk.possible_starts.size()
                      << ", требуется " << required_starts
                      << "\n";
            return {false, "INPUT_ERROR", "Недостаточно возможных стартов для блока", output_dir};
        }

        // Global equality: exactly required_starts block starts total
        {
            LinearExpr start_sum;
            for (const auto& v : blk.start_vars) start_sum += v;
            model.AddEquality(start_sum, required_starts);
        }

        std::vector<std::vector<BoolVar>> covers(total_slots);

        for (int i = 0; i < static_cast<int>(blk.possible_starts.size()); i++) {
            int st = blk.possible_starts[i];

            covers[st].push_back(blk.start_vars[i]);
            covers[st + 1].push_back(blk.start_vars[i]);
        }

        for (int t = 0; t < total_slots; t++) {
            LinearExpr cover_sum;

            for (const auto& v : covers[t]) {
                cover_sum += v;
            }

            model.AddEquality(x[l][t], cover_sum);
        }
    }

    std::cout << "Агрегированных блоковых занятий УП: " << blocks.size() << "\n";
    std::cout << "Переменных старта УП после агрегации: "
              << total_block_start_vars << "\n";

    // ── Закрепление существующего расписания (lock_existing) ──
    if (!options.locked.empty()) {
        std::map<int, int> lesson_id_to_index;
        for (int l = 0; l < num_lessons; l++) {
            lesson_id_to_index[lessons[l].id] = l;
        }
        std::map<Date, int> date_to_day;
        for (int d = 0; d < num_days; d++) {
            date_to_day[all_days[d]] = d;
        }
        int applied = 0;
        int skipped_lesson = 0;
        int skipped_date = 0;
        int skipped_slot = 0;
        for (const LockedAssignment& a : options.locked) {
            auto lit = lesson_id_to_index.find(a.lesson_id);
            if (lit == lesson_id_to_index.end()) { skipped_lesson++; continue; }
            auto dit = date_to_day.find(a.date);
            if (dit == date_to_day.end()) { skipped_date++; continue; }
            if (a.slot < 0 || a.slot >= SLOTS_PER_DAY) { skipped_slot++; continue; }
            int t = dit->second * SLOTS_PER_DAY + a.slot;
            model.AddEquality(x[lit->second][t], 1);
            applied++;
        }
        std::cout << "Зафиксированных слотов (" << options.lock_source << "): " << applied
                  << " (пропущено: уроков " << skipped_lesson
                  << ", дат " << skipped_date
                  << ", слотов " << skipped_slot << ")\n";
    }

    for (int d = 0; d < num_days; d++) {
        for (int g = 0; g < GROUPS; g++) {
            if (IsAvailable(all_days[d], g, unavailable)) {
                continue;
            }

            for (int s = 0; s < SLOTS_PER_DAY; s++) {
                int t = d * SLOTS_PER_DAY + s;

                for (int l = 0; l < num_lessons; l++) {
                    if (lessons[l].is_pp) continue;  // ПП уже зафиксирована выше
                    if (lessons[l].group == g) {
                        model.AddEquality(x[l][t], 0);
                    }
                }
            }
        }
    }

    // Индивидуальная недоступность преподавателей и чётность недель.
    for (int l = 0; l < num_lessons; l++) {
        for (int d = 0; d < num_days; d++) {
            const int w = week_pos[week_index[d]];
            const bool blocked_teacher = lessons[l].teacher >= 0 &&
                DateInUnavailableRanges(all_days[d], lessons[l].teacher, teacher_unavailable);
            if (!blocked_teacher && LessonAllowsWeek(lessons[l], w)) continue;
            for (int s = 0; s < SLOTS_PER_DAY; s++) {
                model.AddEquality(x[l][d * SLOTS_PER_DAY + s], 0);
            }
        }
    }

    // Частичные рабочие окна групп и преподавателей.
    for (int l = 0; l < num_lessons; l++) {
        const int group = lessons[l].group;
        const int teacher = lessons[l].teacher;
        for (int d = 0; d < num_days; d++) {
            for (int s = 0; s < SLOTS_PER_DAY; s++) {
                const bool group_allowed = !group_work[group] ||
                    WorkScheduleAllows(*group_work[group], all_days[d], s);
                const bool teacher_allowed = teacher < 0 || !teacher_work[teacher] ||
                    WorkScheduleAllows(*teacher_work[teacher], all_days[d], s);
                if (!group_allowed || !teacher_allowed)
                    model.AddEquality(x[l][d * SLOTS_PER_DAY + s], 0);
            }
        }
    }

    // ── Теория до ЛПЗ во всём двухнедельном периоде ─────────────────────
    for (int lab = 0; lab < num_lessons; lab++) {
        if (!lessons[lab].is_lab) continue;
        std::vector<int> theory_lessons;
        for (int theory = 0; theory < num_lessons; theory++) {
            if (lessons[theory].is_lab || lessons[theory].is_block || lessons[theory].is_pp)
                continue;
            if (lessons[theory].group == lessons[lab].group &&
                lessons[theory].subject_id == lessons[lab].subject_id) {
                theory_lessons.push_back(theory);
            }
        }
        if (theory_lessons.empty()) continue;
        const auto prior_it = input_data.prior_theory_pairs.find(
            {lessons[lab].group, lessons[lab].subject_id});
        const int prior_theory = prior_it == input_data.prior_theory_pairs.end()
            ? 0 : prior_it->second;
        int required_theory = MIN_INITIAL_THEORY_SLOTS_BEFORE_LABS;
        if (STRICT_ALL_THEORY_BEFORE_LABS) {
            required_theory = prior_theory;
            for (int theory : theory_lessons)
                required_theory += std::max(0, lessons[theory].total_slots);
        }
        for (int t = 0; t < total_slots; t++) {
            LinearExpr theory_before;
            theory_before += prior_theory;
            for (int theory : theory_lessons)
                for (int earlier = 0; earlier < t; earlier++)
                    theory_before += x[theory][earlier];
            model.AddGreaterOrEqual(
                theory_before,
                x[lab][t] * required_theory);
        }
    }

    std::vector<std::vector<BoolVar>> group_busy(
        GROUPS,
        std::vector<BoolVar>(total_slots)
    );

    std::vector<std::vector<std::vector<BoolVar>>> part_busy(
        GROUPS,
        std::vector<std::vector<BoolVar>>(
            PARTS_PER_GROUP,
            std::vector<BoolVar>(total_slots)
        )
    );

    for (int g = 0; g < GROUPS; g++) {
        int base_subgroup = g * PARTS_PER_GROUP;

        for (int t = 0; t < total_slots; t++) {
            LinearExpr whole_sum;
            LinearExpr sub_sum[PARTS_PER_GROUP];

            for (int l = 0; l < num_lessons; l++) {
                if (lessons[l].group != g) continue;

                if (lessons[l].subgroup == -1) {
                    whole_sum += x[l][t];
                } else {
                    int part = lessons[l].subgroup - base_subgroup;

                    if (part >= 0 && part < PARTS_PER_GROUP) {
                        sub_sum[part] += x[l][t];
                    }
                }
            }

            model.AddLessOrEqual(whole_sum, 1);

            for (int p = 0; p < group_part_count[g]; p++) {
                model.AddLessOrEqual(sub_sum[p], 1);

                LinearExpr whole_plus_part;
                whole_plus_part += whole_sum;
                whole_plus_part += sub_sum[p];

                model.AddLessOrEqual(whole_plus_part, 1);
            }

            LinearExpr group_slot_sum;
            group_slot_sum += whole_sum;

            for (int p = 0; p < group_part_count[g]; p++) {
                group_slot_sum += sub_sum[p];
            }

            group_busy[g][t] = MakePositiveIndicator(model, group_slot_sum);

            for (int p = 0; p < PARTS_PER_GROUP; p++) {
                LinearExpr part_slot_sum;
                part_slot_sum += whole_sum;
                part_slot_sum += sub_sum[p];

                part_busy[g][p][t] = MakePositiveIndicator(model, part_slot_sum);
            }
        }
    }

    std::vector<std::vector<BoolVar>> student_entities;

    for (int g = 0; g < GROUPS; g++) {
        for (int p = 0; p < group_part_count[g]; p++) {
            student_entities.push_back(part_busy[g][p]);
        }
    }

    std::vector<bool> active_subject_lessons(num_lessons, false);
    for (int l = 0; l < num_lessons; l++)
        active_subject_lessons[l] = !lessons[l].is_pp && lessons[l].total_slots > 0;
    if (HARD_MAX_TWO_SAME_SUBJECT_PER_DAY) {
        AddMaxSameSubjectPerDay(
            model, lessons, x, num_days, active_subject_lessons,
            MAX_WHOLE_GROUP_SAME_SUBJECT_PAIRS_PER_DAY,
            MAX_SAME_SUBJECT_PAIRS_PER_DAY);
    }

    for (int g = 0; g < GROUPS; g++) {
        for (int p = 0; p < PARTS_PER_GROUP; p++) {
            for (int d = 0; d < num_days; d++) {
                std::vector<BoolVar> up_starts;

                for (const auto& blk : blocks) {
                    const Lesson& lesson = lessons[blk.lesson_id];

                    if (!LessonAffectsPart(lesson, g, p)) {
                        continue;
                    }

                    for (int i = 0; i < static_cast<int>(blk.possible_starts.size()); i++) {
                        int start_t = blk.possible_starts[i];

                        if (start_t / SLOTS_PER_DAY == d) {
                            up_starts.push_back(blk.start_vars[i]);
                        }
                    }
                }

                if (up_starts.empty()) {
                    continue;
                }

                LinearExpr up_start_sum;

                for (const auto& v : up_starts) {
                    up_start_sum += v;
                }

                model.AddLessOrEqual(up_start_sum, 1);

                LinearExpr part_day_sum;

                for (int s = 0; s < SLOTS_PER_DAY; s++) {
                    int t = d * SLOTS_PER_DAY + s;
                    part_day_sum += part_busy[g][p][t];
                }

                BoolVar has_up = MakePositiveIndicator(model, up_start_sum);

                LinearExpr required_up_slots;
                required_up_slots += up_start_sum;
                required_up_slots += up_start_sum;

                model.AddEquality(part_day_sum, required_up_slots)
                    .OnlyEnforceIf(has_up);
            }
        }
    }

    std::vector<std::vector<BoolVar>> teacher_busy(
        TEACHERS,
        std::vector<BoolVar>(total_slots)
    );

    for (int teacher = 0; teacher < TEACHERS; teacher++) {
        for (int t = 0; t < total_slots; t++) {
            LinearExpr sum;

            for (int l = 0; l < num_lessons; l++) {
                if (lessons[l].teacher == teacher) {
                    sum += x[l][t];
                }
            }

            model.AddLessOrEqual(sum, 1);

            teacher_busy[teacher][t] = MakePositiveIndicator(model, sum);
        }
    }

    // Индивидуальный предел числа пар преподавателя за один день.
    for (const TeacherData& teacher : input_data.teachers) {
        const int daily_limit = EffectiveTeacherMaxPairsPerDay(teacher.max_pairs_per_day);
        if (teacher.id < 0 || teacher.id >= TEACHERS || daily_limit == 0) continue;
        for (int d = 0; d < num_days; d++) {
            LinearExpr day_pairs;
            for (int s = 0; s < SLOTS_PER_DAY; s++)
                day_pairs += teacher_busy[teacher.id][d * SLOTS_PER_DAY + s];
            // teacher_busy — индикатор физически занятого pair-slot. Поэтому
            // параллельные строки одного преподавателя не могут занизить счётчик,
            // а значение 7 корректно разрешает полный семипарный день.
            model.AddLessOrEqual(day_pairs, daily_limit);
        }
    }

    // Ограничение числа рабочих дней преподавателя в каждой учебной неделе.
    std::vector<std::vector<BoolVar>> teacher_day_has(
        TEACHERS, std::vector<BoolVar>(num_days));
    for (int teacher = 0; teacher < TEACHERS; teacher++) {
        for (int d = 0; d < num_days; d++) {
            LinearExpr activity;
            for (int s = 0; s < SLOTS_PER_DAY; s++)
                activity += teacher_busy[teacher][d * SLOTS_PER_DAY + s];
            teacher_day_has[teacher][d] = MakePositiveIndicator(model, activity);
        }
    }
    for (const TeacherData& teacher : input_data.teachers) {
        if (teacher.id < 0 || teacher.id >= TEACHERS || teacher.max_work_days_per_week <= 0)
            continue;
        int total_teacher_slots = 0;
        for (const Lesson& lesson : lessons)
            if (lesson.teacher == teacher.id && !lesson.is_pp) total_teacher_slots += lesson.total_slots;
        const auto weekly_load = bresenham_quota(total_teacher_slots, num_weeks);
        for (int w = 0; w < num_weeks; w++) {
            LinearExpr work_days;
            LinearExpr week_pairs;
            for (int d = 0; d < num_days; d++)
                if (week_pos[week_index[d]] == w) {
                    work_days += teacher_day_has[teacher.id][d];
                    for (int s = 0; s < SLOTS_PER_DAY; s++)
                        week_pairs += teacher_busy[teacher.id][d * SLOTS_PER_DAY + s];
                }
            model.AddLessOrEqual(work_days, teacher.max_work_days_per_week);
            if (w < static_cast<int>(weekly_load.size()))
                model.AddEquality(week_pairs, weekly_load[w]);
        }
    }

    // Кабинеты пока задаются вручную на занятии. Для закреплённых кабинетов
    // конфликт в одном слоте является жёстким ограничением.
    std::set<int> fixed_rooms;
    for (const Lesson& lesson : lessons)
        if (lesson.fixed_room >= 0 && !lesson.allow_room_substitution) fixed_rooms.insert(lesson.fixed_room);
    for (int room : fixed_rooms) {
        for (int t = 0; t < total_slots; t++) {
            LinearExpr room_sum;
            for (int l = 0; l < num_lessons; l++) {
                if (lessons[l].fixed_room == room && !lessons[l].allow_room_substitution) room_sum += x[l][t];
            }
            model.AddLessOrEqual(room_sum, 1);
        }
    }

    for (const auto& blk : blocks) {
        int l = blk.lesson_id;
        int teacher = lessons[l].teacher;

        if (teacher < 0 || teacher >= TEACHERS) continue;

        for (int i = 0; i < static_cast<int>(blk.possible_starts.size()); i++) {
            int start_t = blk.possible_starts[i];
            std::vector<int> blocked_slots = TeacherBlockedSlotsForUpStart(
                all_days,
                start_t
            );

            for (int blocked_t : blocked_slots) {
                LinearExpr ordinary_teacher_lessons;

                for (int other = 0; other < num_lessons; other++) {
                    if (lessons[other].is_block) {
                        continue;
                    }

                    if (lessons[other].teacher == teacher) {
                        ordinary_teacher_lessons += x[other][blocked_t];
                    }
                }

                model.AddEquality(ordinary_teacher_lessons, 0)
                    .OnlyEnforceIf(blk.start_vars[i]);
            }
        }
    }

    std::vector<UpStartRef> up_start_refs;

    for (int b = 0; b < static_cast<int>(blocks.size()); b++) {
        const BlockInfo& blk = blocks[b];
        const Lesson& lesson = lessons[blk.lesson_id];

        for (int i = 0; i < static_cast<int>(blk.possible_starts.size()); i++) {
            int start_t = blk.possible_starts[i];
            int day = start_t / SLOTS_PER_DAY;

            up_start_refs.push_back({
                b,
                i,
                lesson.teacher,
                start_t,
                UpIntervalForStartSlot(all_days[day], start_t % SLOTS_PER_DAY)
            });
        }
    }

    for (int a = 0; a < static_cast<int>(up_start_refs.size()); a++) {
        for (int b = a + 1; b < static_cast<int>(up_start_refs.size()); b++) {
            const UpStartRef& left = up_start_refs[a];
            const UpStartRef& right = up_start_refs[b];

            if (left.teacher < 0 || left.teacher != right.teacher) {
                continue;
            }

            if (left.start_t / SLOTS_PER_DAY != right.start_t / SLOTS_PER_DAY) {
                continue;
            }

            if (!IntervalsOverlap(left.interval, right.interval)) {
                continue;
            }

            LinearExpr both_up;
            both_up += blocks[left.block_index].start_vars[left.start_index];
            both_up += blocks[right.block_index].start_vars[right.start_index];

            model.AddLessOrEqual(both_up, 1);
        }
    }

    std::vector<std::vector<std::vector<BoolVar>>> student_day_has(
        GROUPS,
        std::vector<std::vector<BoolVar>>(
            PARTS_PER_GROUP,
            std::vector<BoolVar>(num_days)
        )
    );

    std::vector<BoolVar> student_five_pair_day_vars;

    for (int g = 0; g < GROUPS; g++) {
        for (int p = 0; p < PARTS_PER_GROUP; p++) {
            for (int d = 0; d < num_days; d++) {
                LinearExpr day_sum;

                for (int s = 0; s < SLOTS_PER_DAY; s++) {
                    int t = d * SLOTS_PER_DAY + s;
                    day_sum += part_busy[g][p][t];
                }

                BoolVar has = MakePositiveIndicator(model, day_sum);
                student_day_has[g][p][d] = has;

                AddMinIfPositive(
                    model,
                    day_sum,
                    has,
                    MIN_STUDENT_PAIRS_PER_STUDY_DAY
                );

                model.AddLessOrEqual(day_sum, MAX_STUDENT_PAIRS_PER_DAY);

                BoolVar is_five_pair_day = model.NewBoolVar();
                model.AddEquality(day_sum, MAX_STUDENT_PAIRS_PER_DAY)
                    .OnlyEnforceIf(is_five_pair_day);
                model.AddLessOrEqual(day_sum, MAX_STUDENT_PAIRS_PER_DAY - 1)
                    .OnlyEnforceIf(is_five_pair_day.Not());
                student_five_pair_day_vars.push_back(is_five_pair_day);
            }
        }
    }

    for (int g = 0; g < GROUPS; g++) {
        for (int d = 0; d < num_days; d++) {
            model.AddEquality(student_day_has[g][0][d], student_day_has[g][1][d]);
        }
    }

    for (int g = 0; g < GROUPS; g++) {
        std::map<int, std::vector<int>> week_days;

        for (int d = 0; d < num_days; d++) {
            if (IsAvailable(all_days[d], g, unavailable)) {
                week_days[week_index[d]].push_back(d);
            }
        }

        for (const auto& item : week_days) {
            const std::vector<int>& days = item.second;
            if (days.empty()) {
                continue;
            }

            int required_days = std::min(
                MIN_STUDENT_STUDY_DAYS_PER_WEEK,
                static_cast<int>(days.size())
            );

            LinearExpr week_study_days;
            for (int d : days) {
                week_study_days += student_day_has[g][0][d];
            }

            if (HARD_MIN_STUDY_DAYS_PER_WEEK) {
                model.AddGreaterOrEqual(week_study_days, required_days);
            } else if (USE_QUALITY_OBJECTIVE) {
                IntVar missing = model.NewIntVar(Domain(0, required_days));
                LinearExpr week_with_missing;
                week_with_missing += week_study_days;
                week_with_missing += missing;

                model.AddGreaterOrEqual(week_with_missing, required_days);
                objective += missing * GROUP_WEEK_MISSING_DAY_WEIGHT;
            }
        }
    }

    if (HARD_MIN_2_TEACHER_PAIRS_PER_DAY) {
        for (int teacher = 0; teacher < TEACHERS; teacher++) {
            for (int d = 0; d < num_days; d++) {
                LinearExpr day_sum;

                for (int s = 0; s < SLOTS_PER_DAY; s++) {
                    int t = d * SLOTS_PER_DAY + s;
                    day_sum += teacher_busy[teacher][t];
                }

                AddMin2IfPositive(model, day_sum);
            }
        }
    }

    if (USE_QUALITY_OBJECTIVE) {
        AddSubjectSpreadPenalties(
            model,
            lessons,
            x,
            all_days,
            unavailable,
            objective
        );
    }

    if (HARD_NO_STUDENT_WINDOWS) {
        AddNoWindowsHard(model, student_entities, num_days);
    }

    if (HARD_NO_TEACHER_WINDOWS) {
        AddNoWindowsHard(model, teacher_busy, num_days);
    }

    std::vector<std::vector<IntVar>> group_day_campus(
        GROUPS,
        std::vector<IntVar>(num_days)
    );

    std::vector<std::vector<IntVar>> teacher_day_campus(
        TEACHERS,
        std::vector<IntVar>(num_days)
    );

    for (int g = 0; g < GROUPS; g++) {
        for (int d = 0; d < num_days; d++) {
            group_day_campus[g][d] = model.NewIntVar(Domain(0, 1));
        }
    }

    for (int teacher = 0; teacher < TEACHERS; teacher++) {
        for (int d = 0; d < num_days; d++) {
            teacher_day_campus[teacher][d] = model.NewIntVar(Domain(0, 1));
        }
    }

    for (int l = 0; l < num_lessons; l++) {
        int group = lessons[l].group;
        int teacher = lessons[l].teacher;

        for (int d = 0; d < num_days; d++) {
            for (int s = 0; s < SLOTS_PER_DAY; s++) {
                int t = d * SLOTS_PER_DAY + s;

                if (teacher >= 0) {
                    model.AddEquality(group_day_campus[group][d], teacher_day_campus[teacher][d])
                        .OnlyEnforceIf(x[l][t]);
                }

                if (lessons[l].allowed_campuses.size() == 1) {
                    int campus = static_cast<int>(*lessons[l].allowed_campuses.begin());

                    model.AddEquality(group_day_campus[group][d], campus)
                        .OnlyEnforceIf(x[l][t]);

                    if (teacher >= 0) {
                        model.AddEquality(teacher_day_campus[teacher][d], campus)
                            .OnlyEnforceIf(x[l][t]);
                    }
                }
            }
        }
    }

    // ── Вместимость аудиторного фонда по площадке ──────────────────────
    // Закрытые помещения не участвуют в ёмкости. Эксклюзивные мастерские
    // также не считаем общим резервом: это консервативно, зато CP-SAT не
    // создаёт сетку, которую затем невозможно обеспечить кабинетами.
    for (int d = 0; d < num_days; d++) {
        for (int s = 0; s < SLOTS_PER_DAY; s++) {
            const int t = d * SLOTS_PER_DAY + s;
            int room_capacity_by_campus[2] = {0, 0};
            int sports_capacity_by_campus[2] = {0, 0};
            for (const RoomData& room : input_data.rooms) {
                if (!room.active || room.access_mode == "blocked" ||
                    room.campus < LESNAYA || room.campus > KRIVOUSOVA) continue;
                if (!room.available_slots.empty() && !room.available_slots.count(s + 1)) continue;
                if (!WorkScheduleAllows(room.work_schedule, all_days[d], s)) continue;
                if (room.purpose == "sports_hall") {
                    sports_capacity_by_campus[room.campus]++;
                } else if (room.access_mode != "exclusive") {
                    room_capacity_by_campus[room.campus]++;
                }
            }
            LinearExpr campus0_demand;
            LinearExpr campus1_demand;
            LinearExpr sports0_demand;
            LinearExpr sports1_demand;
            for (int l = 0; l < num_lessons; l++) {
                BoolVar at_campus1 = model.NewBoolVar();
                const IntVar& campus = group_day_campus[lessons[l].group][d];
                model.AddLessOrEqual(at_campus1, x[l][t]);
                model.AddLessOrEqual(at_campus1, campus);
                LinearExpr lower;
                lower += x[l][t];
                lower += campus;
                lower -= 1;
                model.AddGreaterOrEqual(at_campus1, lower);
                LinearExpr& demand0 = lessons[l].required_room_purpose == "sports_hall" ? sports0_demand : campus0_demand;
                LinearExpr& demand1 = lessons[l].required_room_purpose == "sports_hall" ? sports1_demand : campus1_demand;
                demand1 += at_campus1;
                demand0 += x[l][t];
                demand0 -= at_campus1;
            }
            model.AddLessOrEqual(campus0_demand, room_capacity_by_campus[LESNAYA]);
            model.AddLessOrEqual(campus1_demand, room_capacity_by_campus[KRIVOUSOVA]);
            model.AddLessOrEqual(sports0_demand, sports_capacity_by_campus[LESNAYA]);
            model.AddLessOrEqual(sports1_demand, sports_capacity_by_campus[KRIVOUSOVA]);
        }
    }

    if (USE_QUALITY_OBJECTIVE) {
        for (const auto& v : student_five_pair_day_vars) {
            objective += v * STUDENT_FIVE_PAIR_DAY_WEIGHT;
        }

        if (!HARD_NO_STUDENT_WINDOWS && OPTIMIZE_STUDENT_WINDOWS) {
            std::vector<BoolVar> student_gaps =
                CreateWindowPenaltyVars(model, student_entities, num_days);

            for (const auto& gap : student_gaps) {
                objective += gap * STUDENT_WINDOW_WEIGHT;
            }
        }

        if (!HARD_NO_TEACHER_WINDOWS && OPTIMIZE_TEACHER_WINDOWS) {
            std::vector<BoolVar> teacher_gaps =
                CreateWindowPenaltyVars(model, teacher_busy, num_days);

            for (const auto& gap : teacher_gaps) {
                objective += gap * TEACHER_WINDOW_WEIGHT;
            }
        }

        if (STUDENT_LATE_SLOT_WEIGHT > 0) {
            for (const auto& busy : student_entities) {
                for (int d = 0; d < num_days; d++) {
                    int base = d * SLOTS_PER_DAY;

                    for (int s = 0; s < SLOTS_PER_DAY; s++) {
                        objective += busy[base + s] * (s * STUDENT_LATE_SLOT_WEIGHT);
                    }
                }
            }
        }

        if (TEACHER_LATE_SLOT_WEIGHT > 0) {
            for (int teacher = 0; teacher < TEACHERS; teacher++) {
                for (int d = 0; d < num_days; d++) {
                    int base = d * SLOTS_PER_DAY;

                    for (int s = 0; s < SLOTS_PER_DAY; s++) {
                        objective += teacher_busy[teacher][base + s] * (s * TEACHER_LATE_SLOT_WEIGHT);
                    }
                }
            }
        }

        model.Minimize(objective);
    }

    long long est_x_vars = static_cast<long long>(num_lessons) * total_slots;
    long long est_group_busy = static_cast<long long>(GROUPS) * total_slots;
    long long est_part_busy = static_cast<long long>(GROUPS) * PARTS_PER_GROUP * total_slots;
    long long est_teacher_busy = static_cast<long long>(TEACHERS) * total_slots;
    long long est_block_starts = total_block_start_vars;
    long long est_student_day_has = static_cast<long long>(GROUPS) * PARTS_PER_GROUP * num_days;
    long long est_five_pair = static_cast<long long>(student_five_pair_day_vars.size());
    long long est_campus_int = static_cast<long long>(GROUPS) * num_days + static_cast<long long>(TEACHERS) * num_days;

    std::cout << "\n========== Категории переменных (предварительная оценка) ==========\n";
    std::cout << "x[lesson][slot]      : " << est_x_vars << "  (" << num_lessons << " уроков × " << total_slots << " слотов)\n";
    std::cout << "group_busy           : " << est_group_busy << "\n";
    std::cout << "part_busy            : " << est_part_busy << "\n";
    std::cout << "teacher_busy         : " << est_teacher_busy << "\n";
    std::cout << "block start_vars     : " << est_block_starts << "\n";
    std::cout << "student_day_has      : " << est_student_day_has << "\n";
    std::cout << "five_pair_day_vars   : " << est_five_pair << "\n";
    std::cout << "*_day_campus (Int)   : " << est_campus_int << "\n";
    std::cout << "ИТОГО (булевых +- )  : "
              << (est_x_vars + est_group_busy + est_part_busy + est_teacher_busy + est_block_starts + est_student_day_has + est_five_pair)
              << "\n";

    std::cout << "\nЗапуск решателя...\n";

    CpModelProto model_proto = model.Build();

    std::cout << "\n========== Размер модели (фактический) ==========\n";
    std::cout << "Всего переменных     : " << model_proto.variables_size() << "\n";
    std::cout << "Всего ограничений    : " << model_proto.constraints_size() << "\n";
    std::cout << "Размер proto         : " << (model_proto.ByteSizeLong() / (1024.0 * 1024.0)) << " МБ\n";
    std::cout << "Параметры солвера    : workers=" << SOLVER_WORKERS
              << ", time_limit=" << SOLVER_TIME_LIMIT_SECONDS << "s"
              << ", quality_obj=" << (USE_QUALITY_OBJECTIVE ? "true" : "false")
              << ", hard_no_student_windows=" << (HARD_NO_STUDENT_WINDOWS ? "true" : "false")
              << ", stop_first=" << (STOP_AFTER_FIRST_SOLUTION ? "true" : "false") << "\n";

    SatParameters params;
    params.set_num_search_workers(SOLVER_WORKERS);
    params.set_max_time_in_seconds(SOLVER_TIME_LIMIT_SECONDS);
    params.set_random_seed(g_solver_config.random_seed);
    params.set_max_memory_in_mb(SOLVER_MAX_MEMORY_MB);
    params.set_linearization_level(g_solver_config.linearization_level);
    params.set_symmetry_level(g_solver_config.symmetry_level);

    // Realtime-логи поиска: solver сам печатает прогресс каждые ~5 сек.
    params.set_log_search_progress(true);
    params.set_log_subsolver_statistics(true);
    params.set_log_to_stdout(true);

    if (STOP_AFTER_FIRST_SOLUTION) {
        params.set_stop_after_first_solution(true);
    }

    std::cout << "Random seed: " << g_solver_config.random_seed
              << ", linearization_level: " << g_solver_config.linearization_level
              << ", symmetry_level: " << g_solver_config.symmetry_level << "\n\n";

    operations_research::sat::Model sat_model;
    sat_model.Add(NewSatParameters(params));

    // Колбэк на каждое найденное feasible-решение — печатает время и objective.
    int solution_counter = 0;
    auto solve_start = std::chrono::steady_clock::now();
    sat_model.Add(operations_research::sat::NewFeasibleSolutionObserver(
        [&solution_counter, &solve_start](const CpSolverResponse& r) {
            solution_counter++;
            auto now = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(now - solve_start).count();
            std::cout << "\n>>> [Решение #" << solution_counter << "] найдено за "
                      << std::fixed << std::setprecision(2) << elapsed << " сек";
            std::cout << ", objective=" << r.objective_value()
                      << ", bound=" << r.best_objective_bound();
            std::cout << " <<<\n" << std::flush;
        }
    ));

    CpSolverResponse response = SolveCpModel(model_proto, &sat_model);

    std::cout << "\n========== Результат ==========\n";
    std::cout << "Status: " << CpSolverStatus_Name(response.status()) << "\n";
    std::cout << CpSolverResponseStats(response) << "\n";

    if (response.status() == CpSolverStatus::OPTIMAL ||
        response.status() == CpSolverStatus::FEASIBLE) {

        int student_windows = CountWindows(response, student_entities, num_days);
        int teacher_windows = CountWindows(response, teacher_busy, num_days);
        int max_student_pairs = MaxStudentPairsInDay(response, part_busy, num_days);
        int student_days_at_daily_limit = CountStudentDaysWithPairCount(
            response, part_busy, num_days, MAX_STUDENT_PAIRS_PER_DAY);
        int five_pair_days = CountFivePairStudentDays(response, part_busy, num_days);
        int up_day_violations = CountUpDayRuleViolations(
            response,
            lessons,
            x,
            part_busy,
            num_days
        );
        int up_teacher_lock_violations = CountUpTeacherLockViolations(
            response,
            lessons,
            x,
            blocks,
            all_days
        );

        std::cout << "\nРасписание найдено.\n";
        std::cout << "Окон у студентов: " << student_windows << "\n";
        std::cout << "Окон у преподавателей: " << teacher_windows << "\n";
        std::cout << "Максимум пар у студента за день: " << max_student_pairs << "\n";
        std::cout << "Дней на дневном максимуме (" << MAX_STUDENT_PAIRS_PER_DAY
                  << "): " << student_days_at_daily_limit << "\n";
        std::cout << "Дней по 5 пар у подгрупп: " << five_pair_days << "\n";
        std::cout << "Нарушений правила УП-день: " << up_day_violations << "\n";
        std::cout << "Нарушений занятости преподавателя во время УП: "
                  << up_teacher_lock_violations << "\n";

        if (USE_QUALITY_OBJECTIVE) {
            std::cout << "Objective value: " << response.objective_value() << "\n";
            std::cout << "Best bound: " << response.best_objective_bound() << "\n";
        }

        const std::filesystem::path out_dir(output_dir);
        const std::filesystem::path groups_dir = out_dir / "groups";

        std::vector<std::vector<int>> solved_x(
            num_lessons, std::vector<int>(total_slots, 0));
        for (int l = 0; l < num_lessons; l++) {
            for (int t = 0; t < total_slots; t++) {
                solved_x[l][t] = BoolValue(response, x[l][t]) ? 1 : 0;
            }
        }
        std::vector<std::vector<int>> solved_campus(
            GROUPS, std::vector<int>(num_days, 0));
        for (int g = 0; g < GROUPS; g++) {
            for (int d = 0; d < num_days; d++) {
                solved_campus[g][d] = IntValue(response, group_day_campus[g][d]);
            }
        }
        RoomAllocationResult room_allocation = AllocateRooms(
            lessons, input_data.groups, input_data.rooms, all_days, solved_x, solved_campus);
        const RoomAssignmentMap* room_assignments = room_allocation.inventory_configured
            ? &room_allocation.assignments : nullptr;
        WriteBackendReports(out_dir, lessons, solved_x, student_windows, teacher_windows,
            max_student_pairs, student_days_at_daily_limit, five_pair_days, up_day_violations,
            up_teacher_lock_violations, room_allocation);

        WriteAllGroupsTxt(
            (out_dir / "raspisanie_all.txt").string(),
            response,
            all_days,
            lessons,
            x,
            group_busy,
            group_day_campus,
            unavailable_day_texts
        );

        for (int g = 0; g < GROUPS; g++) {
            WriteGroupScheduleTxt(
                (groups_dir / ("raspisanie_group_" + std::to_string(g) + ".txt")).string(),
                response,
                all_days,
                lessons,
                x,
                group_busy,
                group_day_campus,
                unavailable_day_texts,
                g
            );
        }

        WriteGroupsCsv(
            (out_dir / "raspisanie_groups.csv").string(),
            response,
            all_days,
            lessons,
            x,
            group_busy,
            group_day_campus,
            unavailable_day_texts
        );

        WriteTeachersTxt(
            (out_dir / "raspisanie_teachers.txt").string(),
            response,
            all_days,
            lessons,
            x,
            blocks,
            teacher_busy,
            teacher_day_campus
        );

        WriteAllGroupsJson(
            (out_dir / "schedule_all.json").string(),
            response,
            all_days,
            lessons,
            x,
            group_busy,
            group_day_campus,
            unavailable_day_texts,
            room_assignments
        );

        for (int g = 0; g < GROUPS; g++) {
            WriteGroupJson(
                (groups_dir / ("group_" + std::to_string(g) + ".json")).string(),
                response,
                all_days,
                lessons,
                x,
                group_busy,
                group_day_campus,
                unavailable_day_texts,
                g,
                room_assignments
            );
        }

        std::cout << "\nФайлы созданы:\n";
        std::cout << "  " << (std::filesystem::path(output_dir) / "raspisanie_all.txt").string() << "\n";
        std::cout << "  " << (std::filesystem::path(output_dir) / "schedule_all.json").string() << "\n";
        std::cout << "  " << (std::filesystem::path(output_dir) / "groups" / "group_*.json").string() << "\n";
        std::cout << "  " << (std::filesystem::path(output_dir) / "raspisanie_groups.csv").string() << "\n";
        std::cout << "  " << (std::filesystem::path(output_dir) / "raspisanie_teachers.txt").string() << "\n";

        return {true, CpSolverStatus_Name(response.status()), "Расписание найдено", output_dir};

    } else if (response.status() == CpSolverStatus::INFEASIBLE) {
        std::cout << "\nМодель противоречива.\n";
        std::cout << "Что можно ослабить первым:\n";
        std::cout << "  1) HARD_NO_STUDENT_WINDOWS = false\n";
        std::cout << "  2) MIN_STUDENT_STUDY_DAYS_PER_WEEK = 1\n";
        std::cout << "  3) увеличить SUBJECT_BUCKET_EXTRA_SLOTS до 3 или 4\n";
        std::cout << "  4) уменьшить MIN_SUBJECT_SPREAD_TOTAL_SLOTS\n";
        std::cout << "  5) HARD_MIN_2_TEACHER_PAIRS_PER_DAY оставить false\n";
        std::cout << "  6) STRICT_ALL_THEORY_BEFORE_LABS оставить false\n";
        std::cout << "  7) если УП-дни слишком жёсткие, проверь правило "
                  << "student_day_has[g][0][d] == student_day_has[g][1][d]\n";
        return {false, CpSolverStatus_Name(response.status()), "Модель противоречива", output_dir};
    } else if (response.status() == CpSolverStatus::UNKNOWN) {
        std::cout << "\nРешатель не успел найти или доказать решение за лимит времени.\n";
        std::cout << "Что можно сделать:\n";
        std::cout << "  1) увеличить SOLVER_TIME_LIMIT_SECONDS\n";
        std::cout << "  2) уменьшить SOLVER_WORKERS до 2, если не хватает ОЗУ\n";
        std::cout << "  3) увеличить SUBJECT_BUCKET_EXTRA_SLOTS до 3 или 4\n";
        std::cout << "  4) временно поставить HARD_NO_STUDENT_WINDOWS = false\n";
        return {false, CpSolverStatus_Name(response.status()), "Решатель не успел найти или доказать решение за лимит времени", output_dir};
    } else if (response.status() == CpSolverStatus::MODEL_INVALID) {
        std::cout << "\nМодель некорректна. Проверь CpSolverResponseStats выше.\n";
        return {false, CpSolverStatus_Name(response.status()), "Модель некорректна", output_dir};
    } else {
        std::cout << "\nРешение не найдено. Статус: "
                  << CpSolverStatus_Name(response.status())
                  << "\n";
        return {false, CpSolverStatus_Name(response.status()), "Решение не найдено", output_dir};
    }

    return {false, "UNKNOWN", "Решение не найдено", output_dir};
}

// ─────────────────────────────────────────────────────────────────────────────
// Weekly generation: отдельная маленькая CP-SAT задача на каждую неделю.
// Квоты (сколько пар/стартов у каждого занятия в конкретную неделю) берутся
// из алгоритма Брезенхема, поэтому сумма квот == total_slots по каждому уроку.
// ─────────────────────────────────────────────────────────────────────────────

namespace {

struct WeekSolveResult {
    bool success = false;
    std::string status;
    // x_vals[l][local_slot] — 0/1; для уроков с quota==0 не заполняется (всё 0)
    std::vector<std::vector<int>> x_vals;
    // Фактически выбранный решателем корпус для каждой группы и локального дня.
    std::vector<std::vector<int>> group_day_campus;
    int model_variables = 0;
    int model_constraints = 0;
    int warm_start_hints = 0;
    double model_build_seconds = 0.0;
    double feasibility_seconds = 0.0;
    double quality_seconds = 0.0;
    double solve_wall_seconds = 0.0;
    std::int64_t branches = 0;
    std::int64_t conflicts = 0;
    bool adaptive_quality_stop = false;
};

struct WeeklyPreflightResult {
    bool ok = true;
    JsonValue report = JsonValue::MakeObject();
    std::string message;
};

struct QuotaBalanceResult {
    bool success = false;
    bool cancelled = false;
    std::string status;
    double solve_seconds = 0.0;
    JsonValue report = JsonValue::MakeObject();
};

static WeeklyPreflightResult BuildWeeklyPreflight(
    const std::vector<Lesson>& lessons,
    const std::vector<GroupData>& groups,
    const std::vector<TeacherData>& teachers,
    const std::map<int, int>& teacher_period_targets,
    const std::vector<Date>& all_days,
    const std::vector<std::vector<int>>& week_day_indices,
    const std::vector<std::vector<int>>& lesson_week_quota,
    const std::map<int, std::vector<std::pair<Date, Date>>>& unavailable,
    const std::map<int, std::vector<std::pair<Date, Date>>>& teacher_unavailable
) {
    WeeklyPreflightResult result;
    JsonValue issues = JsonValue::MakeArray();
    JsonValue warnings = JsonValue::MakeArray();
    std::vector<const WorkSchedule*> group_work(GROUPS, nullptr);
    std::vector<int> group_part_count(GROUPS, PARTS_PER_GROUP);
    for (const GroupData& group : groups)
        if (group.id >= 0 && group.id < GROUPS) {
            group_work[group.id] = &group.work_schedule;
            group_part_count[group.id] = std::clamp(group.parts, 1, PARTS_PER_GROUP);
        }
    std::vector<const WorkSchedule*> teacher_work(TEACHERS, nullptr);
    for (const TeacherData& teacher : teachers)
        if (teacher.id >= 0 && teacher.id < TEACHERS) teacher_work[teacher.id] = &teacher.work_schedule;

    auto add_issue = [&](const std::string& code, const std::string& message,
                         int week, const std::string& entity_type, int entity_id) {
        JsonValue issue = JsonValue::MakeObject();
        issue.At("severity") = JsonValue::MakeString("error");
        issue.At("code") = JsonValue::MakeString(code);
        issue.At("message") = JsonValue::MakeString(message);
        if (week >= 0) {
            issue.At("week") = JsonValue::MakeNumber(week + 1);
            issue.At("scope") = JsonValue::MakeString("week");
        } else {
            issue.At("week") = JsonValue::MakeNull();
            issue.At("scope") = JsonValue::MakeString("period");
        }
        issue.At("entity_type") = JsonValue::MakeString(entity_type);
        issue.At("entity_id") = JsonValue::MakeNumber(entity_id);
        issues.array_value.push_back(issue);
    };
    const int num_weeks = static_cast<int>(week_day_indices.size());
    for (int w = 0; w < num_weeks; w++) {
        std::vector<std::vector<int>> group_part_demand(
            GROUPS, std::vector<int>(PARTS_PER_GROUP, 0));
        std::vector<int> teacher_demand(TEACHERS, 0);
        std::map<int, int> fixed_room_demand;

        for (int l = 0; l < static_cast<int>(lessons.size()); l++) {
            const int quota = l < static_cast<int>(lesson_week_quota.size()) &&
                w < static_cast<int>(lesson_week_quota[l].size())
                ? lesson_week_quota[l][w] : 0;
            if (quota <= 0) continue;

            const Lesson& lesson = lessons[l];
            const int occupied = quota * (lesson.is_block ? 2 : 1);
            const int real_parts = lesson.group >= 0 && lesson.group < GROUPS
                ? group_part_count[lesson.group] : PARTS_PER_GROUP;
            for (int p = 0; p < real_parts; p++) {
                if (LessonAffectsPart(lesson, lesson.group, p))
                    group_part_demand[lesson.group][p] += occupied;
            }
            if (lesson.teacher >= 0 && lesson.teacher < TEACHERS)
                teacher_demand[lesson.teacher] += occupied;
            if (lesson.fixed_room >= 0 && !lesson.allow_room_substitution)
                fixed_room_demand[lesson.fixed_room] += occupied;

            int possible_positions = 0;
            for (int gd : week_day_indices[w]) {
                const Date& date = all_days[gd];
                if (!IsAvailable(date, lesson.group, unavailable)) continue;
                if (lesson.teacher >= 0 &&
                    DateInUnavailableRanges(date, lesson.teacher, teacher_unavailable)) continue;
                if (lesson.is_block) {
                    for (int s = 0; s < SLOTS_PER_DAY - 1; s++) {
                        if (!IsAllowedUpStartSlot(date, s)) continue;
                        const bool group_ok = !group_work[lesson.group] ||
                            (WorkScheduleAllows(*group_work[lesson.group], date, s) &&
                             WorkScheduleAllows(*group_work[lesson.group], date, s + 1));
                        const bool teacher_ok = lesson.teacher < 0 || !teacher_work[lesson.teacher] ||
                            (WorkScheduleAllows(*teacher_work[lesson.teacher], date, s) &&
                             WorkScheduleAllows(*teacher_work[lesson.teacher], date, s + 1));
                        if (group_ok && teacher_ok) possible_positions++;
                    }
                } else {
                    for (int s = 0; s < SLOTS_PER_DAY; s++) {
                        const bool group_ok = !group_work[lesson.group] ||
                            WorkScheduleAllows(*group_work[lesson.group], date, s);
                        const bool teacher_ok = lesson.teacher < 0 || !teacher_work[lesson.teacher] ||
                            WorkScheduleAllows(*teacher_work[lesson.teacher], date, s);
                        if (group_ok && teacher_ok) possible_positions++;
                    }
                }
            }
            if (quota > possible_positions) {
                add_issue("lesson_week_no_positions",
                    "Для занятия «" + lesson.name + "» нужно " + std::to_string(quota) +
                    " размещений, доступно только " + std::to_string(possible_positions),
                    w, "lesson", lesson.id);
            }
        }

        for (int g = 0; g < GROUPS; g++) {
            int capacity = 0;
            for (int gd : week_day_indices[w]) {
                if (!IsAvailable(all_days[gd], g, unavailable)) continue;
                int allowed_slots = 0;
                for (int s = 0; s < SLOTS_PER_DAY; s++)
                    if (!group_work[g] || WorkScheduleAllows(*group_work[g], all_days[gd], s)) allowed_slots++;
                capacity += std::min(MAX_STUDENT_PAIRS_PER_DAY, allowed_slots);
            }
            for (int p = 0; p < group_part_count[g]; p++) {
                if (group_part_demand[g][p] > capacity) {
                    add_issue("group_week_over_capacity",
                        "Нагрузка подгруппы " + std::to_string(p + 1) + " равна " +
                        std::to_string(group_part_demand[g][p]) + " парам при доступной ёмкости " +
                        std::to_string(capacity), w, "group", g);
                }
            }
        }

        for (int teacher = 0; teacher < TEACHERS; teacher++) {
            if (teacher_demand[teacher] == 0) continue;
            int capacity = 0;
            for (int gd : week_day_indices[w]) {
                if (DateInUnavailableRanges(all_days[gd], teacher, teacher_unavailable)) continue;
                for (int s = 0; s < SLOTS_PER_DAY; s++)
                    if (!teacher_work[teacher] || WorkScheduleAllows(*teacher_work[teacher], all_days[gd], s)) capacity++;
            }
            if (teacher_demand[teacher] > capacity) {
                add_issue("teacher_week_over_capacity",
                    "Нагрузка преподавателя равна " + std::to_string(teacher_demand[teacher]) +
                    " парам при физической ёмкости " + std::to_string(capacity),
                    w, "teacher", teacher);
            }
        }

        const int room_capacity = static_cast<int>(week_day_indices[w].size()) * SLOTS_PER_DAY;
        for (const auto& entry : fixed_room_demand) {
            if (entry.second > room_capacity) {
                add_issue("fixed_room_week_over_capacity",
                    "В закреплённый кабинет требуется поставить " + std::to_string(entry.second) +
                    " пар при физической ёмкости " + std::to_string(room_capacity),
                    w, "room", entry.first);
            }
        }
    }

    for (int l = 0; l < static_cast<int>(lessons.size()); l++) {
        if (lessons[l].is_pp) continue;
        int quota_sum = 0;
        for (int value : lesson_week_quota[l]) quota_sum += value;
        if (quota_sum != lessons[l].total_slots) {
            add_issue("lesson_quota_incomplete",
                "Для занятия «" + lessons[l].name + "» распределено " +
                std::to_string(quota_sum) + " из " + std::to_string(lessons[l].total_slots) +
                " размещений; успешная генерация требует полной нагрузки",
                0, "lesson", lessons[l].id);
        }
    }

    // Цель относится ко всему выбранному диапазону, а не к каждой неделе.
    // Так как ниже каждая lesson quota является точным равенством, выполнение
    // этой проверки вместе с финальным per-lesson invariant гарантирует
    // фактическую вычитку не меньше заданного числа пар преподавателя.
    for (const auto& [teacher, minimum_pairs] : teacher_period_targets) {
        int period_demand = 0;
        for (int l = 0; l < static_cast<int>(lessons.size()); l++) {
            if (lessons[l].teacher != teacher || lessons[l].is_pp) continue;
            int lesson_demand = 0;
            for (int value : lesson_week_quota[l]) lesson_demand += value;
            period_demand += lesson_demand * (lessons[l].is_block ? 2 : 1);
        }
        if (period_demand < minimum_pairs) {
            std::string teacher_name = "ID " + std::to_string(teacher);
            for (const TeacherData& item : teachers) {
                if (item.id == teacher) {
                    teacher_name = item.name;
                    break;
                }
            }
            add_issue("teacher_period_target_shortfall",
                "На весь период преподавателю «" + teacher_name + "» задано минимум " +
                    std::to_string(minimum_pairs) + " пар, но активные квоты содержат только " +
                    std::to_string(period_demand) + " пар",
                -1, "teacher", teacher);
        }
    }

    result.ok = issues.array_value.empty();
    result.message = result.ok
        ? "Недельные квоты проходят проверку физической вместимости"
        : "Найдено критических проблем недельных квот: " +
            std::to_string(issues.array_value.size());
    JsonValue summary = JsonValue::MakeObject();
    summary.At("ok") = JsonValue::MakeBool(result.ok);
    summary.At("weeks") = JsonValue::MakeNumber(num_weeks);
    summary.At("errors") = JsonValue::MakeNumber(issues.array_value.size());
    summary.At("warnings") = JsonValue::MakeNumber(warnings.array_value.size());
    result.report.At("ok") = JsonValue::MakeBool(result.ok);
    result.report.At("message") = JsonValue::MakeString(result.message);
    result.report.At("summary") = summary;
    result.report.At("issues") = issues;
    result.report.At("warnings") = warnings;
    return result;
}

static QuotaBalanceResult BalanceWeeklyQuotas(
    const std::vector<Lesson>& lessons,
    const std::vector<GroupData>& groups,
    const std::vector<TeacherData>& teachers,
    const std::vector<Date>& all_days,
    const std::vector<std::vector<int>>& week_day_indices,
    const std::vector<std::vector<bool>>& lesson_week_allowed,
    const std::map<int, std::vector<std::pair<Date, Date>>>& unavailable,
    const std::map<int, std::vector<std::pair<Date, Date>>>& teacher_unavailable,
    const std::vector<LockedAssignment>& locked,
    std::vector<std::vector<int>>& quotas,
    std::atomic<bool>* cancel_flag
) {
    using operations_research::Domain;
    using operations_research::sat::CpModelBuilder;
    using operations_research::sat::CpModelProto;
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

    QuotaBalanceResult result;
    const int L = static_cast<int>(lessons.size());
    const int W = static_cast<int>(week_day_indices.size());
    const std::vector<std::vector<int>> initial = quotas;
    std::vector<const WorkSchedule*> group_work(GROUPS, nullptr);
    std::vector<int> group_part_count(GROUPS, PARTS_PER_GROUP);
    for (const GroupData& group : groups)
        if (group.id >= 0 && group.id < GROUPS) {
            group_work[group.id] = &group.work_schedule;
            group_part_count[group.id] = std::clamp(group.parts, 1, PARTS_PER_GROUP);
        }
    std::vector<const WorkSchedule*> teacher_work(TEACHERS, nullptr);
    for (const TeacherData& teacher : teachers)
        if (teacher.id >= 0 && teacher.id < TEACHERS) teacher_work[teacher.id] = &teacher.work_schedule;

    std::map<int, int> lesson_id_to_index;
    for (int l = 0; l < L; l++) lesson_id_to_index[lessons[l].id] = l;
    std::map<Date, int> date_to_week;
    for (int w = 0; w < W; w++)
        for (int gd : week_day_indices[w]) date_to_week[all_days[gd]] = w;
    std::map<std::pair<int, int>, std::set<int>> locked_slots;
    for (const LockedAssignment& assignment : locked) {
        auto lit = lesson_id_to_index.find(assignment.lesson_id);
        auto wit = date_to_week.find(assignment.date);
        if (lit == lesson_id_to_index.end() || wit == date_to_week.end()) continue;
        locked_slots[{lit->second, wit->second}].insert(
            assignment.date.day * 100 + assignment.slot);
    }

    CpModelBuilder model;
    std::vector<std::vector<IntVar>> q(L, std::vector<IntVar>(W));
    std::vector<std::vector<IntVar>> deviation_vars(L, std::vector<IntVar>(W));
    LinearExpr objective;

    for (int l = 0; l < L; l++) {
        LinearExpr total;
        for (int w = 0; w < W; w++) {
            int max_positions = 0;
            if (!lessons[l].is_pp && lesson_week_allowed[l][w]) {
                for (int gd : week_day_indices[w]) {
                    const Date& date = all_days[gd];
                    if (!IsAvailable(date, lessons[l].group, unavailable)) continue;
                    if (lessons[l].teacher >= 0 && DateInUnavailableRanges(
                            date, lessons[l].teacher, teacher_unavailable)) continue;
                    if (lessons[l].is_block) {
                        for (int s = 0; s < SLOTS_PER_DAY - 1; s++) {
                            if (!IsAllowedUpStartSlot(date, s)) continue;
                            const bool group_ok = !group_work[lessons[l].group] ||
                                (WorkScheduleAllows(*group_work[lessons[l].group], date, s) &&
                                 WorkScheduleAllows(*group_work[lessons[l].group], date, s + 1));
                            const bool teacher_ok = lessons[l].teacher < 0 || !teacher_work[lessons[l].teacher] ||
                                (WorkScheduleAllows(*teacher_work[lessons[l].teacher], date, s) &&
                                 WorkScheduleAllows(*teacher_work[lessons[l].teacher], date, s + 1));
                            if (group_ok && teacher_ok) max_positions++;
                        }
                    } else {
                        for (int s = 0; s < SLOTS_PER_DAY; s++) {
                            const bool group_ok = !group_work[lessons[l].group] ||
                                WorkScheduleAllows(*group_work[lessons[l].group], date, s);
                            const bool teacher_ok = lessons[l].teacher < 0 || !teacher_work[lessons[l].teacher] ||
                                WorkScheduleAllows(*teacher_work[lessons[l].teacher], date, s);
                            if (group_ok && teacher_ok) max_positions++;
                        }
                    }
                }
            }
            max_positions = std::min(max_positions, lessons[l].total_slots);
            q[l][w] = model.NewIntVar(Domain(0, max_positions));
            total += q[l][w];

            auto lock_it = locked_slots.find({l, w});
            if (lock_it != locked_slots.end()) {
                int minimum = static_cast<int>(lock_it->second.size());
                if (lessons[l].is_block) minimum = (minimum + 1) / 2;
                model.AddGreaterOrEqual(q[l][w], minimum);
            }

            IntVar deviation = model.NewIntVar(Domain(0, lessons[l].total_slots));
            deviation_vars[l][w] = deviation;
            model.AddGreaterOrEqual(deviation, q[l][w] - initial[l][w]);
            model.AddGreaterOrEqual(deviation, initial[l][w] - q[l][w]);
            objective += deviation;
        }
        if (lessons[l].is_pp) {
            model.AddEquality(total, 0);
        } else {
            // Полная сумма нагрузки — обязательный инвариант, а не soft-цель.
            // Если допустимых недель/позиций недостаточно, balance обязан вернуть
            // INFEASIBLE вместо «успешного» расписания с потерянными часами.
            model.AddEquality(total, lessons[l].total_slots);
        }
    }

    for (int w = 0; w < W; w++) {
        for (int g = 0; g < GROUPS; g++) {
            int capacity = 0;
            for (int gd : week_day_indices[w]) {
                if (!IsAvailable(all_days[gd], g, unavailable)) continue;
                int allowed_slots = 0;
                for (int s = 0; s < SLOTS_PER_DAY; s++)
                    if (!group_work[g] || WorkScheduleAllows(*group_work[g], all_days[gd], s)) allowed_slots++;
                capacity += std::min(MAX_STUDENT_PAIRS_PER_DAY, allowed_slots);
            }
            for (int p = 0; p < group_part_count[g]; p++) {
                LinearExpr demand;
                for (int l = 0; l < L; l++) {
                    if (LessonAffectsPart(lessons[l], g, p))
                        demand += q[l][w] * (lessons[l].is_block ? 2 : 1);
                }
                model.AddLessOrEqual(demand, capacity);
            }
        }

        for (int teacher = 0; teacher < TEACHERS; teacher++) {
            int capacity = 0;
            for (int gd : week_day_indices[w])
                if (!DateInUnavailableRanges(all_days[gd], teacher, teacher_unavailable))
                    for (int s = 0; s < SLOTS_PER_DAY; s++)
                        if (!teacher_work[teacher] || WorkScheduleAllows(*teacher_work[teacher], all_days[gd], s)) capacity++;
            LinearExpr demand;
            for (int l = 0; l < L; l++) {
                if (lessons[l].teacher == teacher)
                    demand += q[l][w] * (lessons[l].is_block ? 2 : 1);
            }
            model.AddLessOrEqual(demand, capacity);
        }

        std::map<int, LinearExpr> room_demand;
        for (int l = 0; l < L; l++) {
            if (lessons[l].fixed_room >= 0 && !lessons[l].allow_room_substitution)
                room_demand[lessons[l].fixed_room] +=
                    q[l][w] * (lessons[l].is_block ? 2 : 1);
        }
        for (auto& entry : room_demand) {
            model.AddLessOrEqual(entry.second,
                static_cast<int>(week_day_indices[w].size()) * SLOTS_PER_DAY);
        }
    }

    CpModelProto feasibility_proto = model.Build();
    model.Minimize(objective);
    SatParameters params;
    params.set_num_search_workers(std::min(4, SOLVER_WORKERS));
    params.set_max_time_in_seconds(std::min(20.0, WEEK_TIME_LIMIT_SECONDS));
    params.set_random_seed(g_solver_config.random_seed);
    params.set_linearization_level(1);
    params.set_symmetry_level(2);

    SatParameters feasibility_params = params;
    feasibility_params.set_stop_after_first_solution(true);
    Model feasibility_model;
    feasibility_model.Add(NewSatParameters(feasibility_params));
    if (cancel_flag) {
        feasibility_model.GetOrCreate<operations_research::TimeLimit>()
            ->RegisterExternalBooleanAsLimit(cancel_flag);
    }
    CpSolverResponse feasibility_response = SolveCpModel(feasibility_proto, &feasibility_model);
    CpSolverResponse response = feasibility_response;
    CpModelProto proto = feasibility_proto;
    double quality_seconds = 0.0;

    if (!(cancel_flag && cancel_flag->load()) &&
        (feasibility_response.status() == CpSolverStatus::OPTIMAL ||
         feasibility_response.status() == CpSolverStatus::FEASIBLE)) {
        model.ClearHints();
        for (int l = 0; l < L; l++)
            for (int w = 0; w < W; w++) {
                const int q_value = static_cast<int>(
                    SolutionIntegerValue(feasibility_response, q[l][w]));
                model.AddHint(q[l][w], q_value);
                model.AddHint(deviation_vars[l][w], std::abs(q_value - initial[l][w]));
            }
        proto = model.Build();

        SatParameters quality_params = params;
        quality_params.set_max_time_in_seconds(10.0);
        quality_params.set_stop_after_first_solution(false);
        Model quality_model;
        quality_model.Add(NewSatParameters(quality_params));
        if (cancel_flag) {
            quality_model.GetOrCreate<operations_research::TimeLimit>()
                ->RegisterExternalBooleanAsLimit(cancel_flag);
        }
        CpSolverResponse quality_response = SolveCpModel(proto, &quality_model);
        quality_seconds = quality_response.wall_time();
        if (quality_response.status() == CpSolverStatus::OPTIMAL ||
            quality_response.status() == CpSolverStatus::FEASIBLE) {
            response = quality_response;
        }
    }

    result.solve_seconds = feasibility_response.wall_time() + quality_seconds;
    result.cancelled = cancel_flag && cancel_flag->load();
    result.status = result.cancelled ? "CANCELLED" : CpSolverStatus_Name(response.status());
    result.success = !result.cancelled &&
        (response.status() == CpSolverStatus::OPTIMAL ||
         response.status() == CpSolverStatus::FEASIBLE);

    JsonValue adjustments = JsonValue::MakeArray();
    if (result.success) {
        for (int l = 0; l < L; l++) {
            for (int w = 0; w < W; w++) {
                const int value = static_cast<int>(SolutionIntegerValue(response, q[l][w]));
                quotas[l][w] = value;
                if (value == initial[l][w]) continue;
                JsonValue item = JsonValue::MakeObject();
                item.At("lesson_id") = JsonValue::MakeNumber(lessons[l].id);
                item.At("lesson_name") = JsonValue::MakeString(lessons[l].name);
                item.At("week") = JsonValue::MakeNumber(w + 1);
                item.At("from") = JsonValue::MakeNumber(initial[l][w]);
                item.At("to") = JsonValue::MakeNumber(value);
                adjustments.array_value.push_back(item);
            }
        }
    }

    result.report.At("success") = JsonValue::MakeBool(result.success);
    result.report.At("cancelled") = JsonValue::MakeBool(result.cancelled);
    result.report.At("status") = JsonValue::MakeString(result.status);
    result.report.At("solve_seconds") = JsonValue::MakeNumber(result.solve_seconds);
    result.report.At("feasibility_seconds") = JsonValue::MakeNumber(feasibility_response.wall_time());
    result.report.At("quality_seconds") = JsonValue::MakeNumber(quality_seconds);
    result.report.At("variables") = JsonValue::MakeNumber(proto.variables_size());
    result.report.At("constraints") = JsonValue::MakeNumber(proto.constraints_size());
    result.report.At("adjustment_count") = JsonValue::MakeNumber(adjustments.array_value.size());
    result.report.At("adjustments") = adjustments;
    return result;
}

// Реконструирует и записывает все файлы расписания из flat-массива global_x_vals.
// Вызывается как после каждой недели (частичное), так и в конце (итоговое).
// print_stats=true — печатает статистику (окна, нарушения и т.д.).
static bool WriteScheduleFiles(
    const std::string& output_dir,
    const std::vector<std::vector<int>>& global_x_vals,
    int num_days,
    const std::vector<Lesson>& lessons,
    const std::vector<Date>& all_days,
    const std::vector<std::vector<int>>& solved_group_day_campus,
    const std::vector<GroupData>& groups,
    const std::vector<RoomData>& rooms,
    const std::map<int, std::vector<std::pair<Date, Date>>>& unavailable,
    const std::map<int, std::map<Date, std::string>>& unavailable_day_texts,
    bool print_stats
) {
    using operations_research::Domain;
    using operations_research::sat::BoolVar;
    using operations_research::sat::CpModelBuilder;
    using operations_research::sat::CpModelProto;
    using operations_research::sat::CpSolverResponse;
    using operations_research::sat::CpSolverStatus;
    using operations_research::sat::IntVar;
    using operations_research::sat::LinearExpr;
    using operations_research::sat::NewSatParameters;
    using operations_research::sat::SatParameters;
    using operations_research::sat::SolveCpModel;

    const int num_lessons = static_cast<int>(lessons.size());
    const int total_slots = num_days * SLOTS_PER_DAY;

    // Производные busy-значения
    std::vector<std::vector<int>> gbusy_v(GROUPS, std::vector<int>(total_slots, 0));
    std::vector<std::vector<std::vector<int>>> pbusy_v(
        GROUPS, std::vector<std::vector<int>>(PARTS_PER_GROUP, std::vector<int>(total_slots, 0))
    );
    std::vector<std::vector<int>> tbusy_v(TEACHERS, std::vector<int>(total_slots, 0));

    for (int l = 0; l < num_lessons; l++) {
        int g = lessons[l].group;
        int teacher = lessons[l].teacher;
        int base_sg = g * PARTS_PER_GROUP;
        for (int t = 0; t < total_slots; t++) {
            if (!global_x_vals[l][t]) continue;
            gbusy_v[g][t] = 1;
            if (teacher >= 0) tbusy_v[teacher][t] = 1;
            if (lessons[l].subgroup == -1) {
                for (int p = 0; p < PARTS_PER_GROUP; p++) pbusy_v[g][p][t] = 1;
            } else {
                int part = lessons[l].subgroup - base_sg;
                if (part >= 0 && part < PARTS_PER_GROUP) pbusy_v[g][part][t] = 1;
            }
        }
    }

    // Кампусы. Берём именно решение CP-SAT; вычисление только по занятиям с
    // одним разрешённым корпусом теряло выбор для гибких дисциплин.
    std::vector<std::vector<int>> g_campus_v(GROUPS, std::vector<int>(num_days, 0));
    std::vector<std::vector<int>> t_campus_v(TEACHERS, std::vector<int>(num_days, 0));
    for (int g = 0; g < GROUPS && g < static_cast<int>(solved_group_day_campus.size()); g++) {
        for (int d = 0; d < num_days && d < static_cast<int>(solved_group_day_campus[g].size()); d++) {
            g_campus_v[g][d] = solved_group_day_campus[g][d];
        }
    }
    for (int l = 0; l < num_lessons; l++) {
        int g = lessons[l].group;
        int teacher = lessons[l].teacher;
        for (int d = 0; d < num_days; d++) {
            for (int s = 0; s < SLOTS_PER_DAY; s++) {
                if (global_x_vals[l][d * SLOTS_PER_DAY + s]) {
                    if (teacher >= 0) t_campus_v[teacher][d] = g_campus_v[g][d];
                }
            }
        }
    }

    // Блоки для WriteTeachersTxt
    std::vector<BlockInfo> rec_blocks;
    for (int l = 0; l < num_lessons; l++) {
        if (!lessons[l].is_block) continue;
        BlockInfo bi;
        bi.lesson_id = l;
        for (int d = 0; d < num_days; d++) {
            if (!IsAvailable(all_days[d], lessons[l].group, unavailable)) continue;
            for (int s = 0; s < SLOTS_PER_DAY - 1; s++) {
                if (!IsAllowedUpStartSlot(all_days[d], s)) continue;
                bi.possible_starts.push_back(d * SLOTS_PER_DAY + s);
            }
        }
        rec_blocks.push_back(bi);
    }

    // Тривиальная фиксирующая модель
    CpModelBuilder fix_model;

    std::vector<std::vector<BoolVar>> fx(num_lessons, std::vector<BoolVar>(total_slots));
    for (int l = 0; l < num_lessons; l++)
        for (int t = 0; t < total_slots; t++) {
            fx[l][t] = fix_model.NewBoolVar();
            fix_model.AddEquality(fx[l][t], global_x_vals[l][t]);
        }

    for (auto& blk : rec_blocks) {
        int l = blk.lesson_id;
        for (int i = 0; i < static_cast<int>(blk.possible_starts.size()); i++)
            blk.start_vars.push_back(fix_model.NewBoolVar());
        for (int i = 0; i < static_cast<int>(blk.possible_starts.size()); i++) {
            int st = blk.possible_starts[i];
            int val = (global_x_vals[l][st] && global_x_vals[l][st + 1]) ? 1 : 0;
            fix_model.AddEquality(blk.start_vars[i], val);
        }
    }

    std::vector<std::vector<BoolVar>> fgb(GROUPS, std::vector<BoolVar>(total_slots));
    std::vector<std::vector<std::vector<BoolVar>>> fpb(
        GROUPS, std::vector<std::vector<BoolVar>>(PARTS_PER_GROUP, std::vector<BoolVar>(total_slots))
    );
    std::vector<std::vector<BoolVar>> ftb(TEACHERS, std::vector<BoolVar>(total_slots));

    for (int g = 0; g < GROUPS; g++)
        for (int t = 0; t < total_slots; t++) {
            fgb[g][t] = fix_model.NewBoolVar();
            fix_model.AddEquality(fgb[g][t], gbusy_v[g][t]);
        }
    for (int g = 0; g < GROUPS; g++)
        for (int p = 0; p < PARTS_PER_GROUP; p++)
            for (int t = 0; t < total_slots; t++) {
                fpb[g][p][t] = fix_model.NewBoolVar();
                fix_model.AddEquality(fpb[g][p][t], pbusy_v[g][p][t]);
            }
    for (int teacher = 0; teacher < TEACHERS; teacher++)
        for (int t = 0; t < total_slots; t++) {
            ftb[teacher][t] = fix_model.NewBoolVar();
            fix_model.AddEquality(ftb[teacher][t], tbusy_v[teacher][t]);
        }

    std::vector<std::vector<IntVar>> fgdc(GROUPS, std::vector<IntVar>(num_days));
    std::vector<std::vector<IntVar>> ftdc(TEACHERS, std::vector<IntVar>(num_days));
    for (int g = 0; g < GROUPS; g++)
        for (int d = 0; d < num_days; d++) {
            fgdc[g][d] = fix_model.NewIntVar(Domain(0, 1));
            fix_model.AddEquality(fgdc[g][d], g_campus_v[g][d]);
        }
    for (int teacher = 0; teacher < TEACHERS; teacher++)
        for (int d = 0; d < num_days; d++) {
            ftdc[teacher][d] = fix_model.NewIntVar(Domain(0, 1));
            fix_model.AddEquality(ftdc[teacher][d], t_campus_v[teacher][d]);
        }

    SatParameters fix_params;
    fix_params.set_num_search_workers(1);
    fix_params.set_max_time_in_seconds(10.0);
    fix_params.set_stop_after_first_solution(true);

    operations_research::sat::Model fix_sat;
    fix_sat.Add(NewSatParameters(fix_params));
    CpModelProto fix_proto = fix_model.Build();
    CpSolverResponse fix_resp = SolveCpModel(fix_proto, &fix_sat);

    if (fix_resp.status() != CpSolverStatus::OPTIMAL &&
        fix_resp.status() != CpSolverStatus::FEASIBLE) {
        std::cerr << "WriteScheduleFiles: reconstruction failed\n";
        return false;
    }

    if (print_stats) {
        std::vector<std::vector<BoolVar>> student_ents;
        for (int g = 0; g < GROUPS; g++)
            for (int p = 0; p < PARTS_PER_GROUP; p++)
                student_ents.push_back(fpb[g][p]);

        std::cout << "\nСтатистика расписания:\n";
        std::cout << "  Окон у студентов: " << CountWindows(fix_resp, student_ents, num_days) << "\n";
        std::cout << "  Окон у преподавателей: " << CountWindows(fix_resp, ftb, num_days) << "\n";
        std::cout << "  Макс. пар у студента в день: " << MaxStudentPairsInDay(fix_resp, fpb, num_days) << "\n";
        std::cout << "  Дней по 5 пар у подгрупп: " << CountFivePairStudentDays(fix_resp, fpb, num_days) << "\n";
        std::cout << "  Нарушений правила УП-день: " << CountUpDayRuleViolations(fix_resp, lessons, fx, fpb, num_days) << "\n";
        std::cout << "  Нарушений занятости препод. во время УП: "
                  << CountUpTeacherLockViolations(fix_resp, lessons, fx, rec_blocks, all_days) << "\n";
    }

    const std::filesystem::path out_dir(output_dir);
    const std::filesystem::path groups_dir = out_dir / "groups";
    std::filesystem::create_directories(out_dir);
    std::filesystem::create_directories(groups_dir);

    RoomAllocationResult room_allocation = AllocateRooms(
        lessons, groups, rooms, all_days, global_x_vals, solved_group_day_campus);
    const RoomAssignmentMap* room_assignments = room_allocation.inventory_configured
        ? &room_allocation.assignments : nullptr;

    std::vector<std::vector<BoolVar>> student_ents;
    for (int g = 0; g < GROUPS; g++)
        for (int p = 0; p < PARTS_PER_GROUP; p++)
            student_ents.push_back(fpb[g][p]);
    const int student_windows = CountWindows(fix_resp, student_ents, num_days);
    const int teacher_windows = CountWindows(fix_resp, ftb, num_days);
    const int max_student_pairs = MaxStudentPairsInDay(fix_resp, fpb, num_days);
    const int student_days_at_daily_limit = CountStudentDaysWithPairCount(
        fix_resp, fpb, num_days, MAX_STUDENT_PAIRS_PER_DAY);
    const int five_pair_days = CountFivePairStudentDays(fix_resp, fpb, num_days);
    const int up_day_violations = CountUpDayRuleViolations(
        fix_resp, lessons, fx, fpb, num_days);
    const int up_teacher_lock_violations = CountUpTeacherLockViolations(
        fix_resp, lessons, fx, rec_blocks, all_days);
    WriteBackendReports(out_dir, lessons, global_x_vals, student_windows,
        teacher_windows, max_student_pairs, student_days_at_daily_limit,
        five_pair_days, up_day_violations,
        up_teacher_lock_violations, room_allocation);

    WriteAllGroupsTxt(
        (out_dir / "raspisanie_all.txt").string(),
        fix_resp, all_days, lessons, fx, fgb, fgdc, unavailable_day_texts);

    for (int g = 0; g < GROUPS; g++) {
        WriteGroupScheduleTxt(
            (groups_dir / ("raspisanie_group_" + std::to_string(g) + ".txt")).string(),
            fix_resp, all_days, lessons, fx, fgb, fgdc, unavailable_day_texts, g);
    }

    WriteGroupsCsv(
        (out_dir / "raspisanie_groups.csv").string(),
        fix_resp, all_days, lessons, fx, fgb, fgdc, unavailable_day_texts);

    WriteTeachersTxt(
        (out_dir / "raspisanie_teachers.txt").string(),
        fix_resp, all_days, lessons, fx, rec_blocks, ftb, ftdc);

    WriteAllGroupsJson(
        (out_dir / "schedule_all.json").string(),
        fix_resp, all_days, lessons, fx, fgb, fgdc, unavailable_day_texts,
        room_assignments);

    for (int g = 0; g < GROUPS; g++) {
        WriteGroupJson(
            (groups_dir / ("group_" + std::to_string(g) + ".json")).string(),
            fix_resp, all_days, lessons, fx, fgb, fgdc, unavailable_day_texts, g,
            room_assignments);
    }

    return true;
}

// Точная недельная модель всегда применяет весь набор hard-ограничений.
// Ослабленный fallback намеренно отсутствует: несовместимые квоты должны дать
// явный failure и могут ремонтироваться только с повторной проверкой этой модели.
static WeekSolveResult SolveOneWeek(
    int week_num,
    const std::vector<int>& wdix,
    const std::vector<Date>& all_days,
    const std::vector<Lesson>& lessons,
    const std::vector<GroupData>& groups,
    const std::vector<TeacherData>& teachers,
    const std::vector<RoomData>& rooms,
    const std::map<int, std::vector<std::pair<Date, Date>>>& unavailable,
    const std::map<int, std::vector<std::pair<Date, Date>>>& teacher_unavailable,
    const std::vector<int>& quotas,
    const std::vector<LockedAssignment>& locked,
    const std::map<std::pair<int, int>, int>& initial_prior_theory,
    const WeekSolveResult* warm_start,
    std::atomic<bool>* cancel_flag
) {
    using operations_research::Domain;
    using operations_research::sat::BoolVar;
    using operations_research::sat::CpModelBuilder;
    using operations_research::sat::CpModelProto;
    using operations_research::sat::CpSolverResponse;
    using operations_research::sat::CpSolverStatus;
    using operations_research::sat::CpSolverStatus_Name;
    using operations_research::sat::IntVar;
    using operations_research::sat::LinearExpr;
    using operations_research::sat::NewSatParameters;
    using operations_research::sat::SatParameters;
    using operations_research::sat::SolveCpModel;
    using operations_research::sat::SolutionIntegerValue;

    const int num_lessons = static_cast<int>(lessons.size());
    const int W = static_cast<int>(wdix.size());
    const int local_slots = W * SLOTS_PER_DAY;
    const auto model_build_started = std::chrono::steady_clock::now();

    // Дни недели (локальный индекс → Date)
    std::vector<Date> week_days;
    week_days.reserve(W);
    for (int gd : wdix) week_days.push_back(all_days[gd]);

    std::vector<const WorkSchedule*> group_work(GROUPS, nullptr);
    std::vector<int> group_part_count(GROUPS, PARTS_PER_GROUP);
    for (const GroupData& group : groups)
        if (group.id >= 0 && group.id < GROUPS) {
            group_work[group.id] = &group.work_schedule;
            group_part_count[group.id] = std::clamp(group.parts, 1, PARTS_PER_GROUP);
        }
    std::vector<const WorkSchedule*> teacher_work(TEACHERS, nullptr);
    for (const TeacherData& teacher : teachers)
        if (teacher.id >= 0 && teacher.id < TEACHERS) teacher_work[teacher.id] = &teacher.work_schedule;

    CpModelBuilder model;
    LinearExpr objective;

    // x[l][lt] — только для уроков с quota > 0
    std::vector<std::vector<BoolVar>> x(num_lessons, std::vector<BoolVar>(local_slots));
    for (int l = 0; l < num_lessons; l++) {
        if (quotas[l] == 0) continue;
        for (int lt = 0; lt < local_slots; lt++) x[l][lt] = model.NewBoolVar();
    }

    // Индексы активных уроков исключают повторные полные проходы по ~1000
    // строкам при построении каждого слота группы/преподавателя.
    std::vector<std::vector<int>> active_group_lessons(GROUPS);
    std::vector<std::vector<int>> active_teacher_lessons(TEACHERS);
    std::map<int, std::vector<int>> active_room_lessons;
    for (int l = 0; l < num_lessons; l++) {
        if (quotas[l] <= 0) continue;
        if (lessons[l].group >= 0 && lessons[l].group < GROUPS)
            active_group_lessons[lessons[l].group].push_back(l);
        if (lessons[l].teacher >= 0 && lessons[l].teacher < TEACHERS)
            active_teacher_lessons[lessons[l].teacher].push_back(l);
        if (lessons[l].fixed_room >= 0 && !lessons[l].allow_room_substitution)
            active_room_lessons[lessons[l].fixed_room].push_back(l);
    }

    // ── Блочные (УП) уроки ─────────────────────────────────────────────────
    struct LocalBlockInfo {
        int lesson_id;
        std::vector<int> possible_starts;
        std::vector<BoolVar> start_vars;
    };
    std::vector<LocalBlockInfo> blocks;

    for (int l = 0; l < num_lessons; l++) {
        if (!lessons[l].is_block || quotas[l] == 0) continue;

        LocalBlockInfo bi;
        bi.lesson_id = l;

        for (int ld = 0; ld < W; ld++) {
            if (!IsAvailable(week_days[ld], lessons[l].group, unavailable)) continue;
            for (int s = 0; s < SLOTS_PER_DAY - 1; s++) {
                if (!IsAllowedUpStartSlot(week_days[ld], s)) continue;
                const int group = lessons[l].group;
                const int teacher = lessons[l].teacher;
                if (group >= 0 && group < GROUPS && group_work[group] &&
                    (!WorkScheduleAllows(*group_work[group], week_days[ld], s) ||
                     !WorkScheduleAllows(*group_work[group], week_days[ld], s + 1))) continue;
                if (teacher >= 0 && teacher < TEACHERS && teacher_work[teacher] &&
                    (!WorkScheduleAllows(*teacher_work[teacher], week_days[ld], s) ||
                     !WorkScheduleAllows(*teacher_work[teacher], week_days[ld], s + 1))) continue;
                bi.possible_starts.push_back(ld * SLOTS_PER_DAY + s);
            }
        }
        for (int i = 0; i < static_cast<int>(bi.possible_starts.size()); i++)
            bi.start_vars.push_back(model.NewBoolVar());

        blocks.push_back(bi);
    }

    // ── Ограничения суммы ──────────────────────────────────────────────────
    for (int l = 0; l < num_lessons; l++) {
        if (quotas[l] == 0 || lessons[l].is_block) continue;
        LinearExpr sum;
        for (int lt = 0; lt < local_slots; lt++) sum += x[l][lt];
        model.AddEquality(sum, quotas[l]);
    }

    // ── Рабочие интервалы по дням и номерам пар ─────────────────────────
    for (int l = 0; l < num_lessons; l++) {
        if (quotas[l] == 0) continue;
        const int group = lessons[l].group;
        const int teacher = lessons[l].teacher;
        for (int ld = 0; ld < W; ld++) {
            for (int s = 0; s < SLOTS_PER_DAY; s++) {
                const bool group_allowed = group < 0 || group >= GROUPS || !group_work[group] ||
                    WorkScheduleAllows(*group_work[group], week_days[ld], s);
                const bool teacher_allowed = teacher < 0 || teacher >= TEACHERS || !teacher_work[teacher] ||
                    WorkScheduleAllows(*teacher_work[teacher], week_days[ld], s);
                if (!group_allowed || !teacher_allowed)
                    model.AddEquality(x[l][ld * SLOTS_PER_DAY + s], 0);
            }
        }
    }

    for (auto& blk : blocks) {
        int l = blk.lesson_id;
        int req = quotas[l];

        if (static_cast<int>(blk.possible_starts.size()) < req) {
            WeekSolveResult res;
            res.success = false;
            res.status = "NO_STARTS_W" + std::to_string(week_num);
            return res;
        }

        {
            LinearExpr ss;
            for (const auto& v : blk.start_vars) ss += v;
            model.AddEquality(ss, req);
        }

        std::vector<std::vector<BoolVar>> covers(local_slots);
        for (int i = 0; i < static_cast<int>(blk.possible_starts.size()); i++) {
            int st = blk.possible_starts[i];
            covers[st].push_back(blk.start_vars[i]);
            covers[st + 1].push_back(blk.start_vars[i]);
        }
        for (int lt = 0; lt < local_slots; lt++) {
            LinearExpr cs;
            for (const auto& v : covers[lt]) cs += v;
            model.AddEquality(x[l][lt], cs);
        }
    }

    // ── Зафиксированные слоты (locked) ────────────────────────────────────
    {
        std::map<int, int> lid_to_l;
        for (int l = 0; l < num_lessons; l++) lid_to_l[lessons[l].id] = l;

        for (const LockedAssignment& a : locked) {
            auto it = lid_to_l.find(a.lesson_id);
            if (it == lid_to_l.end()) continue;
            int l = it->second;
            if (quotas[l] == 0) continue;
            int local_d = -1;
            for (int ld = 0; ld < W; ld++) {
                if (week_days[ld] == a.date) { local_d = ld; break; }
            }
            if (local_d < 0 || a.slot < 0 || a.slot >= SLOTS_PER_DAY) continue;
            int lt = local_d * SLOTS_PER_DAY + a.slot;
            model.AddEquality(x[l][lt], 1);
        }
    }

    // ── Недоступные дни ───────────────────────────────────────────────────
    for (int ld = 0; ld < W; ld++) {
        for (int g = 0; g < GROUPS; g++) {
            if (IsAvailable(week_days[ld], g, unavailable)) continue;
            for (int s = 0; s < SLOTS_PER_DAY; s++) {
                int lt = ld * SLOTS_PER_DAY + s;
                for (int l : active_group_lessons[g]) model.AddEquality(x[l][lt], 0);
            }
        }
    }

    for (int l = 0; l < num_lessons; l++) {
        if (quotas[l] == 0 || lessons[l].teacher < 0) continue;
        for (int ld = 0; ld < W; ld++) {
            if (!DateInUnavailableRanges(week_days[ld], lessons[l].teacher, teacher_unavailable)) continue;
            for (int s = 0; s < SLOTS_PER_DAY; s++) {
                model.AddEquality(x[l][ld * SLOTS_PER_DAY + s], 0);
            }
        }
    }

    // ── Теория до ЛПЗ ───────────────────────────────────────────────────
    // Первая ЛПЗ по предмету допускается только после заданного количества
    // уже проведённых теоретических пар. Для второй недели учитываем теорию из
    // найденной первой недели. Строки ЛПЗ без теоретической строки заранее
    // исключаются из двухнедельного среза подготовительным скриптом.
    for (int lab = 0; lab < num_lessons; lab++) {
        if (quotas[lab] <= 0 || !lessons[lab].is_lab) continue;
        std::vector<int> theory_lessons;
        for (int theory = 0; theory < num_lessons; theory++) {
            if (lessons[theory].is_lab || lessons[theory].is_block || lessons[theory].is_pp)
                continue;
            if (lessons[theory].group == lessons[lab].group &&
                lessons[theory].subject_id == lessons[lab].subject_id) {
                theory_lessons.push_back(theory);
            }
        }
        if (theory_lessons.empty()) continue;

        int prior_theory = 0;
        const auto initial_it = initial_prior_theory.find(
            {lessons[lab].group, lessons[lab].subject_id});
        if (initial_it != initial_prior_theory.end()) prior_theory += initial_it->second;
        if (warm_start && warm_start->success) {
            for (int theory : theory_lessons) {
                if (theory >= static_cast<int>(warm_start->x_vals.size())) continue;
                prior_theory += static_cast<int>(std::count(
                    warm_start->x_vals[theory].begin(),
                    warm_start->x_vals[theory].end(), 1));
            }
        }

        int required_theory = MIN_INITIAL_THEORY_SLOTS_BEFORE_LABS;
        if (STRICT_ALL_THEORY_BEFORE_LABS) {
            required_theory = prior_theory;
            for (int theory : theory_lessons)
                required_theory += std::max(0, quotas[theory]);
        }

        for (int lt = 0; lt < local_slots; lt++) {
            LinearExpr theory_before;
            theory_before += prior_theory;
            for (int theory : theory_lessons) {
                if (quotas[theory] <= 0) continue;
                for (int earlier = 0; earlier < lt; earlier++)
                    theory_before += x[theory][earlier];
            }
            model.AddGreaterOrEqual(
                theory_before,
                x[lab][lt] * required_theory);
        }
    }

    // ── ПП: только в разрешённые дни ──────────────────────────────────────
    // ПП расставляется детерминированно вне модели — здесь делать нечего.
    // (is_pp уроки имеют quota==0, переменных не получают.)

    // ── group_busy, part_busy ─────────────────────────────────────────────
    std::vector<std::vector<BoolVar>> group_busy(GROUPS, std::vector<BoolVar>(local_slots));
    std::vector<std::vector<std::vector<BoolVar>>> part_busy(
        GROUPS,
        std::vector<std::vector<BoolVar>>(PARTS_PER_GROUP, std::vector<BoolVar>(local_slots))
    );

    for (int g = 0; g < GROUPS; g++) {
        int base_sg = g * PARTS_PER_GROUP;
        for (int lt = 0; lt < local_slots; lt++) {
            LinearExpr whole_sum;
            LinearExpr sub_sum[PARTS_PER_GROUP];

            for (int l : active_group_lessons[g]) {
                if (lessons[l].subgroup == -1) {
                    whole_sum += x[l][lt];
                } else {
                    int part = lessons[l].subgroup - base_sg;
                    if (part >= 0 && part < PARTS_PER_GROUP) sub_sum[part] += x[l][lt];
                }
            }

            model.AddLessOrEqual(whole_sum, 1);
            for (int p = 0; p < PARTS_PER_GROUP; p++) {
                model.AddLessOrEqual(sub_sum[p], 1);
                LinearExpr wp; wp += whole_sum; wp += sub_sum[p];
                model.AddLessOrEqual(wp, 1);
            }

            LinearExpr gss; gss += whole_sum;
            for (int p = 0; p < PARTS_PER_GROUP; p++) gss += sub_sum[p];
            group_busy[g][lt] = MakePositiveIndicator(model, gss);

            for (int p = 0; p < PARTS_PER_GROUP; p++) {
                LinearExpr pss; pss += whole_sum; pss += sub_sum[p];
                part_busy[g][p][lt] = MakePositiveIndicator(model, pss);
            }
        }
    }

    std::vector<std::vector<BoolVar>> student_entities;
    for (int g = 0; g < GROUPS; g++)
        for (int p = 0; p < group_part_count[g]; p++)
            student_entities.push_back(part_busy[g][p]);

    std::vector<bool> active_subject_lessons(num_lessons, false);
    for (int l = 0; l < num_lessons; l++) active_subject_lessons[l] = quotas[l] > 0;
    if (HARD_MAX_TWO_SAME_SUBJECT_PER_DAY) {
        AddMaxSameSubjectPerDay(
            model, lessons, x, W, active_subject_lessons,
            MAX_WHOLE_GROUP_SAME_SUBJECT_PAIRS_PER_DAY,
            MAX_SAME_SUBJECT_PAIRS_PER_DAY);
    }

    // ── Правило УП-день ───────────────────────────────────────────────────
    for (int g = 0; g < GROUPS; g++) {
        for (int p = 0; p < group_part_count[g]; p++) {
            for (int ld = 0; ld < W; ld++) {
                std::vector<BoolVar> up_starts;
                for (const auto& blk : blocks) {
                    if (!LessonAffectsPart(lessons[blk.lesson_id], g, p)) continue;
                    for (int i = 0; i < static_cast<int>(blk.possible_starts.size()); i++) {
                        if (blk.possible_starts[i] / SLOTS_PER_DAY == ld)
                            up_starts.push_back(blk.start_vars[i]);
                    }
                }
                if (up_starts.empty()) continue;

                LinearExpr uss;
                for (const auto& v : up_starts) uss += v;
                model.AddLessOrEqual(uss, 1);

                // УП занимает весь учебный день физической части группы.
                LinearExpr day_sum;
                for (int s = 0; s < SLOTS_PER_DAY; s++)
                    day_sum += part_busy[g][p][ld * SLOTS_PER_DAY + s];

                BoolVar has_up = MakePositiveIndicator(model, uss);
                LinearExpr req2; req2 += uss; req2 += uss;
                model.AddEquality(day_sum, req2).OnlyEnforceIf(has_up);
            }
        }
    }

    // ── teacher_busy ──────────────────────────────────────────────────────
    std::vector<std::vector<BoolVar>> teacher_busy(TEACHERS, std::vector<BoolVar>(local_slots));
    for (int teacher = 0; teacher < TEACHERS; teacher++) {
        for (int lt = 0; lt < local_slots; lt++) {
            LinearExpr sum;
            for (int l : active_teacher_lessons[teacher]) sum += x[l][lt];
            model.AddLessOrEqual(sum, 1);
            teacher_busy[teacher][lt] = MakePositiveIndicator(model, sum);
        }
    }

    // Один закреплённый кабинет не может одновременно обслуживать два занятия.
    for (const auto& room_entry : active_room_lessons) {
        for (int lt = 0; lt < local_slots; lt++) {
            LinearExpr room_sum;
            for (int l : room_entry.second) room_sum += x[l][lt];
            model.AddLessOrEqual(room_sum, 1);
        }
    }

    // ── Преподаватель заблокирован во время УП ────────────────────────────
    for (const auto& blk : blocks) {
        int teacher = lessons[blk.lesson_id].teacher;
        if (teacher < 0 || teacher >= TEACHERS) continue;
        for (int i = 0; i < static_cast<int>(blk.possible_starts.size()); i++) {
            int start_lt = blk.possible_starts[i];
            // week_days используется как локальный all_days → возвращает локальные слоты
            std::vector<int> blocked = TeacherBlockedSlotsForUpStart(week_days, start_lt);
            for (int blt : blocked) {
                LinearExpr ord;
                for (int other : active_teacher_lessons[teacher]) {
                    if (!lessons[other].is_block) ord += x[other][blt];
                }
                model.AddEquality(ord, 0).OnlyEnforceIf(blk.start_vars[i]);
            }
        }
    }

    // ── Нельзя двум УП одного преподавателя пересекаться по времени ──────
    {
        struct UpRef { int bi, si, teacher, lt; TimeInterval iv; };
        std::vector<UpRef> up_refs;
        for (int b = 0; b < static_cast<int>(blocks.size()); b++) {
            const auto& blk = blocks[b];
            int teacher = lessons[blk.lesson_id].teacher;
            for (int i = 0; i < static_cast<int>(blk.possible_starts.size()); i++) {
                int lt = blk.possible_starts[i];
                int ld = lt / SLOTS_PER_DAY;
                up_refs.push_back({b, i, teacher, lt,
                    UpIntervalForStartSlot(week_days[ld], lt % SLOTS_PER_DAY)});
            }
        }
        for (int a = 0; a < static_cast<int>(up_refs.size()); a++) {
            for (int b = a + 1; b < static_cast<int>(up_refs.size()); b++) {
                const auto& la = up_refs[a]; const auto& lb = up_refs[b];
                if (la.teacher < 0 || la.teacher != lb.teacher) continue;
                if (la.lt / SLOTS_PER_DAY != lb.lt / SLOTS_PER_DAY) continue;
                if (!IntervalsOverlap(la.iv, lb.iv)) continue;
                LinearExpr both;
                both += blocks[la.bi].start_vars[la.si];
                both += blocks[lb.bi].start_vars[lb.si];
                model.AddLessOrEqual(both, 1);
            }
        }
    }

    // ── Недельная нагрузка каждой физической части группы ──────────────────
    // Параллельные 3+3 пары двух подгрупп — это по 3 пары для каждой части,
    // а не 6 последовательных пар одного студента. УП при этом занимает два
    // pair-slot в календаре конкретной части.
    const std::vector<std::vector<int>> group_part_week_total =
        ComputeGroupPartWeeklyOccupiedPairs(
            lessons, quotas, GROUPS, group_part_count);

    // ── Макс/мин пар в день ───────────────────────────────────────────────
    std::vector<std::vector<std::vector<BoolVar>>> student_day_has(
        GROUPS,
        std::vector<std::vector<BoolVar>>(PARTS_PER_GROUP, std::vector<BoolVar>(W))
    );
    std::vector<BoolVar> five_pair_vars;
    std::vector<BoolVar> two_pair_vars;

    for (int g = 0; g < GROUPS; g++) {
        for (int p = 0; p < group_part_count[g]; p++) {
            const int eff_min = EffectiveStudentDailyMinimum(
                MIN_STUDENT_PAIRS_PER_STUDY_DAY,
                group_part_week_total[g][p]);
            LinearExpr two_pair_days;
            for (int ld = 0; ld < W; ld++) {
                LinearExpr ds;
                for (int s = 0; s < SLOTS_PER_DAY; s++)
                    ds += part_busy[g][p][ld * SLOTS_PER_DAY + s];

                BoolVar has = MakePositiveIndicator(model, ds);
                student_day_has[g][p][ld] = has;
                if (eff_min > 0) AddMinIfPositive(model, ds, has, eff_min);
                model.AddLessOrEqual(ds, MAX_STUDENT_PAIRS_PER_DAY);

                BoolVar is5 = model.NewBoolVar();
                model.AddEquality(ds, MAX_STUDENT_PAIRS_PER_DAY).OnlyEnforceIf(is5);
                model.AddLessOrEqual(ds, MAX_STUDENT_PAIRS_PER_DAY - 1).OnlyEnforceIf(is5.Not());
                five_pair_vars.push_back(is5);

                BoolVar is2 = model.NewBoolVar();
                model.AddEquality(ds, 2).OnlyEnforceIf(is2);
                model.AddNotEqual(ds, 2).OnlyEnforceIf(is2.Not());
                two_pair_vars.push_back(is2);
                two_pair_days += is2;
            }
            if (HARD_MAX_ONE_TWO_PAIR_STUDENT_DAY)
                model.AddLessOrEqual(two_pair_days, 1);
        }
    }

    // ── Синхронизация подгрупп ────────────────────────────────────────────
    // Учебные дни физических частей группы синхронизированы как hard-правило.
    for (int g = 0; g < GROUPS; g++)
        if (group_part_count[g] > 1)
            for (int ld = 0; ld < W; ld++)
                model.AddEquality(student_day_has[g][0][ld], student_day_has[g][1][ld]);

    // ── Мин. учебных дней в неделю ────────────────────────────────────────
    for (int g = 0; g < GROUPS; g++) {
        std::vector<int> avail_lds;
        for (int ld = 0; ld < W; ld++)
            if (IsAvailable(week_days[ld], g, unavailable)) avail_lds.push_back(ld);
        if (avail_lds.empty()) continue;

        const int req_d = std::max(0, std::min(
            MIN_STUDENT_STUDY_DAYS_PER_WEEK,
            static_cast<int>(avail_lds.size())));
        for (int p = 0; p < group_part_count[g]; ++p) {
            if (group_part_week_total[g][p] == 0) continue;
            LinearExpr wsd;
            for (int ld : avail_lds) wsd += student_day_has[g][p][ld];

            if (HARD_MIN_STUDY_DAYS_PER_WEEK) {
                model.AddGreaterOrEqual(wsd, req_d);
            } else if (USE_QUALITY_OBJECTIVE && req_d > 0) {
                IntVar miss = model.NewIntVar(Domain(0, req_d));
                LinearExpr wm; wm += wsd; wm += miss;
                model.AddGreaterOrEqual(wm, req_d);
                objective += miss * GROUP_WEEK_MISSING_DAY_WEIGHT;
            }
        }
    }

    // ── Мин 2 пары преподавателю ──────────────────────────────────────────
    if (HARD_MIN_2_TEACHER_PAIRS_PER_DAY) {
        for (int teacher = 0; teacher < TEACHERS; teacher++) {
            for (int ld = 0; ld < W; ld++) {
                LinearExpr ds;
                for (int s = 0; s < SLOTS_PER_DAY; s++)
                    ds += teacher_busy[teacher][ld * SLOTS_PER_DAY + s];
                AddMin2IfPositive(model, ds);
            }
        }
    }

    // ── Без окон (жёстко) ─────────────────────────────────────────────────
    if (HARD_NO_STUDENT_WINDOWS) AddNoWindowsHard(model, student_entities, W);
    if (HARD_NO_TEACHER_WINDOWS) AddNoWindowsHard(model, teacher_busy, W);

    // ── Кампус ────────────────────────────────────────────────────────────
    std::vector<std::vector<IntVar>> group_day_campus(GROUPS, std::vector<IntVar>(W));
    std::vector<std::vector<IntVar>> teacher_day_campus(TEACHERS, std::vector<IntVar>(W));
    std::vector<std::vector<BoolVar>> teacher_day_has(TEACHERS, std::vector<BoolVar>(W));
    for (int g = 0; g < GROUPS; g++)
        for (int ld = 0; ld < W; ld++)
            group_day_campus[g][ld] = model.NewIntVar(Domain(0, 1));
    for (int t = 0; t < TEACHERS; t++)
        for (int ld = 0; ld < W; ld++)
            teacher_day_campus[t][ld] = model.NewIntVar(Domain(0, 1));

    // В день без занятий значение корпуса не имеет смысла. Раньше оно
    // оставалось свободным и порождало 2^N эквивалентных ветвей поиска.
    for (int g = 0; g < GROUPS; g++) {
        for (int ld = 0; ld < W; ld++) {
            LinearExpr activity;
            for (int s = 0; s < SLOTS_PER_DAY; s++)
                activity += group_busy[g][ld * SLOTS_PER_DAY + s];
            model.AddLessOrEqual(group_day_campus[g][ld], activity);
        }
    }
    for (int teacher = 0; teacher < TEACHERS; teacher++) {
        for (int ld = 0; ld < W; ld++) {
            LinearExpr activity;
            for (int s = 0; s < SLOTS_PER_DAY; s++)
                activity += teacher_busy[teacher][ld * SLOTS_PER_DAY + s];
            teacher_day_has[teacher][ld] = MakePositiveIndicator(model, activity);
            model.AddLessOrEqual(teacher_day_campus[teacher][ld], teacher_day_has[teacher][ld]);
        }
    }
    for (const TeacherData& teacher : teachers) {
        if (teacher.id < 0 || teacher.id >= TEACHERS || teacher.max_work_days_per_week <= 0) continue;
        LinearExpr work_days;
        for (int ld = 0; ld < W; ld++) work_days += teacher_day_has[teacher.id][ld];
        model.AddLessOrEqual(work_days, teacher.max_work_days_per_week);
    }
    for (const TeacherData& teacher : teachers) {
        const int daily_limit = EffectiveTeacherMaxPairsPerDay(teacher.max_pairs_per_day);
        if (teacher.id < 0 || teacher.id >= TEACHERS || daily_limit == 0) continue;
        for (int ld = 0; ld < W; ld++) {
            LinearExpr day_pairs;
            for (int s = 0; s < SLOTS_PER_DAY; s++)
                day_pairs += teacher_busy[teacher.id][ld * SLOTS_PER_DAY + s];
            model.AddLessOrEqual(day_pairs, daily_limit);
        }
    }

    for (int l = 0; l < num_lessons; l++) {
        if (quotas[l] == 0) continue;
        int g = lessons[l].group;
        int teacher = lessons[l].teacher;
        for (int ld = 0; ld < W; ld++) {
            for (int s = 0; s < SLOTS_PER_DAY; s++) {
                int lt = ld * SLOTS_PER_DAY + s;
                if (teacher >= 0)
                    model.AddEquality(group_day_campus[g][ld], teacher_day_campus[teacher][ld])
                        .OnlyEnforceIf(x[l][lt]);
                if (lessons[l].allowed_campuses.size() == 1) {
                    int campus = static_cast<int>(*lessons[l].allowed_campuses.begin());
                    model.AddEquality(group_day_campus[g][ld], campus).OnlyEnforceIf(x[l][lt]);
                    if (teacher >= 0)
                        model.AddEquality(teacher_day_campus[teacher][ld], campus).OnlyEnforceIf(x[l][lt]);
                }
            }
        }
    }

    // ── Вместимость аудиторного фонда по корпусу ────────────────────────
    // Кабинеты назначаются после CP-SAT, но без этих ограничений модель могла
    // одновременно отправить почти все группы в один корпус. В результате
    // корректная по преподавателям сетка получала десятки занятий без комнаты.
    for (int ld = 0; ld < W; ld++) {
        for (int s = 0; s < SLOTS_PER_DAY; s++) {
            const int lt = ld * SLOTS_PER_DAY + s;
            int room_capacity_by_campus[2] = {0, 0};
            int sports_capacity_by_campus[2] = {0, 0};
            for (const RoomData& room : rooms) {
                if (!room.active || room.access_mode == "blocked" ||
                    room.campus < LESNAYA || room.campus > KRIVOUSOVA) continue;
                if (!room.available_slots.empty() && !room.available_slots.count(s + 1)) continue;
                if (!WorkScheduleAllows(room.work_schedule, week_days[ld], s)) continue;
                if (room.purpose == "sports_hall") {
                    sports_capacity_by_campus[room.campus]++;
                } else if (room.access_mode != "exclusive") {
                    room_capacity_by_campus[room.campus]++;
                }
            }
            LinearExpr campus0_demand;
            LinearExpr campus1_demand;
            LinearExpr sports0_demand;
            LinearExpr sports1_demand;
            for (int l = 0; l < num_lessons; l++) {
                if (quotas[l] <= 0) continue;
                BoolVar at_campus1 = model.NewBoolVar();
                const IntVar& campus = group_day_campus[lessons[l].group][ld];
                model.AddLessOrEqual(at_campus1, x[l][lt]);
                model.AddLessOrEqual(at_campus1, campus);
                LinearExpr lower;
                lower += x[l][lt];
                lower += campus;
                lower -= 1;
                model.AddGreaterOrEqual(at_campus1, lower);
                LinearExpr& demand0 = lessons[l].required_room_purpose == "sports_hall" ? sports0_demand : campus0_demand;
                LinearExpr& demand1 = lessons[l].required_room_purpose == "sports_hall" ? sports1_demand : campus1_demand;
                demand1 += at_campus1;
                demand0 += x[l][lt];
                demand0 -= at_campus1;
            }
            model.AddLessOrEqual(campus0_demand, room_capacity_by_campus[LESNAYA]);
            model.AddLessOrEqual(campus1_demand, room_capacity_by_campus[KRIVOUSOVA]);
            model.AddLessOrEqual(sports0_demand, sports_capacity_by_campus[LESNAYA]);
            model.AddLessOrEqual(sports1_demand, sports_capacity_by_campus[KRIVOUSOVA]);
        }
    }

    // Тёплый старт: если недельная квота занятия совпадает с предыдущей,
    // предлагаем CP-SAT уже найденный рисунок слотов. Hint не является
    // ограничением — при новых конфликтах solver свободно его исправляет.
    int warm_hint_count = 0;
    if (warm_start && warm_start->success) {
        for (int l = 0; l < num_lessons; l++) {
            if (quotas[l] <= 0 || l >= static_cast<int>(warm_start->x_vals.size()) ||
                static_cast<int>(warm_start->x_vals[l].size()) != local_slots) continue;
            const int previous_occupied = static_cast<int>(std::count(
                warm_start->x_vals[l].begin(), warm_start->x_vals[l].end(), 1));
            const int expected_occupied = quotas[l] * (lessons[l].is_block ? 2 : 1);
            if (previous_occupied != expected_occupied) continue;
            for (int lt = 0; lt < local_slots; lt++) {
                model.AddHint(x[l][lt], warm_start->x_vals[l][lt] != 0);
                warm_hint_count++;
            }
        }
    }

    // Фаза 1 не содержит переменных и ограничений, существующих только ради
    // objective. Это заметно ускоряет получение первого допустимого решения.
    const bool run_quality_phase = USE_QUALITY_OBJECTIVE && !STOP_AFTER_FIRST_SOLUTION &&
        g_solver_config.quality_improvement_seconds > 0.0;
    CpModelProto feasibility_proto = model.Build();

    // ── Целевая функция качества ──────────────────────────────────────────
    if (run_quality_phase) {
        for (const auto& v : two_pair_vars)
            objective += v * STUDENT_TWO_PAIR_DAY_WEIGHT;
        for (const auto& v : five_pair_vars)
            objective += v * STUDENT_FIVE_PAIR_DAY_WEIGHT;

        if (!HARD_NO_STUDENT_WINDOWS && OPTIMIZE_STUDENT_WINDOWS) {
            for (const auto& gap : CreateWindowPenaltyVars(model, student_entities, W))
                objective += gap * STUDENT_WINDOW_WEIGHT;
        }
        if (!HARD_NO_TEACHER_WINDOWS && OPTIMIZE_TEACHER_WINDOWS) {
            for (const auto& gap : CreateWindowPenaltyVars(model, teacher_busy, W))
                objective += gap * TEACHER_WINDOW_WEIGHT;
        }
        if (STUDENT_LATE_SLOT_WEIGHT > 0) {
            for (const auto& busy : student_entities)
                for (int ld = 0; ld < W; ld++)
                    for (int s = 0; s < SLOTS_PER_DAY; s++)
                        objective += busy[ld * SLOTS_PER_DAY + s] * (s * STUDENT_LATE_SLOT_WEIGHT);
        }
        if (TEACHER_LATE_SLOT_WEIGHT > 0) {
            for (int teacher = 0; teacher < TEACHERS; teacher++)
                for (int ld = 0; ld < W; ld++)
                    for (int s = 0; s < SLOTS_PER_DAY; s++)
                        objective += teacher_busy[teacher][ld * SLOTS_PER_DAY + s] * (s * TEACHER_LATE_SLOT_WEIGHT);
        }
        // Первая площадка в campus_priority — мягкое предпочтение. Оно не
        // делает модель неразрешимой, если дисциплина или кабинет требуют другой корпус.
        for (const TeacherData& teacher : teachers) {
            if (teacher.id < 0 || teacher.id >= TEACHERS || teacher.campus_priority.empty()) continue;
            const int preferred = teacher.campus_priority.front();
            for (int ld = 0; ld < W; ld++) {
                if (preferred == LESNAYA) {
                    objective += teacher_day_campus[teacher.id][ld] * TEACHER_CAMPUS_PREFERENCE_WEIGHT;
                } else if (preferred == KRIVOUSOVA) {
                    BoolVar mismatch = model.NewBoolVar();
                    LinearExpr relation;
                    relation += mismatch;
                    relation += teacher_day_campus[teacher.id][ld];
                    model.AddEquality(relation, teacher_day_has[teacher.id][ld]);
                    objective += mismatch * TEACHER_CAMPUS_PREFERENCE_WEIGHT;
                }
            }
        }
        model.Minimize(objective);
    }

    // ── Решаем ────────────────────────────────────────────────────────────
    SatParameters params;
    params.set_num_search_workers(SOLVER_WORKERS);
    params.set_max_time_in_seconds(WEEK_TIME_LIMIT_SECONDS);
    params.set_random_seed(g_solver_config.random_seed + week_num * 17);
    params.set_max_memory_in_mb(SOLVER_MAX_MEMORY_MB);
    params.set_linearization_level(g_solver_config.linearization_level);
    params.set_symmetry_level(g_solver_config.symmetry_level);
    params.set_stop_after_first_solution(true);

    operations_research::sat::Model feasibility_model;
    feasibility_model.Add(NewSatParameters(params));
    if (cancel_flag) {
        feasibility_model.GetOrCreate<operations_research::TimeLimit>()
            ->RegisterExternalBooleanAsLimit(cancel_flag);
    }
    CpSolverResponse feasibility_resp = SolveCpModel(feasibility_proto, &feasibility_model);

    CpSolverResponse resp = feasibility_resp;
    CpModelProto solved_proto = feasibility_proto;
    double quality_seconds = 0.0;
    bool quality_phase_limited = false;

    if (run_quality_phase && !(cancel_flag && cancel_flag->load()) &&
        (feasibility_resp.status() == CpSolverStatus::OPTIMAL ||
         feasibility_resp.status() == CpSolverStatus::FEASIBLE)) {
        // Полное допустимое решение первой фазы становится hint для модели
        // качества. Повторные подсказки очищаем, чтобы proto был валидным.
        model.ClearHints();
        for (int l = 0; l < num_lessons; l++) {
            if (quotas[l] <= 0) continue;
            for (int lt = 0; lt < local_slots; lt++) {
                model.AddHint(x[l][lt], SolutionIntegerValue(feasibility_resp, x[l][lt]) != 0);
            }
        }
        for (int g = 0; g < GROUPS; g++) {
            for (int ld = 0; ld < W; ld++) {
                model.AddHint(group_day_campus[g][ld],
                    SolutionIntegerValue(feasibility_resp, group_day_campus[g][ld]));
            }
        }

        solved_proto = model.Build();
        SatParameters quality_params = params;
        quality_params.set_stop_after_first_solution(false);
        quality_params.set_max_time_in_seconds(g_solver_config.quality_improvement_seconds);
        operations_research::sat::Model quality_model;
        quality_model.Add(NewSatParameters(quality_params));
        if (cancel_flag) {
            quality_model.GetOrCreate<operations_research::TimeLimit>()
                ->RegisterExternalBooleanAsLimit(cancel_flag);
        }
        CpSolverResponse quality_resp = SolveCpModel(solved_proto, &quality_model);
        quality_seconds = quality_resp.wall_time();
        if (quality_resp.status() == CpSolverStatus::OPTIMAL ||
            quality_resp.status() == CpSolverStatus::FEASIBLE) {
            resp = quality_resp;
            quality_phase_limited = quality_resp.status() == CpSolverStatus::FEASIBLE;
        }
    }

    const double model_build_seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - model_build_started).count() -
        feasibility_resp.wall_time() - quality_seconds;

    WeekSolveResult result;
    result.status = cancel_flag && cancel_flag->load()
        ? "CANCELLED" : CpSolverStatus_Name(resp.status());
    result.model_variables = solved_proto.variables_size();
    result.model_constraints = solved_proto.constraints_size();
    result.warm_start_hints = warm_hint_count;
    result.model_build_seconds = std::max(0.0, model_build_seconds);
    result.feasibility_seconds = feasibility_resp.wall_time();
    result.quality_seconds = quality_seconds;
    result.solve_wall_seconds = feasibility_resp.wall_time() + quality_seconds;
    result.branches = feasibility_resp.num_branches() +
        (quality_seconds > 0.0 ? resp.num_branches() : 0);
    result.conflicts = feasibility_resp.num_conflicts() +
        (quality_seconds > 0.0 ? resp.num_conflicts() : 0);
    result.adaptive_quality_stop = quality_phase_limited;

    if (result.status != "CANCELLED" &&
        (resp.status() == CpSolverStatus::OPTIMAL ||
         resp.status() == CpSolverStatus::FEASIBLE)) {
        result.success = true;
        result.x_vals.assign(num_lessons, std::vector<int>(local_slots, 0));
        result.group_day_campus.assign(GROUPS, std::vector<int>(W, 0));
        for (int l = 0; l < num_lessons; l++) {
            if (quotas[l] == 0) continue;
            for (int lt = 0; lt < local_slots; lt++)
                result.x_vals[l][lt] = static_cast<int>(SolutionIntegerValue(resp, x[l][lt]));
        }
        for (int g = 0; g < GROUPS; g++) {
            for (int ld = 0; ld < W; ld++) {
                result.group_day_campus[g][ld] =
                    static_cast<int>(SolutionIntegerValue(resp, group_day_campus[g][ld]));
            }
        }
    } else {
        result.success = false;
    }

    return result;
}

}  // anonymous namespace

GenerationResult GenerateScheduleWeekly(const std::string& output_dir) {
    GenerationOptions empty_opts;
    return GenerateScheduleWeekly(output_dir, empty_opts);
}

GenerationResult GenerateScheduleWeekly(
    const std::string& output_dir,
    const GenerationOptions& options
) {
    WeeklyGenCallbacks empty_cbs;
    return GenerateScheduleWeekly(output_dir, options, empty_cbs);
}

GenerationResult GenerateScheduleWeekly(
    const std::string& output_dir,
    const GenerationOptions& options,
    const WeeklyGenCallbacks& callbacks
) {

    // ── Загрузка входных данных ───────────────────────────────────────────
    ScheduleInputData input_data;
    std::string input_error;
    if (!LoadScheduleInputData(input_data, input_error)) {
        return {false, "INPUT_ERROR",
            "Не удалось загрузить data/timetable_data.json: " + input_error, output_dir};
    }

    const Date start_date = input_data.start_date;
    const Date end_date   = input_data.end_date;
    const auto& unavailable = input_data.unavailable;
    const auto& teacher_unavailable = input_data.teacher_unavailable;
    const auto& unavailable_day_texts = input_data.unavailable_day_texts;

    auto all_days = GenerateSchoolDays(start_date, end_date);
    const int num_days   = static_cast<int>(all_days.size());
    const int total_slots = num_days * SLOTS_PER_DAY;

    std::vector<Lesson> lessons = input_data.lessons;
    const int num_lessons = static_cast<int>(lessons.size());

    std::vector<std::string> validation_errors;
    if (!ValidateInputLessonsDetailed(lessons, validation_errors)) {
        std::string msg = "Входные данные содержат ошибки (" +
            std::to_string(validation_errors.size()) + "): ";
        for (size_t i = 0; i < validation_errors.size() && i < 5; i++) {
            if (i > 0) msg += " | ";
            msg += validation_errors[i];
        }
        if (validation_errors.size() > 5) msg += " | …";
        return {false, "INPUT_ERROR", msg, output_dir};
    }

    std::filesystem::create_directories(output_dir);
    std::filesystem::create_directories(std::filesystem::path(output_dir) / "groups");

    // Полностью нерабочие даты добавляются в обычную модель недоступности.
    // Частичные окна (например, только 2–5 пары) применяются непосредственно
    // к переменным занятия внутри недельной CP-SAT модели.
    auto work_unavailable = unavailable;
    auto teacher_unavailable_model = teacher_unavailable;
    for (const GroupData& group : input_data.groups) {
        for (const Date& date : all_days) {
            bool any_slot = false;
            for (int slot = 0; slot < SLOTS_PER_DAY; slot++)
                any_slot = any_slot || WorkScheduleAllows(group.work_schedule, date, slot);
            if (!any_slot) work_unavailable[group.id].push_back({date, date});
        }
    }
    for (const TeacherData& teacher : input_data.teachers) {
        for (const Date& date : all_days) {
            bool any_slot = false;
            for (int slot = 0; slot < SLOTS_PER_DAY; slot++)
                any_slot = any_slot || WorkScheduleAllows(teacher.work_schedule, date, slot);
            if (!any_slot) teacher_unavailable_model[teacher.id].push_back({date, date});
        }
    }

    PrintInputDiagnostics(lessons, all_days, work_unavailable, start_date);

    // ── ПП: детерминированная расстановка до CP-SAT ───────────────────────
    // ПП-уроки не участвуют в модели (quota=0); их дни блокируются для УП через
    // unavailable_model, что также исключает целиком-ПП недели из делителя.
    const PpPlan pp_plan = ComputePpPlan(lessons, all_days, work_unavailable);
    const std::map<int, std::vector<std::pair<Date, Date>>> unavailable_model =
        MergeUnavailable(work_unavailable, pp_plan.pp_block);

    // ── Структура недель ──────────────────────────────────────────────────
    std::vector<int> week_index(num_days);
    for (int d = 0; d < num_days; d++)
        week_index[d] = WeekIndexFromStart(start_date, all_days[d]);

    std::set<int> wk_set;
    for (int wi : week_index) wk_set.insert(wi);
    const std::vector<int> weeks_list(wk_set.begin(), wk_set.end());
    const int num_weeks = static_cast<int>(weeks_list.size());
    std::map<int, int> week_pos;
    for (int i = 0; i < num_weeks; i++) week_pos[weeks_list[i]] = i;

    // week_day_indices[w] = список глобальных индексов дней в неделе w
    std::vector<std::vector<int>> week_day_indices(num_weeks);
    for (int d = 0; d < num_days; d++)
        week_day_indices[week_pos[week_index[d]]].push_back(d);

    // ── group_avail_weeks: недели с доступными днями ──────────────────────
    std::vector<std::vector<int>> group_avail_weeks(GROUPS);
    std::vector<std::map<int, std::vector<int>>> group_week_days(GROUPS);

    for (int g = 0; g < GROUPS; g++) {
        for (int d = 0; d < num_days; d++) {
            if (IsAvailable(all_days[d], g, unavailable_model)) {
                int w = week_pos[week_index[d]];
                group_week_days[g][w].push_back(d);
            }
        }
        for (auto& kv : group_week_days[g]) group_avail_weeks[g].push_back(kv.first);
    }

    // ── Алгоритм Брезенхема ───────────────────────────────────────────────
    auto bresenham_quota = [](int total, int active_weeks) -> std::vector<int> {
        if (active_weeks <= 0) return {};
        std::vector<int> q(active_weeks, 0);
        int acc = 0;
        for (int i = 0; i < active_weeks; i++) {
            acc += total;
            int slots_here = acc / active_weeks;
            acc -= slots_here * active_weeks;
            q[i] = slots_here;
        }
        return q;
    };

    // ── lesson_week_quota[l][w] — квоты по Брезенхему ────────────────────
    // ПП-уроки расставляются детерминированно (см. ComputePpPlan) и в модель не
    // попадают: их недельная квота всегда 0.
    std::vector<std::vector<int>> lesson_week_quota(num_lessons, std::vector<int>(num_weeks, 0));
    std::vector<std::vector<bool>> lesson_week_allowed(
        num_lessons, std::vector<bool>(num_weeks, false));

    for (int l = 0; l < num_lessons; l++) {
        if (lessons[l].is_pp) continue;  // ПП вне модели

        int g = lessons[l].group;
        std::vector<int> avail_weeks;
        for (int w : group_avail_weeks[g]) {
            if (LessonAllowsWeek(lessons[l], w)) {
                avail_weeks.push_back(w);
                lesson_week_allowed[l][w] = true;
            }
        }
        int active = static_cast<int>(avail_weeks.size());
        if (active == 0) continue;
        auto q = bresenham_quota(lessons[l].total_slots, active);
        for (int i = 0; i < active; i++)
            lesson_week_quota[l][avail_weeks[i]] = q[i];
    }

    // ── Корректировка квот под зафиксированные слоты ─────────────────────
    // Если locked-слот попадает в неделю с quota==0, повышаем квоту в этой неделе
    // и понижаем в неделе с максимальной квотой (чтобы sum == total_slots).
    if (!options.locked.empty()) {
        std::map<int, int> lid_to_l;
        for (int l = 0; l < num_lessons; l++) lid_to_l[lessons[l].id] = l;
        std::map<Date, int> date_to_day;
        for (int d = 0; d < num_days; d++) date_to_day[all_days[d]] = d;

        for (const LockedAssignment& a : options.locked) {
            auto lit = lid_to_l.find(a.lesson_id);
            if (lit == lid_to_l.end()) continue;
            int l = lit->second;
            auto dit = date_to_day.find(a.date);
            if (dit == date_to_day.end()) continue;
            int d = dit->second;
            int w = week_pos[week_index[d]];

            if (lesson_week_quota[l][w] == 0) {
                // Найти неделю с максимальной квотой для этого урока
                int max_w = -1, max_q = 0;
                for (int ww = 0; ww < num_weeks; ww++) {
                    if (lesson_week_quota[l][ww] > max_q) {
                        max_q = lesson_week_quota[l][ww];
                        max_w = ww;
                    }
                }
                if (max_w >= 0 && max_q > 0) {
                    lesson_week_quota[l][w]++;
                    lesson_week_quota[l][max_w]--;
                    std::cout << "  [weekly] lock: урок " << lessons[l].name
                              << " неделя " << w << " получила квоту +1 (с недели " << max_w << ")\n";
                }
            }
        }
    }

    const QuotaBalanceResult quota_balance = BalanceWeeklyQuotas(
        lessons, input_data.groups, input_data.teachers,
        all_days, week_day_indices, lesson_week_allowed,
        unavailable_model, teacher_unavailable_model, options.locked,
        lesson_week_quota, callbacks.cancel_flag);
    {
        std::ofstream quota_out(
            std::filesystem::path(output_dir) / "quota_balance.json",
            std::ios::binary | std::ios::trunc);
        if (quota_out) quota_out << ToJson(quota_balance.report, 2);
    }
    if (quota_balance.cancelled) {
        return {false, "CANCELLED", "Генерация отменена во время балансировки квот", output_dir};
    }
    if (!quota_balance.success) {
        return {false, "QUOTA_INFEASIBLE",
            "Не удалось распределить недельные квоты: " + quota_balance.status, output_dir};
    }

    const WeeklyPreflightResult preflight = BuildWeeklyPreflight(
        lessons, input_data.groups, input_data.teachers,
        input_data.teacher_period_targets,
        all_days, week_day_indices, lesson_week_quota,
        unavailable_model, teacher_unavailable_model);
    {
        std::ofstream preflight_out(
            std::filesystem::path(output_dir) / "solver_preflight.json",
            std::ios::binary | std::ios::trunc);
        if (preflight_out) preflight_out << ToJson(preflight.report, 2);
    }
    if (!preflight.ok) {
        return {false, "PREFLIGHT_INFEASIBLE", preflight.message, output_dir};
    }

    std::cout << "\n══ Режим: генерация по неделям ══\n";
    std::cout << "Недель: " << num_weeks << ", Дней: " << num_days << "\n";
    std::cout << "Лимит на неделю: " << WEEK_TIME_LIMIT_SECONDS << " с"
              << " | no_student_windows=" << (HARD_NO_STUDENT_WINDOWS ? "HARD" : "soft")
              << " | min_pairs_day=" << MIN_STUDENT_PAIRS_PER_STUDY_DAY
              << " | quality_obj=" << (USE_QUALITY_OBJECTIVE ? "on" : "off")
              << "\n\n";

    // ── Решаем по неделям ─────────────────────────────────────────────────
    std::vector<std::vector<int>> global_x_vals(num_lessons, std::vector<int>(total_slots, 0));
    std::vector<std::vector<int>> global_group_day_campus(
        GROUPS, std::vector<int>(num_days, 0));

    // ПП вписываем сразу — чтобы практика была и в каждом промежуточном автосохранении.
    for (const PpPlacement& p : pp_plan.placements) {
        global_x_vals[p.lesson_index][p.global_day * SLOTS_PER_DAY + p.slot] = 1;
        const Lesson& lesson = lessons[p.lesson_index];
        if (lesson.allowed_campuses.size() == 1) {
            global_group_day_campus[lesson.group][p.global_day] =
                static_cast<int>(*lesson.allowed_campuses.begin());
        }
    }

    auto solve_start = std::chrono::steady_clock::now();
    WeekSolveResult previous_week_result;
    bool has_previous_week = false;
    JsonValue solver_week_metrics = JsonValue::MakeArray();
    JsonValue runtime_quota_repairs = JsonValue::MakeArray();

    for (int w = 0; w < num_weeks; w++) {
        // ── Проверка отмены ────────────────────────────────────────────────
        if (callbacks.cancel_flag && callbacks.cancel_flag->load()) {
            std::cout << "\nГенерация отменена пользователем на неделе " << (w + 1) << ".\n";
            // Сохраняем частичный результат перед выходом
            WriteScheduleFiles(output_dir, global_x_vals, num_days,
                lessons, all_days, global_group_day_campus,
                input_data.groups, input_data.rooms,
                unavailable, unavailable_day_texts, false);
            return {false, "CANCELLED", "Генерация отменена", output_dir};
        }

        const auto& wdix = week_day_indices[w];
        if (wdix.empty()) continue;

        bool any_lesson = false;
        for (int l = 0; l < num_lessons; l++) {
            if (lesson_week_quota[l][w] > 0) { any_lesson = true; break; }
        }

        std::string date_from = DateToString(all_days[wdix.front()]);
        std::string date_to   = DateToString(all_days[wdix.back()]);

        if (!any_lesson) {
            std::cout << "Неделя " << (w + 1) << "/" << num_weeks
                      << " [" << date_from << " … " << date_to << "] — пропуск (нет занятий)\n";
            if (callbacks.on_week_done)
                callbacks.on_week_done(w, num_weeks, date_from, date_to, "skipped", 0.0);
            continue;
        }

        if (callbacks.on_week_start)
            callbacks.on_week_start(w, num_weeks, date_from, date_to);

        std::cout << "Неделя " << (w + 1) << "/" << num_weeks
                  << " [" << date_from << " … " << date_to
                  << "] " << wdix.size() << " дней\n";

        std::vector<int> quotas(num_lessons);
        for (int l = 0; l < num_lessons; l++) quotas[l] = lesson_week_quota[l][w];

        auto t0 = std::chrono::steady_clock::now();
        WeekSolveResult wr = SolveOneWeek(
            w, wdix, all_days, lessons, input_data.groups, input_data.teachers, input_data.rooms, unavailable_model,
            teacher_unavailable_model, quotas, options.locked,
            input_data.prior_theory_pairs,
            has_previous_week ? &previous_week_result : nullptr,
            callbacks.cancel_flag
        );
        double elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - t0).count();

        if (wr.status == "CANCELLED") {
            std::cout << "  CP-SAT остановлен пользователем за " << std::fixed
                      << std::setprecision(1) << elapsed << " с\n";
            if (callbacks.on_week_done)
                callbacks.on_week_done(w, num_weeks, date_from, date_to, "cancelled", elapsed);
            WriteScheduleFiles(output_dir, global_x_vals, num_days,
                lessons, all_days, global_group_day_campus,
                input_data.groups, input_data.rooms,
                unavailable, unavailable_day_texts, false);
            return {false, "CANCELLED", "Генерация отменена", output_dir};
        }

        // Стандартная генерация никогда не принимает модель с ослабленными
        // hard-ограничениями. UNKNOWN остаётся явной ошибкой/тайм-аутом, а
        // доказанный конфликт квот ниже может ремонтироваться только с повторной
        // проверкой той же точной модели.

        // Агрегатный балансировщик не видит все сочетания подгрупп, УП,
        // преподавателей и кампусов. Если точная модель доказала конфликт,
        // переносим по одной квоте в наименее загруженную будущую неделю и
        // каждый раз перепроверяем точной моделью. Суммарные часы сохраняются.
        if (!wr.success && wr.status.find("INFEASIBLE") != std::string::npos) {
            auto teacher_load = [&](int teacher, int week) {
                if (teacher < 0) return 0;
                int value = 0;
                for (int l = 0; l < num_lessons; l++)
                    if (lessons[l].teacher == teacher)
                        value += lesson_week_quota[l][week] *
                            (lessons[l].is_block ? 2 : 1);
                return value;
            };
            auto group_part_load = [&](int group, int part, int week) {
                int value = 0;
                for (int l = 0; l < num_lessons; l++)
                    if (LessonAffectsPart(lessons[l], group, part))
                        value += lesson_week_quota[l][week] *
                            (lessons[l].is_block ? 2 : 1);
                return value;
            };
            auto has_locked_here = [&](int lesson_index) {
                for (const LockedAssignment& assignment : options.locked) {
                    if (assignment.lesson_id != lessons[lesson_index].id) continue;
                    for (int gd : wdix)
                        if (assignment.date == all_days[gd]) return true;
                }
                return false;
            };

            constexpr int kMaxQuotaRepairAttempts = 24;
            for (int attempt = 0; attempt < kMaxQuotaRepairAttempts && !wr.success; attempt++) {
                int best_lesson = -1;
                int best_target = -1;
                long long best_score = std::numeric_limits<long long>::min();

                for (int l = 0; l < num_lessons; l++) {
                    if (lesson_week_quota[l][w] <= 0 || lessons[l].is_pp ||
                        has_locked_here(l)) continue;

                    int source_pressure = teacher_load(lessons[l].teacher, w) * 2;
                    for (int p = 0; p < PARTS_PER_GROUP; p++) {
                        if (LessonAffectsPart(lessons[l], lessons[l].group, p))
                            source_pressure += group_part_load(lessons[l].group, p, w);
                    }
                    if (lessons[l].is_block) source_pressure += 20;

                    for (int target = w + 1; target < num_weeks; target++) {
                        if (!lesson_week_allowed[l][target]) continue;
                        int target_pressure = teacher_load(lessons[l].teacher, target) * 2;
                        for (int p = 0; p < PARTS_PER_GROUP; p++) {
                            if (LessonAffectsPart(lessons[l], lessons[l].group, p))
                                target_pressure += group_part_load(lessons[l].group, p, target);
                        }
                        const long long score =
                            static_cast<long long>(source_pressure) * 1000LL -
                            static_cast<long long>(target_pressure) * 20LL -
                            (target - w);
                        if (score > best_score) {
                            best_score = score;
                            best_lesson = l;
                            best_target = target;
                        }
                    }
                }

                if (best_lesson < 0 || best_target < 0) break;
                lesson_week_quota[best_lesson][w]--;
                lesson_week_quota[best_lesson][best_target]++;
                quotas[best_lesson]--;

                JsonValue repair = JsonValue::MakeObject();
                repair.At("lesson_id") = JsonValue::MakeNumber(lessons[best_lesson].id);
                repair.At("lesson_name") = JsonValue::MakeString(lessons[best_lesson].name);
                repair.At("from_week") = JsonValue::MakeNumber(w + 1);
                repair.At("to_week") = JsonValue::MakeNumber(best_target + 1);
                repair.At("attempt") = JsonValue::MakeNumber(attempt + 1);
                runtime_quota_repairs.array_value.push_back(repair);
                std::cout << "  [quota-repair] «" << lessons[best_lesson].name
                          << "»: неделя " << (w + 1) << " → " << (best_target + 1) << "\n";

                auto repair_started = std::chrono::steady_clock::now();
                wr = SolveOneWeek(
                    w, wdix, all_days, lessons, input_data.groups, input_data.teachers, input_data.rooms,
                    unavailable_model, teacher_unavailable_model, quotas, options.locked,
                    input_data.prior_theory_pairs,
                    has_previous_week ? &previous_week_result : nullptr,
                    callbacks.cancel_flag
                );
                elapsed += std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - repair_started).count();
            }

            JsonValue repair_report = JsonValue::MakeObject();
            repair_report.At("count") = JsonValue::MakeNumber(runtime_quota_repairs.array_value.size());
            repair_report.At("items") = runtime_quota_repairs;
            std::ofstream repair_out(
                std::filesystem::path(output_dir) / "quota_runtime_repairs.json",
                std::ios::binary | std::ios::trunc);
            if (repair_out) repair_out << ToJson(repair_report, 2);
        }

        if (!wr.success) {
            std::cerr << "  ОШИБКА недели " << (w + 1) << ": " << wr.status
                      << " (" << std::fixed << std::setprecision(1) << elapsed << " с)\n";
            if (callbacks.on_week_done)
                callbacks.on_week_done(w, num_weeks, date_from, date_to, "failed", elapsed);
            WriteScheduleFiles(output_dir, global_x_vals, num_days,
                lessons, all_days, global_group_day_campus,
                input_data.groups, input_data.rooms,
                unavailable, unavailable_day_texts, false);
            return {false, wr.status,
                "Не удалось решить неделю " + std::to_string(w + 1) + ": " + wr.status,
                output_dir};
        }

        std::cout << "  Решено за " << std::fixed << std::setprecision(1) << elapsed
                  << " с [" << wr.status << "]\n";
        std::cout << "  Модель: " << wr.model_variables << " переменных, "
                  << wr.model_constraints << " ограничений; build="
                  << std::setprecision(3) << wr.model_build_seconds << " с, feasible="
                  << wr.feasibility_seconds << " с, quality=" << wr.quality_seconds
                  << " с, total=" << wr.solve_wall_seconds << " с, hints=" << wr.warm_start_hints
                  << ", branches=" << wr.branches << ", conflicts=" << wr.conflicts
                  << (wr.adaptive_quality_stop ? ", quality-stop" : "") << "\n";

        JsonValue metric = JsonValue::MakeObject();
        metric.At("week") = JsonValue::MakeNumber(w + 1);
        metric.At("date_from") = JsonValue::MakeString(date_from);
        metric.At("date_to") = JsonValue::MakeString(date_to);
        metric.At("status") = JsonValue::MakeString(wr.status);
        metric.At("variables") = JsonValue::MakeNumber(wr.model_variables);
        metric.At("constraints") = JsonValue::MakeNumber(wr.model_constraints);
        metric.At("warm_start_hints") = JsonValue::MakeNumber(wr.warm_start_hints);
        metric.At("model_build_seconds") = JsonValue::MakeNumber(wr.model_build_seconds);
        metric.At("feasibility_seconds") = JsonValue::MakeNumber(wr.feasibility_seconds);
        metric.At("quality_seconds") = JsonValue::MakeNumber(wr.quality_seconds);
        metric.At("solve_seconds") = JsonValue::MakeNumber(wr.solve_wall_seconds);
        metric.At("branches") = JsonValue::MakeNumber(static_cast<double>(wr.branches));
        metric.At("conflicts") = JsonValue::MakeNumber(static_cast<double>(wr.conflicts));
        metric.At("adaptive_quality_stop") = JsonValue::MakeBool(wr.adaptive_quality_stop);
        solver_week_metrics.array_value.push_back(metric);
        WriteSolverMetricsReport(
            std::filesystem::path(output_dir), solver_week_metrics, "running",
            std::chrono::duration<double>(std::chrono::steady_clock::now() - solve_start).count());

        // Копируем локальные x_vals в глобальные
        for (int l = 0; l < num_lessons; l++) {
            if (quotas[l] == 0) continue;
            for (int li = 0; li < static_cast<int>(wdix.size()); li++) {
                int gd = wdix[li];
                for (int s = 0; s < SLOTS_PER_DAY; s++) {
                    global_x_vals[l][gd * SLOTS_PER_DAY + s] = wr.x_vals[l][li * SLOTS_PER_DAY + s];
                }
            }
        }
        for (int g = 0; g < GROUPS; g++) {
            for (int li = 0; li < static_cast<int>(wdix.size()); li++) {
                global_group_day_campus[g][wdix[li]] = wr.group_day_campus[g][li];
            }
        }
        previous_week_result = wr;
        has_previous_week = true;

        // ── Автосохранение после каждой недели ────────────────────────────
        WriteScheduleFiles(output_dir, global_x_vals, num_days,
            lessons, all_days, global_group_day_campus,
            input_data.groups, input_data.rooms,
            unavailable, unavailable_day_texts, false);

        if (callbacks.on_week_done)
            callbacks.on_week_done(w, num_weeks, date_from, date_to, "done", elapsed);
    }

    double total_elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - solve_start).count();
    std::cout << "\nВсего недель решено за " << std::fixed << std::setprecision(1)
              << total_elapsed << " с\n";

    // Защита последнего рубежа: даже если квоты/ПП/копирование недель когда-либо
    // разойдутся, неполная нагрузка не должна получить WEEKLY_FEASIBLE.
    const ScheduleLoadSummary final_load =
        ComputeScheduleLoadSummary(lessons, global_x_vals);
    if (!final_load.complete()) {
        WriteSolverMetricsReport(
            std::filesystem::path(output_dir), solver_week_metrics, "partial", total_elapsed);
        WriteScheduleFiles(output_dir, global_x_vals, num_days,
            lessons, all_days, global_group_day_campus,
            input_data.groups, input_data.rooms,
            unavailable, unavailable_day_texts, true);

        std::ostringstream message;
        message << "Нагрузка расписания не совпадает с планом по каждой строке: запланировано "
                << final_load.planned_hours << " ч, поставлено "
                << final_load.scheduled_hours << " ч, недобор "
                << final_load.missing_hours << " ч, лишних "
                << final_load.excess_hours << " ч, строк с расхождением "
                << final_load.mismatched_lessons;
        std::cerr << "  ОШИБКА: " << message.str() << "\n";
        return {false, "PARTIAL_SCHEDULE", message.str(), output_dir};
    }

    WriteSolverMetricsReport(
        std::filesystem::path(output_dir), solver_week_metrics, "done", total_elapsed);

    // Финальная запись со статистикой
    WriteScheduleFiles(output_dir, global_x_vals, num_days,
        lessons, all_days, global_group_day_campus,
        input_data.groups, input_data.rooms,
        unavailable, unavailable_day_texts, true);

    std::cout << "\nФайлы созданы в: " << output_dir << "\n";

    return {true, "WEEKLY_FEASIBLE", "Расписание по неделям найдено", output_dir};
}

int RunScheduler() {
    GenerationResult result = GenerateSchedule("output/latest");
    return result.success ? 0 : 1;
}

}  // namespace timetable
