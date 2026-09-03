#include "http_server.h"

#ifndef _WIN32
#error "This simple API server is implemented for Windows/Winsock."
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <ctime>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "config.h"
#include "auth.h"
#include "data_store.h"
#include "json_utils.h"
#include "runtime_config.h"
#include "schedule_validator.h"
#include "scheduler.h"

namespace timetable {
namespace {

std::mutex g_schedule_mutex;
std::mutex g_data_api_mutex;

// ── Async generation state ────────────────────────────────────────────────────

struct WeekEntry {
    int num = 0;
    std::string date_from;
    std::string date_to;
    std::string status;   // "pending" | "running" | "done" | "failed" | "skipped"
    double elapsed = 0.0;
};

struct GenState {
    std::mutex mu;
    std::atomic<bool> running{false};
    std::atomic<bool> cancel_requested{false};

    // Защищено mu:
    std::string state;          // "idle" | "running" | "done" | "failed" | "cancelled"
    int total_weeks = 0;
    int current_week = 0;       // 1-based, 0 = ещё не начинали
    int solved_weeks = 0;
    std::vector<WeekEntry> weeks;
    std::string result_message;
    double total_elapsed = 0.0;
    bool result_success = false;
    bool validation_checked = false;
    bool validation_ok = false;
    long long validation_remaining_hours = 0;
    int validation_incomplete_lessons = 0;
    int validation_unassigned_rooms = 0;
    int validation_hard_errors = 0;
    int validation_warnings = 0;
    std::string validation_hours_source;

    void reset(int total) {
        std::lock_guard<std::mutex> lock(mu);
        state = "running";
        total_weeks = total;
        current_week = 0;
        solved_weeks = 0;
        weeks.clear();
        for (int i = 0; i < total; i++) {
            weeks.push_back({i + 1, "", "", "pending", 0.0});
        }
        result_message = "";
        total_elapsed = 0.0;
        result_success = false;
        validation_checked = false;
        validation_ok = false;
        validation_remaining_hours = 0;
        validation_incomplete_lessons = 0;
        validation_unassigned_rooms = 0;
        validation_hard_errors = 0;
        validation_warnings = 0;
        validation_hours_source.clear();
    }
};

static GenState g_gen;
static auto g_gen_start = std::chrono::steady_clock::now();

std::string ReadFileUtf8(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return "";
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool FileExists(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec) && std::filesystem::is_regular_file(path, ec);
}

constexpr int kTransferBundleSchemaVersion = 1;

std::string TransferTimestamp(bool filename_safe = false) {
    const auto now = std::chrono::system_clock::now();
    const std::time_t value = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
    gmtime_s(&utc, &value);
    std::ostringstream out;
    out << std::put_time(&utc, filename_safe ? "%Y%m%d-%H%M%S" : "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

std::filesystem::path NewGenerationCandidate(const std::filesystem::path& final_output) {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path root = final_output.parent_path() / "candidates";
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    return root / ("generation-" + TransferTimestamp(true) + "-" + std::to_string(nonce));
}

bool PromoteValidatedCandidate(const std::filesystem::path& candidate,
                               const std::filesystem::path& final_output,
                               std::string& error) {
    std::error_code ec;
    if (!std::filesystem::exists(candidate / "schedule_all.json", ec)) {
        error = "Кандидат не содержит schedule_all.json";
        return false;
    }
    const std::filesystem::path archive_root = final_output.parent_path() / "archive";
    std::filesystem::create_directories(archive_root, ec);
    if (ec) {
        error = "Не удалось создать каталог резервных копий: " + ec.message();
        return false;
    }
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path backup = archive_root /
        (final_output.filename().string() + "-before-" + TransferTimestamp(true) + "-" + std::to_string(nonce));
    const bool had_previous = std::filesystem::exists(final_output, ec);
    if (had_previous) {
        std::filesystem::rename(final_output, backup, ec);
        if (ec) {
            error = "Не удалось сохранить предыдущий проверенный вариант: " + ec.message();
            return false;
        }
    }
    std::filesystem::rename(candidate, final_output, ec);
    if (!ec) return true;

    const std::string promotion_error = ec.message();
    if (had_previous) {
        ec.clear();
        std::filesystem::rename(backup, final_output, ec);
    }
    error = "Не удалось опубликовать проверенный кандидат: " + promotion_error;
    return false;
}

JsonValue ReadOptionalJsonFile(const std::filesystem::path& path) {
    if (!FileExists(path)) return JsonValue::MakeNull();
    JsonParseResult parsed = ParseJson(ReadFileUtf8(path));
    return parsed.ok ? parsed.value : JsonValue::MakeNull();
}

int JsonArraySize(const JsonValue& object, const std::string& key) {
    const JsonValue& value = object.At(key);
    return value.IsArray() ? static_cast<int>(value.array_value.size()) : 0;
}

struct FinalOutputValidation {
    bool checked = false;
    bool ok = false;
    long long remaining_hours = 0;
    long long excess_hours = 0;
    int incomplete_lessons = 0;
    int mismatched_lessons = 0;
    int unassigned_rooms = 0;
    int hard_errors = 0;
    int warnings = 0;
    std::string hours_source;
    std::string message;
    JsonValue report = JsonValue::MakeObject();

    JsonValue ToJsonValue() const {
        JsonValue value = JsonValue::MakeObject();
        value.At("checked") = JsonValue::MakeBool(checked);
        value.At("ok") = JsonValue::MakeBool(ok);
        value.At("remaining_hours") = JsonValue::MakeNumber(static_cast<double>(remaining_hours));
        value.At("excess_hours") = JsonValue::MakeNumber(static_cast<double>(excess_hours));
        value.At("incomplete_lessons") = JsonValue::MakeNumber(incomplete_lessons);
        value.At("mismatched_lessons") = JsonValue::MakeNumber(mismatched_lessons);
        value.At("unassigned_rooms") = JsonValue::MakeNumber(unassigned_rooms);
        value.At("hard_errors") = JsonValue::MakeNumber(hard_errors);
        value.At("warnings") = JsonValue::MakeNumber(warnings);
        value.At("hours_source") = JsonValue::MakeString(hours_source);
        value.At("message") = JsonValue::MakeString(message);
        value.At("report") = report;
        return value;
    }
};

FinalOutputValidation ValidateFinalOutputParts(
    const JsonValue& data,
    const std::filesystem::path& schedule_file,
    const JsonValue& room_report,
    const JsonValue& quality_report) {
    FinalOutputValidation result;
    result.checked = true;

    // Доверяем quality_report только нового формата, где есть отдельные поля
    // недобора/перебора по строкам. Старый агрегатный отчёт перепроверяем по
    // schedule_all.json, чтобы равные общие суммы не скрыли перестановку часов.
    if (quality_report.IsObject() && quality_report.At("remaining_hours").IsNumber() &&
        quality_report.At("excess_hours").IsNumber() &&
        quality_report.At("mismatched_lessons").IsNumber()) {
        const int reported_remaining = std::max(0, JsonInt(quality_report, "remaining_hours", 0));
        const int planned = std::max(0, JsonInt(quality_report, "planned_hours", 0));
        const int scheduled = std::max(0, JsonInt(quality_report, "scheduled_hours", 0));
        result.remaining_hours = std::max(reported_remaining, std::max(0, planned - scheduled));
        result.excess_hours = std::max(
            std::max(0, JsonInt(quality_report, "excess_hours", 0)),
            std::max(0, scheduled - planned));
        result.mismatched_lessons = std::max(
            0, JsonInt(quality_report, "mismatched_lessons", 0));
        if (quality_report.At("load_matches_plan_exactly").IsBool() &&
            !quality_report.At("load_matches_plan_exactly").bool_value) {
            result.mismatched_lessons = std::max(1, result.mismatched_lessons);
        }
        result.hours_source = "quality_report";
    } else {
        JsonParseResult schedule = ParseJson(ReadFileUtf8(schedule_file));
        if (!schedule.ok || !schedule.value.IsObject() || !schedule.value.At("groups").IsArray()) {
            result.message = "Итоговая проверка: schedule_all.json отсутствует или повреждён";
            return result;
        }
        std::map<int, int> scheduled_events;
        for (const JsonValue& group : schedule.value.At("groups").array_value) {
            for (const JsonValue& day : group.At("days").array_value) {
                for (const JsonValue& slot : day.At("slots").array_value) {
                    for (const JsonValue& lesson : slot.At("lessons").array_value) {
                        const int lesson_id = JsonInt(lesson, "id", -1);
                        if (lesson_id >= 0) scheduled_events[lesson_id]++;
                    }
                }
            }
        }
        const JsonValue& lessons = data.At("lessons");
        if (!lessons.IsArray()) {
            result.message = "Итоговая проверка: входные данные не содержат список занятий";
            return result;
        }
        for (const JsonValue& lesson : lessons.array_value) {
            if (!JsonBool(lesson, "plan_active", true) ||
                !JsonBool(lesson, "generation_active", true)) continue;
            const int lesson_id = JsonInt(lesson, "id", -1);
            const int expected = std::max(0, JsonInt(lesson, "total_slots", 0)) *
                (JsonBool(lesson, "is_block", false) ? 4 : 2);
            const int scheduled = scheduled_events[lesson_id] * 2;
            if (scheduled < expected) {
                result.remaining_hours += expected - scheduled;
                result.incomplete_lessons++;
                result.mismatched_lessons++;
            } else if (scheduled > expected) {
                result.excess_hours += scheduled - expected;
                result.mismatched_lessons++;
            }
        }
        result.hours_source = "active_total_slots_fallback";
    }

    if (!room_report.IsObject() || !room_report.At("unassigned").IsNumber()) {
        result.message = "Итоговая проверка: room_allocation.json отсутствует или повреждён";
        return result;
    }
    result.unassigned_rooms = std::max(0, JsonInt(room_report, "unassigned", 0));
    result.ok = result.remaining_hours == 0 && result.excess_hours == 0 &&
        result.mismatched_lessons == 0 && result.unassigned_rooms == 0;
    if (result.ok) {
        result.message = "Итоговая проверка пройдена: часы закрыты, кабинеты назначены";
    } else {
        std::ostringstream message;
        message << "Итоговая проверка не пройдена";
        if (result.remaining_hours > 0) {
            message << ": осталось " << result.remaining_hours << " ч.";
            if (result.incomplete_lessons > 0) {
                message << " по " << result.incomplete_lessons << " занятиям";
            }
        }
        if (result.excess_hours > 0) {
            message << (result.remaining_hours > 0 ? "; " : ": ")
                    << "лишних размещений на " << result.excess_hours << " ч.";
        }
        if (result.mismatched_lessons > 0 && result.incomplete_lessons == 0) {
            message << (result.remaining_hours > 0 || result.excess_hours > 0 ? "; " : ": ")
                    << "строк с расхождением " << result.mismatched_lessons;
        }
        if (result.unassigned_rooms > 0) {
            message << (result.remaining_hours > 0 || result.excess_hours > 0 ||
                            result.mismatched_lessons > 0 ? "; " : ": ")
                    << "без кабинета " << result.unassigned_rooms << " событий";
        }
        result.message = message.str();
    }
    return result;
}

FinalOutputValidation ValidateFinalOutput(const std::filesystem::path& output_dir) {
    FinalOutputValidation result;
    result.checked = true;
    const auto schedule_file = output_dir / "schedule_all.json";
    JsonParseResult schedule = ParseJson(ReadFileUtf8(schedule_file));
    if (!schedule.ok || !schedule.value.IsObject() || !schedule.value.At("groups").IsArray()) {
        result.message = "Итоговая проверка: schedule_all.json отсутствует или повреждён";
        return result;
    }
    EnsureDataFileExists();
    JsonParseResult data = ParseJson(ReadDataJsonText());
    if (!data.ok || !data.value.IsObject()) {
        result.message = "Итоговая проверка: не удалось прочитать текущие входные данные";
        return result;
    }
    JsonParseResult room_report = ParseJson(ReadFileUtf8(output_dir / "room_allocation.json"));
    JsonParseResult quality_report = ParseJson(ReadFileUtf8(output_dir / "quality_report.json"));
    result = ValidateFinalOutputParts(data.value, schedule_file,
        room_report.ok ? room_report.value : JsonValue::MakeNull(),
        quality_report.ok ? quality_report.value : JsonValue::MakeNull());

    ScheduleInputData input;
    std::string input_error;
    if (!LoadScheduleInputData(input, input_error)) {
        result.ok = false;
        result.message = "Полная проверка: не удалось загрузить входные данные: " + input_error;
        return result;
    }
    ScheduleValidationOptions options;
    options.source = "auto";
    const ScheduleValidationResult strict =
        ValidateScheduleJson(input, g_solver_config, schedule.value, options);
    result.hard_errors = strict.hard_error_count;
    result.warnings = strict.warning_count;
    result.report = strict.report;
    result.remaining_hours = std::max(result.remaining_hours, strict.remaining_hours);
    result.excess_hours = std::max(result.excess_hours, strict.excess_hours);
    result.incomplete_lessons = std::max(result.incomplete_lessons, strict.incomplete_lessons);
    result.mismatched_lessons = std::max(result.mismatched_lessons, strict.mismatched_lessons);
    result.unassigned_rooms = std::max(result.unassigned_rooms, strict.unassigned_rooms);
    const bool legacy_ok = result.ok;
    const std::string legacy_message = result.message;
    result.ok = legacy_ok && strict.ok;
    result.message = !strict.ok ? strict.message : (legacy_ok ? strict.message : legacy_message);

    std::ofstream report_file(output_dir / "validation_report.json", std::ios::binary | std::ios::trunc);
    if (report_file) report_file << ToJson(strict.report, 2);
    return result;
}

FinalOutputValidation ValidateImportedOutput(
    const JsonValue& data,
    const JsonValue& schedule,
    const JsonValue& room_report) {
    FinalOutputValidation result;
    result.checked = true;
    if (!data.IsObject() || !schedule.IsObject() || !schedule.At("groups").IsArray()) {
        result.message = "Итоговая проверка: пакет не содержит корректное расписание";
        return result;
    }
    std::error_code ec;
    const auto temporary_root = std::filesystem::temp_directory_path(ec);
    if (ec) {
        result.message = "Итоговая проверка: недоступен временный каталог";
        return result;
    }
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto temporary = temporary_root /
        ("raspis-transfer-validation-" + std::to_string(nonce) + ".json");
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) {
            result.message = "Итоговая проверка: не удалось создать временный снимок";
            return result;
        }
        out << ToJson(schedule, 0);
        if (!out) {
            out.close();
            std::filesystem::remove(temporary, ec);
            result.message = "Итоговая проверка: не удалось записать временный снимок";
            return result;
        }
    }
    result = ValidateFinalOutputParts(data, temporary, room_report, JsonValue::MakeNull());
    std::filesystem::remove(temporary, ec);
    return result;
}

void ApplyFinalOutputGate(GenerationResult& generation, const FinalOutputValidation& validation) {
    if (!generation.success || validation.ok) return;
    generation.success = false;
    generation.status = "OUTPUT_VALIDATION_FAILED";
    generation.message = validation.message;
}

JsonValue BuildTransferBundle(const std::filesystem::path& out_dir, const std::string& source) {
    JsonValue bundle = JsonValue::MakeObject();
    bundle.At("format") = JsonValue::MakeString("raspis-transfer-bundle");
    bundle.At("schema_version") = JsonValue::MakeNumber(kTransferBundleSchemaVersion);
    bundle.At("exported_at") = JsonValue::MakeString(TransferTimestamp());
    bundle.At("source") = JsonValue::MakeString(source);

    EnsureDataFileExists();
    JsonParseResult data = ParseJson(ReadDataJsonText());
    bundle.At("data") = data.ok ? data.value : JsonValue::MakeNull();

    JsonValue schedules = JsonValue::MakeObject();
    schedules.At("auto") = ReadOptionalJsonFile(out_dir / "schedule_all.json");
    schedules.At("manual") = ReadOptionalJsonFile(
        std::filesystem::path("output") / "manual" / "schedule_all.json");
    schedules.At("published") = ReadOptionalJsonFile(
        std::filesystem::path("output") / "published" / "schedule_all.json");
    bundle.At("schedules") = schedules;

    JsonValue reports = JsonValue::MakeObject();
    const std::vector<std::pair<std::string, std::string>> report_files = {
        {"room_allocation", "room_allocation.json"},
        {"quality", "quality_report.json"},
        {"solver_metrics", "solver_metrics.json"},
        {"solver_preflight", "solver_preflight.json"},
        {"quota_balance", "quota_balance.json"},
        {"quota_runtime_repairs", "quota_runtime_repairs.json"},
        {"semester_readout", "semester_readout_report.json"},
    };
    for (const auto& item : report_files) {
        reports.At(item.first) = ReadOptionalJsonFile(out_dir / item.second);
    }
    bundle.At("reports") = reports;

    JsonValue summary = JsonValue::MakeObject();
    if (data.ok && data.value.IsObject()) {
        summary.At("groups") = JsonValue::MakeNumber(JsonArraySize(data.value, "groups"));
        summary.At("teachers") = JsonValue::MakeNumber(JsonArraySize(data.value, "teachers"));
        summary.At("lessons") = JsonValue::MakeNumber(JsonArraySize(data.value, "lessons"));
        summary.At("rooms") = JsonValue::MakeNumber(JsonArraySize(data.value, "rooms"));
        summary.At("substitutions") = JsonValue::MakeNumber(JsonArraySize(data.value, "substitutions"));
        summary.At("accounting_adjustments") =
            JsonValue::MakeNumber(JsonArraySize(data.value, "accounting_adjustments"));
    }
    summary.At("has_auto_schedule") = JsonValue::MakeBool(schedules.At("auto").IsObject());
    summary.At("has_manual_schedule") = JsonValue::MakeBool(schedules.At("manual").IsObject());
    summary.At("has_published_schedule") = JsonValue::MakeBool(schedules.At("published").IsObject());
    summary.At("hours_recalculated_on_import") = JsonValue::MakeBool(true);
    bundle.At("summary") = summary;
    return bundle;
}

bool AtomicWriteJsonFile(const std::filesystem::path& path, const JsonValue& value, std::string& error) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        error = "Не удалось создать каталог " + path.parent_path().string() + ": " + ec.message();
        return false;
    }
    const std::filesystem::path temporary = path.string() + ".tmp-transfer";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) {
            error = "Не удалось записать временный файл " + temporary.string();
            return false;
        }
        out << ToJson(value, 2) << "\n";
        if (!out) {
            error = "Ошибка записи " + temporary.string();
            return false;
        }
    }
    std::filesystem::remove(path, ec);
    ec.clear();
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        error = "Не удалось применить файл " + path.string() + ": " + ec.message();
        std::filesystem::remove(temporary, ec);
        return false;
    }
    return true;
}

void RemoveGroupJsonFiles(const std::filesystem::path& groups_dir) {
    std::error_code ec;
    if (!std::filesystem::exists(groups_dir, ec)) return;
    for (const auto& entry : std::filesystem::directory_iterator(groups_dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        const std::string name = entry.path().filename().string();
        if (name.rfind("group_", 0) == 0 && entry.path().extension() == ".json") {
            std::filesystem::remove(entry.path(), ec);
            ec.clear();
        }
    }
}

bool IsScheduleSnapshot(const JsonValue& value) {
    return value.IsNull() || (value.IsObject() && value.At("groups").IsArray());
}

bool WriteScheduleSnapshot(const std::filesystem::path& directory, const JsonValue& value,
                           std::string& error) {
    const auto schedule_file = directory / "schedule_all.json";
    const auto groups_dir = directory / "groups";
    std::error_code ec;
    if (value.IsNull()) {
        std::filesystem::remove(schedule_file, ec);
        RemoveGroupJsonFiles(groups_dir);
        return true;
    }
    if (!IsScheduleSnapshot(value)) {
        error = "Некорректная структура расписания";
        return false;
    }
    if (!AtomicWriteJsonFile(schedule_file, value, error)) return false;
    std::filesystem::create_directories(groups_dir, ec);
    if (ec) {
        error = "Не удалось создать каталог расписаний групп: " + ec.message();
        return false;
    }
    RemoveGroupJsonFiles(groups_dir);
    for (const JsonValue& group : value.At("groups").array_value) {
        if (!group.IsObject()) continue;
        const int group_index = JsonInt(group, "group_index", -1);
        if (group_index < 0) continue;
        if (!AtomicWriteJsonFile(
                groups_dir / ("group_" + std::to_string(group_index) + ".json"), group, error)) {
            return false;
        }
    }
    return true;
}

bool WriteOptionalReport(const std::filesystem::path& path, const JsonValue& value, std::string& error) {
    if (value.IsNull()) {
        std::error_code ec;
        std::filesystem::remove(path, ec);
        return true;
    }
    if (!value.IsObject() && !value.IsArray()) {
        error = "Некорректный отчёт " + path.filename().string();
        return false;
    }
    return AtomicWriteJsonFile(path, value, error);
}

std::string DefaultPrimarySchedule(const JsonValue& schedules) {
    if (schedules.At("manual").IsObject()) return "manual";
    if (schedules.At("auto").IsObject()) return "auto";
    if (schedules.At("published").IsObject()) return "published";
    return "";
}

using HttpHeaders = std::vector<std::pair<std::string, std::string>>;

std::string MakeHttpResponse(int code, const std::string& status, const std::string& content_type,
                             const std::string& body, const HttpHeaders& headers = {}) {
    std::ostringstream out;
    out << "HTTP/1.1 " << code << " " << status << "\r\n";
    out << "Content-Type: " << content_type << "\r\n";
    out << "Content-Length: " << body.size() << "\r\n";
    out << "Connection: close\r\n";
    out << "Access-Control-Allow-Origin: *\r\n";
    out << "Access-Control-Allow-Methods: GET, POST, PUT, PATCH, DELETE, OPTIONS\r\n";
    out << "Access-Control-Allow-Headers: Content-Type\r\n";
    for (const auto& header : headers) out << header.first << ": " << header.second << "\r\n";
    out << "\r\n";
    out << body;
    return out.str();
}

std::string JsonResponse(int code, const std::string& status, const std::string& body,
                         const HttpHeaders& headers = {}) {
    return MakeHttpResponse(code, status, "application/json; charset=utf-8", body, headers);
}

std::string ErrorJson(int code, const std::string& status, const std::string& message) {
    return JsonResponse(code, status, "{\"success\":false,\"message\":\"" + JsonEscape(message) + "\"}");
}

std::string OkJson(const JsonValue& value) {
    return JsonResponse(200, "OK", ToJson(value, 2));
}

std::string CreatedJson(const JsonValue& value) {
    return JsonResponse(201, "Created", ToJson(value, 2));
}

std::string BodyOfRequest(const std::string& request) {
    size_t pos = request.find("\r\n\r\n");
    if (pos == std::string::npos) return "";
    return request.substr(pos + 4);
}

std::string Lower(std::string value);

std::string HeaderValue(const std::string& request, const std::string& wanted_name) {
    std::istringstream stream(request);
    std::string line;
    std::getline(stream, line);  // request line
    const std::string wanted = Lower(wanted_name);
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) break;
        const size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        if (Lower(line.substr(0, colon)) != wanted) continue;
        size_t begin = colon + 1;
        while (begin < line.size() && std::isspace(static_cast<unsigned char>(line[begin]))) ++begin;
        return line.substr(begin);
    }
    return "";
}

std::string CookieValue(const std::string& request, const std::string& cookie_name) {
    std::string cookies = HeaderValue(request, "cookie");
    size_t begin = 0;
    while (begin < cookies.size()) {
        size_t end = cookies.find(';', begin);
        if (end == std::string::npos) end = cookies.size();
        std::string part = cookies.substr(begin, end - begin);
        size_t first = part.find_first_not_of(" \t");
        if (first != std::string::npos) part.erase(0, first);
        const std::string prefix = cookie_name + "=";
        if (part.rfind(prefix, 0) == 0) return part.substr(prefix.size());
        begin = end + 1;
    }
    return "";
}

std::string SessionCookie(const std::string& token, bool clear = false) {
    std::ostringstream cookie;
    cookie << "raspis_session=" << token
           << "; Path=/; HttpOnly; SameSite=Strict";
    if (clear) cookie << "; Max-Age=0";
    else cookie << "; Max-Age=28800";
    return cookie.str();
}

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string UrlDecode(const std::string& value) {
    std::string out;
    for (size_t i = 0; i < value.size(); i++) {
        if (value[i] == '%' && i + 2 < value.size()) {
            std::string hex = value.substr(i + 1, 2);
            char* end = nullptr;
            long c = std::strtol(hex.c_str(), &end, 16);
            if (end && *end == '\0') {
                out.push_back(static_cast<char>(c));
                i += 2;
                continue;
            }
        }
        if (value[i] == '+') out.push_back(' ');
        else out.push_back(value[i]);
    }
    return out;
}

int ParseIdFromPath(const std::string& path, const std::string& prefix) {
    if (path.rfind(prefix, 0) != 0) return -1;
    std::string id_text = path.substr(prefix.size());
    if (id_text.empty()) return -1;
    try {
        return std::stoi(id_text);
    } catch (...) {
        return -1;
    }
}

JsonParseResult LoadRoot() {
    EnsureDataFileExists();
    return ParseJson(ReadDataJsonText());
}

bool SaveRoot(const JsonValue& root, std::string& error) {
    return SaveDataJson(root, error);
}

JsonValue ResponseEnvelope(bool success, const std::string& message, const JsonValue* data = nullptr) {
    JsonValue root = JsonValue::MakeObject();
    root.At("success") = JsonValue::MakeBool(success);
    root.At("message") = JsonValue::MakeString(message);
    root.At("needs_regenerate") = JsonValue::MakeBool(true);
    if (data) root.At("data") = *data;
    return root;
}

std::string GetArrayEndpoint(const std::string& array_name) {
    JsonParseResult parsed = LoadRoot();
    if (!parsed.ok) return ErrorJson(500, "Internal Server Error", parsed.error);
    return OkJson(parsed.value.At(array_name));
}

std::string PostArrayEndpoint(const std::string& array_name, const std::string& body) {
    JsonParseResult parsed = LoadRoot();
    if (!parsed.ok) return ErrorJson(500, "Internal Server Error", parsed.error);
    JsonParseResult body_json = ParseJson(body);
    if (!body_json.ok || !body_json.value.IsObject()) {
        return ErrorJson(400, "Bad Request", "Нужен JSON-объект в теле запроса");
    }

    JsonValue& array_value = parsed.value.At(array_name);
    if (!array_value.IsArray()) array_value = JsonValue::MakeArray();

    JsonValue item = body_json.value;
    if (!item.Has("id")) {
        item.At("id") = JsonValue::MakeNumber(NextId(array_value));
    }
    array_value.array_value.push_back(item);
    NormalizeDataRoot(parsed.value);
    JsonValue& saved_item = parsed.value.At(array_name).array_value.back();

    std::string error;
    if (!SaveRoot(parsed.value, error)) return ErrorJson(500, "Internal Server Error", error);
    JsonValue envelope = ResponseEnvelope(true, "Сохранено. Для применения вызови POST /api/schedule/regenerate.", &saved_item);
    return CreatedJson(envelope);
}

std::string PutArrayEndpoint(const std::string& array_name, int id, const std::string& body) {
    JsonParseResult parsed = LoadRoot();
    if (!parsed.ok) return ErrorJson(500, "Internal Server Error", parsed.error);
    JsonParseResult body_json = ParseJson(body);
    if (!body_json.ok || !body_json.value.IsObject()) {
        return ErrorJson(400, "Bad Request", "Нужен JSON-объект в теле запроса");
    }

    JsonValue& array_value = parsed.value.At(array_name);
    if (!array_value.IsArray()) return ErrorJson(404, "Not Found", "Массив " + array_name + " не найден");

    JsonValue* existing = FindObjectById(array_value, id);
    if (!existing) return ErrorJson(404, "Not Found", "Запись не найдена");

    const std::string stable_uid = JsonString(*existing, "uid", "");
    JsonValue item = body_json.value;
    item.At("id") = JsonValue::MakeNumber(id);
    if (!stable_uid.empty()) item.At("uid") = JsonValue::MakeString(stable_uid);
    *existing = item;
    NormalizeDataRoot(parsed.value);
    existing = FindObjectById(parsed.value.At(array_name), id);

    std::string error;
    if (!SaveRoot(parsed.value, error)) return ErrorJson(500, "Internal Server Error", error);
    JsonValue envelope = ResponseEnvelope(true, "Обновлено. Для применения вызови POST /api/schedule/regenerate.", existing);
    return OkJson(envelope);
}

std::string PatchArrayEndpoint(const std::string& array_name, int id, const std::string& body) {
    JsonParseResult parsed = LoadRoot();
    if (!parsed.ok) return ErrorJson(500, "Internal Server Error", parsed.error);
    JsonParseResult body_json = ParseJson(body);
    if (!body_json.ok || !body_json.value.IsObject()) {
        return ErrorJson(400, "Bad Request", "Нужен JSON-объект в теле запроса");
    }

    JsonValue& array_value = parsed.value.At(array_name);
    if (!array_value.IsArray()) return ErrorJson(404, "Not Found", "Массив " + array_name + " не найден");

    JsonValue* existing = FindObjectById(array_value, id);
    if (!existing) return ErrorJson(404, "Not Found", "Запись не найдена");

    for (const auto& kv : body_json.value.object_value) {
        if (kv.first == "id") continue;
        existing->At(kv.first) = kv.second;
    }
    existing->At("id") = JsonValue::MakeNumber(id);

    std::string error;
    if (!SaveRoot(parsed.value, error)) return ErrorJson(500, "Internal Server Error", error);
    JsonValue envelope = ResponseEnvelope(true, "Обновлено. Для применения вызови POST /api/schedule/regenerate.", existing);
    return OkJson(envelope);
}

std::string BulkPatchArrayEndpoint(const std::string& array_name, const std::string& body) {
    JsonParseResult parsed = LoadRoot();
    if (!parsed.ok) return ErrorJson(500, "Internal Server Error", parsed.error);
    JsonParseResult body_json = ParseJson(body);
    if (!body_json.ok || !body_json.value.IsObject() || !body_json.value.At("patch").IsObject()) {
        return ErrorJson(400, "Bad Request", "Нужны JSON-объект patch и массив ids либо all=true");
    }
    std::set<int> ids;
    const JsonValue& requested_ids = body_json.value.At("ids");
    if (requested_ids.IsArray()) {
        for (const JsonValue& value : requested_ids.array_value)
            if (value.IsNumber()) ids.insert(static_cast<int>(std::llround(value.number_value)));
    }
    const bool all = JsonBool(body_json.value, "all", false);
    int updated = 0;
    JsonValue& array = parsed.value.At(array_name);
    for (JsonValue& item : array.array_value) {
        const int id = JsonInt(item, "id", -1);
        if (!all && !ids.count(id)) continue;
        for (const auto& pair : body_json.value.At("patch").object_value) {
            if (pair.first == "id" || pair.first == "uid") continue;
            item.At(pair.first) = pair.second;
        }
        updated++;
    }
    if (updated == 0) return ErrorJson(404, "Not Found", "Не выбрано ни одной существующей записи");
    NormalizeDataRoot(parsed.value);
    std::string error;
    if (!SaveDataJson(parsed.value, error, "Массовое изменение " + array_name))
        return ErrorJson(500, "Internal Server Error", error);
    JsonValue data = JsonValue::MakeObject();
    data.At("updated") = JsonValue::MakeNumber(updated);
    return OkJson(ResponseEnvelope(true, "Массовые настройки сохранены. Перегенерируй расписание.", &data));
}

std::string DeleteArrayEndpoint(const std::string& array_name, int id) {
    JsonParseResult parsed = LoadRoot();
    if (!parsed.ok) return ErrorJson(500, "Internal Server Error", parsed.error);

    JsonValue& array_value = parsed.value.At(array_name);
    if (!array_value.IsArray()) return ErrorJson(404, "Not Found", "Массив " + array_name + " не найден");
    if (!RemoveObjectById(array_value, id)) return ErrorJson(404, "Not Found", "Запись не найдена");

    std::string error;
    if (!SaveRoot(parsed.value, error)) return ErrorJson(500, "Internal Server Error", error);
    JsonValue envelope = ResponseEnvelope(true, "Удалено. Для применения вызови POST /api/schedule/regenerate.");
    return OkJson(envelope);
}

std::string GetOneEndpoint(const std::string& array_name, int id) {
    JsonParseResult parsed = LoadRoot();
    if (!parsed.ok) return ErrorJson(500, "Internal Server Error", parsed.error);
    JsonValue& array_value = parsed.value.At(array_name);
    JsonValue* item = FindObjectById(array_value, id);
    if (!item) return ErrorJson(404, "Not Found", "Запись не найдена");
    return OkJson(*item);
}

int GroupIndexFromPathValue(const std::string& raw_value) {
    std::string value = UrlDecode(raw_value);
    try {
        return std::stoi(value);
    } catch (...) {
        // дальше ищем по имени
    }

    JsonParseResult parsed = LoadRoot();
    if (!parsed.ok || !parsed.value.At("groups").IsArray()) return -1;
    std::string lowered = Lower(value);
    for (const JsonValue& group : parsed.value.At("groups").array_value) {
        if (Lower(JsonString(group, "name", "")) == lowered)
            return JsonInt(group, "id", -1);
    }
    return -1;
}

std::string GetSettings() {
    JsonParseResult parsed = LoadRoot();
    if (!parsed.ok) return ErrorJson(500, "Internal Server Error", parsed.error);
    return OkJson(parsed.value.At("settings"));
}

bool ValidateSolverConfigPatch(const JsonValue& patch, std::string& error) {
    std::map<std::string, SolverConfigField> fields;
    for (const SolverConfigField& field : SolverConfigSchema()) fields[field.key] = field;
    for (const auto& item : patch.object_value) {
        if (item.first == "profile") continue;
        const auto known = fields.find(item.first);
        if (known == fields.end()) {
            error = "Неизвестный параметр решателя: " + item.first;
            return false;
        }
        if (known->second.type == "bool" && !item.second.IsBool()) {
            error = "Параметр " + item.first + " должен быть логическим";
            return false;
        }
        if ((known->second.type == "int" || known->second.type == "double") &&
            (!item.second.IsNumber() || !std::isfinite(item.second.number_value))) {
            error = "Параметр " + item.first + " должен быть числом";
            return false;
        }
        if (known->second.type == "int" && item.second.IsNumber() &&
            std::floor(item.second.number_value) != item.second.number_value) {
            error = "Параметр " + item.first + " должен быть целым числом";
            return false;
        }
    }
    auto numeric = [&](const std::string& key, double minimum, double maximum) {
        if (!patch.Has(key)) return true;
        const double value = patch.At(key).number_value;
        if (value >= minimum && value <= maximum) return true;
        std::ostringstream message;
        message << "Параметр " << key << " должен быть от " << minimum << " до " << maximum;
        error = message.str();
        return false;
    };
    return numeric("solver_time_limit_seconds", 1, 86400) &&
        numeric("week_time_limit_seconds", 5, 86400) &&
        numeric("quality_improvement_seconds", 0, 86400) &&
        numeric("solver_workers", 1, 64) &&
        numeric("solver_max_memory_mb", 64, 1048576) &&
        numeric("linearization_level", 0, 2) &&
        numeric("symmetry_level", 0, 3) &&
        numeric("min_student_pairs_per_study_day", 1, SLOTS_PER_DAY) &&
        numeric("max_student_pairs_per_day", 1, SLOTS_PER_DAY) &&
        numeric("max_whole_group_same_subject_pairs_per_day", 1, SLOTS_PER_DAY) &&
        numeric("max_same_subject_pairs_per_day", 1, SLOTS_PER_DAY) &&
        numeric("min_student_study_days_per_week", 0, 6) &&
        numeric("min_initial_theory_slots_before_labs", 0, 1000);
}

std::string GetSolverConfig() {
    JsonParseResult parsed = LoadRoot();
    if (!parsed.ok) return ErrorJson(500, "Internal Server Error", parsed.error);

    JsonValue& settings = parsed.value.At("settings");
    JsonValue& solver_config = settings.At("solver_config");

    if (!solver_config.IsObject()) {
        solver_config = SolverConfigToJson(DefaultSolverConfig());
    } else {
        JsonValue defaults = SolverConfigToJson(DefaultSolverConfig());
        for (const auto& kv : defaults.object_value) {
            if (!solver_config.Has(kv.first)) {
                solver_config.At(kv.first) = kv.second;
            }
        }
    }

    JsonValue schema_arr = JsonValue::MakeArray();
    for (const SolverConfigField& field : SolverConfigSchema()) {
        JsonValue entry = JsonValue::MakeObject();
        entry.At("key") = JsonValue::MakeString(field.key);
        entry.At("label") = JsonValue::MakeString(field.label);
        entry.At("description") = JsonValue::MakeString(field.description);
        entry.At("category") = JsonValue::MakeString(field.category);
        entry.At("type") = JsonValue::MakeString(field.type);
        schema_arr.array_value.push_back(entry);
    }

    const RuntimeSolverConfig parsed_config = ParseSolverConfig(solver_config);
    JsonValue effective = SolverConfigToJson(parsed_config);
    if (solver_config.At("profile").IsString()) effective.At("profile") = solver_config.At("profile");
    JsonValue result = JsonValue::MakeObject();
    result.At("values") = effective;
    result.At("effective_values") = effective;
    result.At("schema") = schema_arr;
    result.At("defaults") = SolverConfigToJson(DefaultSolverConfig());
    return OkJson(result);
}

std::string UpdateSolverConfig(const std::string& body, bool reset_to_defaults) {
    JsonParseResult parsed = LoadRoot();
    if (!parsed.ok) return ErrorJson(500, "Internal Server Error", parsed.error);

    JsonValue& settings = parsed.value.At("settings");
    if (!settings.IsObject()) settings = JsonValue::MakeObject();

    RuntimeSolverConfig effective_config = DefaultSolverConfig();
    if (reset_to_defaults) {
        settings.At("solver_config") = SolverConfigToJson(DefaultSolverConfig());
    } else {
        JsonParseResult body_json = ParseJson(body);
        if (!body_json.ok || !body_json.value.IsObject()) {
            return ErrorJson(400, "Bad Request", "Нужен JSON-объект в теле запроса");
        }
        std::string validation_error;
        if (!ValidateSolverConfigPatch(body_json.value, validation_error)) {
            return ErrorJson(400, "Bad Request", validation_error);
        }

        JsonValue& solver_config = settings.At("solver_config");
        if (!solver_config.IsObject()) solver_config = SolverConfigToJson(DefaultSolverConfig());

        for (const auto& kv : body_json.value.object_value) {
            solver_config.At(kv.first) = kv.second;
        }

        effective_config = ParseSolverConfig(solver_config);
        const JsonValue profile = solver_config.At("profile");
        solver_config = SolverConfigToJson(effective_config);
        if (profile.IsString()) solver_config.At("profile") = profile;
        if (effective_config.min_student_pairs_per_study_day > effective_config.max_student_pairs_per_day) {
            return ErrorJson(400, "Bad Request",
                "Минимум пар у студента не может быть больше суточного максимума");
        }
        if (effective_config.max_whole_group_same_subject_pairs_per_day >
            effective_config.max_same_subject_pairs_per_day) {
            return ErrorJson(400, "Bad Request",
                "Предел общегрупповых повторов не может быть больше предела физической подгруппы");
        }
    }

    std::string error;
    if (!SaveRoot(parsed.value, error)) return ErrorJson(500, "Internal Server Error", error);
    ApplySolverConfig(effective_config);

    JsonValue envelope = ResponseEnvelope(
        true,
        reset_to_defaults
            ? "Параметры солвера сброшены к дефолтам. Запусти регенерацию чтобы применить."
            : "Параметры солвера сохранены. Запусти регенерацию чтобы применить.",
        &settings.At("solver_config")
    );
    return OkJson(envelope);
}

std::string GetSolverProfiles() {
    JsonValue profiles = JsonValue::MakeArray();
    auto add = [&](const std::string& id, const std::string& name,
                   const std::string& description, double quality_seconds) {
        JsonValue profile = JsonValue::MakeObject();
        profile.At("id") = JsonValue::MakeString(id);
        profile.At("name") = JsonValue::MakeString(name);
        profile.At("description") = JsonValue::MakeString(description);
        profile.At("quality_improvement_seconds") = JsonValue::MakeNumber(quality_seconds);
        profiles.array_value.push_back(profile);
    };
    add("fast", "Черновик", "Первое допустимое решение без дополнительной оптимизации", 0.0);
    add("balanced", "Баланс", "Полная проверка и мягкая оптимизация окон/площадок", 15.0);
    add("final", "Финальная вычитка", "Без окон у студентов, усиленная оптимизация преподавателей и площадок", 120.0);
    return OkJson(profiles);
}

std::string ApplySolverProfile(const std::string& profile_id) {
    if (profile_id != "fast" && profile_id != "balanced" && profile_id != "final") {
        return ErrorJson(404, "Not Found", "Неизвестный профиль решателя: " + profile_id);
    }

    JsonParseResult parsed = LoadRoot();
    if (!parsed.ok) return ErrorJson(500, "Internal Server Error", parsed.error);
    JsonValue& settings = parsed.value.At("settings");
    if (!settings.IsObject()) settings = JsonValue::MakeObject();
    JsonValue& config = settings.At("solver_config");
    if (!config.IsObject()) config = SolverConfigToJson(DefaultSolverConfig());

    config.At("profile") = JsonValue::MakeString(profile_id);
    config.At("solver_workers") = JsonValue::MakeNumber(4);
    config.At("linearization_level") = JsonValue::MakeNumber(0);
    config.At("symmetry_level") = JsonValue::MakeNumber(2);
    config.At("hard_no_student_windows") = JsonValue::MakeBool(profile_id == "final");
    config.At("hard_no_teacher_windows") = JsonValue::MakeBool(false);
    config.At("hard_max_two_same_subject_per_day") = JsonValue::MakeBool(true);
    config.At("max_whole_group_same_subject_pairs_per_day") = JsonValue::MakeNumber(2);
    config.At("max_same_subject_pairs_per_day") = JsonValue::MakeNumber(3);
    config.At("stop_after_first_solution") = JsonValue::MakeBool(profile_id == "fast");

    if (profile_id == "fast") {
        config.At("use_quality_objective") = JsonValue::MakeBool(false);
        config.At("optimize_student_windows") = JsonValue::MakeBool(false);
        config.At("optimize_teacher_windows") = JsonValue::MakeBool(false);
        config.At("quality_improvement_seconds") = JsonValue::MakeNumber(0);
        config.At("week_time_limit_seconds") = JsonValue::MakeNumber(30);
    } else if (profile_id == "balanced") {
        config.At("use_quality_objective") = JsonValue::MakeBool(true);
        config.At("optimize_student_windows") = JsonValue::MakeBool(true);
        config.At("optimize_teacher_windows") = JsonValue::MakeBool(true);
        config.At("quality_improvement_seconds") = JsonValue::MakeNumber(15);
        config.At("week_time_limit_seconds") = JsonValue::MakeNumber(90);
        config.At("student_window_weight") = JsonValue::MakeNumber(600);
        config.At("teacher_window_weight") = JsonValue::MakeNumber(4000);
        config.At("teacher_campus_preference_weight") = JsonValue::MakeNumber(500);
    } else {
        config.At("use_quality_objective") = JsonValue::MakeBool(true);
        config.At("optimize_student_windows") = JsonValue::MakeBool(false);
        config.At("optimize_teacher_windows") = JsonValue::MakeBool(true);
        config.At("quality_improvement_seconds") = JsonValue::MakeNumber(120);
        config.At("week_time_limit_seconds") = JsonValue::MakeNumber(180);
        config.At("teacher_window_weight") = JsonValue::MakeNumber(12000);
        config.At("teacher_campus_preference_weight") = JsonValue::MakeNumber(1500);
        config.At("student_two_pair_day_weight") = JsonValue::MakeNumber(4000);
        config.At("student_late_slot_weight") = JsonValue::MakeNumber(20);
    }

    const RuntimeSolverConfig effective_config = ParseSolverConfig(config);
    std::string error;
    if (!SaveDataJson(parsed.value, error, "Выбран профиль решателя " + profile_id))
        return ErrorJson(500, "Internal Server Error", error);
    ApplySolverConfig(effective_config);
    JsonValue envelope = ResponseEnvelope(
        true, "Профиль решателя применён. Запусти регенерацию.", &config);
    return OkJson(envelope);
}

std::string UpdateSettings(const std::string& body, bool patch) {
    JsonParseResult parsed = LoadRoot();
    if (!parsed.ok) return ErrorJson(500, "Internal Server Error", parsed.error);
    JsonParseResult body_json = ParseJson(body);
    if (!body_json.ok || !body_json.value.IsObject()) {
        return ErrorJson(400, "Bad Request", "Нужен JSON-объект в теле запроса");
    }

    JsonValue& settings = parsed.value.At("settings");
    if (!settings.IsObject() || !patch) settings = JsonValue::MakeObject();
    for (const auto& kv : body_json.value.object_value) {
        settings.At(kv.first) = kv.second;
    }

    std::string error;
    if (!SaveRoot(parsed.value, error)) return ErrorJson(500, "Internal Server Error", error);
    JsonValue envelope = ResponseEnvelope(true, "Настройки сохранены. Для применения вызови POST /api/schedule/regenerate.", &settings);
    return OkJson(envelope);
}

std::string HandleCrud(const std::string& method, const std::string& path, const std::string& body,
                       const std::string& api_prefix, const std::string& array_name) {
    if (path == api_prefix) {
        if (method == "GET") return GetArrayEndpoint(array_name);
        if (method == "POST") return PostArrayEndpoint(array_name, body);
    }

    const std::string one_prefix = api_prefix + "/";
    if (path.rfind(one_prefix, 0) == 0) {
        int id = ParseIdFromPath(path, one_prefix);
        if (id < 0) return ErrorJson(400, "Bad Request", "Некорректный id");
        if (method == "GET") return GetOneEndpoint(array_name, id);
        if (method == "PUT") return PutArrayEndpoint(array_name, id, body);
        if (method == "PATCH") return PatchArrayEndpoint(array_name, id, body);
        if (method == "DELETE") return DeleteArrayEndpoint(array_name, id);
    }

    return "";
}

std::string HandleRequest(const std::string& request, const std::string& output_dir) {
    std::istringstream request_stream(request);
    std::string method;
    std::string path;
    std::string version;
    request_stream >> method >> path >> version;
    std::string body = BodyOfRequest(request);

    size_t query_pos = path.find('?');
    if (query_pos != std::string::npos) path = path.substr(0, query_pos);

    // CRUD-запросы выполняют read-modify-write. Сериализуем весь цикл, чтобы
    // два одновременных сохранения не затёрли изменения друг друга.
    std::unique_lock<std::mutex> data_write_lock(g_data_api_mutex, std::defer_lock);
    if (method == "POST" || method == "PUT" || method == "PATCH" || method == "DELETE") {
        data_write_lock.lock();
    }

    const std::filesystem::path out_dir(output_dir);

    if (method == "OPTIONS") return JsonResponse(200, "OK", "{}");

    const std::string session_token = CookieValue(request, "raspis_session");

    if (method == "POST" && path == "/api/auth/login") {
        JsonParseResult parsed = ParseJson(body);
        if (!parsed.ok || !parsed.value.IsObject())
            return ErrorJson(400, "Bad Request", "Укажите логин и пароль");
        AuthResult result = CreateAuthSession(
            JsonString(parsed.value, "username", ""), JsonString(parsed.value, "password", ""));
        if (!result.ok) return ErrorJson(401, "Unauthorized", result.error);
        return JsonResponse(200, "OK",
            "{\"success\":true,\"username\":\"" + JsonEscape(AuthUsername()) + "\"}",
            {{"Set-Cookie", SessionCookie(result.value)}, {"Cache-Control", "no-store"}});
    }

    if (method == "GET" && path == "/api/auth/status") {
        std::string username;
        const bool authenticated = ValidateAuthSession(session_token, username);
        return JsonResponse(200, "OK",
            authenticated
                ? "{\"authenticated\":true,\"username\":\"" + JsonEscape(username) + "\"}"
                : "{\"authenticated\":false}",
            {{"Cache-Control", "no-store"}});
    }

    if (method == "POST" && path == "/api/auth/logout") {
        DestroyAuthSession(session_token);
        return JsonResponse(200, "OK", "{\"success\":true}",
            {{"Set-Cookie", SessionCookie("", true)}, {"Cache-Control", "no-store"}});
    }

    // Опубликованное расписание остаётся публичным для студенческого режима.
    if (method == "GET" && path == "/api/schedule/published") {
        const auto file = std::filesystem::path("output") / "published" / "schedule_all.json";
        if (!FileExists(file)) return ErrorJson(404, "Not Found", "Студенческая версия ещё не опубликована");
        return JsonResponse(200, "OK", ReadFileUtf8(file));
    }
    const std::string public_group_prefix = "/api/schedule/published/group/";
    if (method == "GET" && path.rfind(public_group_prefix, 0) == 0) {
        const int group = GroupIndexFromPathValue(path.substr(public_group_prefix.size()));
        if (group < 0) return ErrorJson(404, "Not Found", "Группа не найдена");
        const auto file = std::filesystem::path("output") / "published" / "groups" /
            ("group_" + std::to_string(group) + ".json");
        if (!FileExists(file)) return ErrorJson(404, "Not Found", "Группа не опубликована");
        return JsonResponse(200, "OK", ReadFileUtf8(file));
    }

    std::string authenticated_username;
    if (!ValidateAuthSession(session_token, authenticated_username)) {
        return ErrorJson(401, "Unauthorized", "Требуется вход диспетчера");
    }

    if (method == "PUT" && path == "/api/auth/credentials") {
        JsonParseResult parsed = ParseJson(body);
        if (!parsed.ok || !parsed.value.IsObject())
            return ErrorJson(400, "Bad Request", "Укажите текущий пароль и новые данные");
        AuthResult result = ChangeAuthCredentials(
            session_token,
            JsonString(parsed.value, "current_password", ""),
            JsonString(parsed.value, "new_username", ""),
            JsonString(parsed.value, "new_password", ""));
        if (!result.ok) return ErrorJson(400, "Bad Request", result.error);
        return JsonResponse(200, "OK",
            "{\"success\":true,\"username\":\"" +
                JsonEscape(JsonString(parsed.value, "new_username", "")) + "\"}",
            {{"Set-Cookie", SessionCookie(result.value)}, {"Cache-Control", "no-store"}});
    }

    // Генератор читает единый согласованный снимок базы и параметров. Любая
    // параллельная запись могла раньше изменить глобальную конфигурацию или
    // привести к финальной проверке уже против другой версии данных.
    const bool mutating_request = method == "POST" || method == "PUT" ||
        method == "PATCH" || method == "DELETE";
    if (g_gen.running.load() && mutating_request && path != "/api/schedule/cancel") {
        return ErrorJson(409, "Conflict",
            "Во время генерации данные и настройки заблокированы. Дождитесь завершения или отмените генерацию.");
    }

    if (method == "GET" && (path == "/" || path == "/api")) {
        return JsonResponse(200, "OK",
            "{"
            "\"name\":\"timetable api\","
            "\"note\":\"После изменения данных вызови POST /api/schedule/regenerate\","
            "\"endpoints\":["
            "\"GET /api/data\","
            "\"PUT /api/data\","
            "\"GET /api/transfer/export\","
            "\"POST /api/transfer/import\","
            "\"GET/PUT/PATCH /api/settings\","
            "\"GET/PUT/PATCH /api/settings/solver-config\","
            "\"POST /api/settings/solver-config/reset\","
            "\"GET /api/settings/solver-profiles\","
            "\"POST /api/settings/solver-profile/{fast|balanced|final}\","
            "\"GET/POST /api/groups\","
            "\"GET/PUT/PATCH/DELETE /api/groups/{id}\","
            "\"GET/POST /api/teachers\","
            "\"GET/PUT/PATCH/DELETE /api/teachers/{id}\","
            "\"GET/POST /api/lessons\","
            "\"GET/PUT/PATCH/DELETE /api/lessons/{id}\","
            "\"GET/POST /api/unavailable\","
            "\"GET/PUT/PATCH/DELETE /api/unavailable/{id}\","
            "\"GET/POST /api/teacher-unavailable\","
            "\"GET/PUT/PATCH/DELETE /api/teacher-unavailable/{id}\","
            "\"GET/POST /api/rooms\"," 
            "\"GET/PUT/PATCH/DELETE /api/rooms/{id}\"," 
            "\"GET/POST /api/room-types\"," 
            "\"GET/PUT/PATCH/DELETE /api/room-types/{id}\"," 
            "\"PATCH /api/groups/bulk\","
            "\"PATCH /api/teachers/bulk\","
            "\"GET/POST /api/substitutions\","
            "\"GET/PUT/PATCH/DELETE /api/substitutions/{id}\","
            "\"GET /api/audit\","
            "\"GET /api/hours\","
            "\"GET /api/accounting/teacher-occupancy\","
            "\"GET /api/accounting/substitutions.csv\","
            "\"GET /api/schedule/quality\","
            "\"GET /api/schedule/rooms\","
            "\"GET /api/schedule/solver-metrics\","
            "\"GET /api/schedule/preflight\","
            "\"GET /api/schedule/quota-balance\","
            "\"GET /api/versions\","
            "\"POST /api/schedule/regenerate\","
            "\"POST /api/schedule/validate\","
            "\"GET /api/schedule\","
            "\"GET /api/schedule/group/{id-or-name}\""
            "]}"
        );
    }

    if (method == "GET" && path == "/api/data") {
        return JsonResponse(200, "OK", ReadDataJsonText());
    }

    if (method == "PUT" && path == "/api/data") {
        JsonParseResult parsed = ParseJson(body);
        if (!parsed.ok || !parsed.value.IsObject()) return ErrorJson(400, "Bad Request", parsed.error.empty() ? "Нужен JSON-объект" : parsed.error);
        std::string error;
        if (!SaveDataJson(parsed.value, error, "Импорт или полная замена данных")) return ErrorJson(500, "Internal Server Error", error);
        JsonValue envelope = ResponseEnvelope(true, "Файл данных заменён. Для применения вызови POST /api/schedule/regenerate.");
        return OkJson(envelope);
    }

    if (method == "GET" && path == "/api/transfer/export") {
        JsonValue bundle = BuildTransferBundle(out_dir, "local-desktop");
        if (!bundle.At("data").IsObject()) {
            return ErrorJson(500, "Internal Server Error", "Не удалось прочитать базу для экспорта");
        }
        const std::string filename = "raspis-full-" + TransferTimestamp(true) + ".raspis.json";
        return JsonResponse(200, "OK", ToJson(bundle, 2), {
            {"Content-Disposition", "attachment; filename=\"" + filename + "\""},
            {"Cache-Control", "no-store"},
        });
    }

    if (method == "POST" && path == "/api/transfer/import") {
        if (g_gen.running.load()) {
            return ErrorJson(409, "Conflict", "Нельзя загружать пакет во время генерации расписания");
        }
        JsonParseResult parsed = ParseJson(body);
        if (!parsed.ok || !parsed.value.IsObject()) {
            return ErrorJson(400, "Bad Request", parsed.error.empty() ? "Нужен JSON-объект" : parsed.error);
        }
        const JsonValue& request_root = parsed.value;
        const JsonValue& bundle = request_root.At("bundle").IsObject()
            ? request_root.At("bundle") : request_root;
        if (JsonString(bundle, "format", "") != "raspis-transfer-bundle" ||
            JsonInt(bundle, "schema_version", 0) != kTransferBundleSchemaVersion) {
            return ErrorJson(400, "Bad Request", "Это не поддерживаемый пакет переноса расписания");
        }
        const JsonValue& imported_data = bundle.At("data");
        const JsonValue& schedules = bundle.At("schedules");
        if (!imported_data.IsObject() || !imported_data.At("groups").IsArray() ||
            !imported_data.At("teachers").IsArray() || !imported_data.At("lessons").IsArray()) {
            return ErrorJson(400, "Bad Request", "В пакете отсутствует полная база групп, преподавателей и занятий");
        }
        if (!schedules.IsObject() || !IsScheduleSnapshot(schedules.At("auto")) ||
            !IsScheduleSnapshot(schedules.At("manual")) ||
            !IsScheduleSnapshot(schedules.At("published"))) {
            return ErrorJson(400, "Bad Request", "В пакете повреждён раздел расписаний");
        }
        JsonValue audit = BuildDataAudit(imported_data);
        if (!JsonBool(audit, "ok", false)) {
            audit.At("success") = JsonValue::MakeBool(false);
            audit.At("message") = JsonValue::MakeString(
                "Пакет не применён: аудит импортируемой базы обнаружил ошибки");
            return JsonResponse(422, "Unprocessable Entity", ToJson(audit, 2));
        }

        std::string primary_schedule = JsonString(
            request_root, "primary_schedule", DefaultPrimarySchedule(schedules));
        if (primary_schedule != "auto" && primary_schedule != "manual" &&
            primary_schedule != "published") {
            return ErrorJson(400, "Bad Request", "Неизвестный вариант основного расписания");
        }
        const JsonValue& selected_schedule = schedules.At(primary_schedule);
        if (!selected_schedule.IsObject()) {
            return ErrorJson(400, "Bad Request", "Выбранный вариант расписания отсутствует в пакете");
        }
        const bool publish = JsonBool(request_root, "publish", true);
        FinalOutputValidation import_validation;
        if (publish) {
            import_validation = ValidateImportedOutput(
                imported_data, selected_schedule, bundle.At("reports").At("room_allocation"));
            if (!import_validation.ok) {
                JsonValue details = import_validation.ToJsonValue();
                JsonValue envelope = ResponseEnvelope(false, import_validation.message, &details);
                return JsonResponse(422, "Unprocessable Entity", ToJson(envelope, 2));
            }
        }

        std::lock_guard<std::mutex> schedule_lock(g_schedule_mutex);
        std::string error;
        const std::filesystem::path backups_dir =
            std::filesystem::path("output") / "transfer_backups";
        const std::filesystem::path backup_file = backups_dir /
            ("transfer_backup_" + TransferTimestamp(true) + ".raspis.json");
        if (!AtomicWriteJsonFile(backup_file, BuildTransferBundle(out_dir, "pre-import-backup"), error)) {
            return ErrorJson(500, "Internal Server Error", "Не удалось создать резервную копию: " + error);
        }

        if (!SaveDataJson(imported_data, error, "Импорт полного пакета с desktop-приложения")) {
            return ErrorJson(500, "Internal Server Error", error);
        }
        if (!WriteScheduleSnapshot(out_dir, selected_schedule, error) ||
            !WriteScheduleSnapshot(std::filesystem::path("output") / "manual",
                                   schedules.At("manual"), error) ||
            !WriteScheduleSnapshot(std::filesystem::path("output") / "published",
                                   publish ? selected_schedule : schedules.At("published"), error)) {
            return ErrorJson(500, "Internal Server Error",
                "База сохранена, но не удалось применить расписание: " + error +
                ". Полная предыдущая версия сохранена в " + backup_file.string());
        }

        const JsonValue& reports = bundle.At("reports");
        const std::vector<std::pair<std::string, std::string>> report_files = {
            {"room_allocation", "room_allocation.json"},
            {"quality", "quality_report.json"},
            {"solver_metrics", "solver_metrics.json"},
            {"solver_preflight", "solver_preflight.json"},
            {"quota_balance", "quota_balance.json"},
            {"quota_runtime_repairs", "quota_runtime_repairs.json"},
            {"semester_readout", "semester_readout_report.json"},
        };
        if (reports.IsObject()) {
            for (const auto& item : report_files) {
                if (!WriteOptionalReport(out_dir / item.second, reports.At(item.first), error)) {
                    return ErrorJson(500, "Internal Server Error",
                        "Основные данные применены, но не удалось сохранить отчёт: " + error);
                }
            }
        }

        JsonValue result = JsonValue::MakeObject();
        result.At("success") = JsonValue::MakeBool(true);
        result.At("message") = JsonValue::MakeString(
            "Полный пакет применён. Учёт часов пересчитан по перенесённому расписанию и заменам.");
        result.At("primary_schedule") = JsonValue::MakeString(primary_schedule);
        result.At("published") = JsonValue::MakeBool(publish);
        result.At("validation") = import_validation.ToJsonValue();
        result.At("backup_file") = JsonValue::MakeString(backup_file.string());
        result.At("summary") = bundle.At("summary");
        return OkJson(result);
    }

    if ((method == "GET" || method == "POST") && path == "/api/audit") {
        JsonParseResult parsed = method == "POST" ? ParseJson(body) : LoadRoot();
        if (!parsed.ok) return ErrorJson(500, "Internal Server Error", parsed.error);
        return OkJson(BuildDataAudit(parsed.value));
    }

    if (method == "GET" && path == "/api/hours") {
        JsonParseResult parsed = LoadRoot();
        if (!parsed.ok) return ErrorJson(500, "Internal Server Error", parsed.error);
        return OkJson(BuildHoursReport(parsed.value, (out_dir / "schedule_all.json").string()));
    }

    if (method == "GET" && path == "/api/accounting/teacher-occupancy") {
        JsonParseResult parsed = LoadRoot();
        if (!parsed.ok) return ErrorJson(500, "Internal Server Error", parsed.error);
        return OkJson(BuildTeacherOccupancyReport(
            parsed.value, (out_dir / "schedule_all.json").string()));
    }

    if (method == "GET" && path == "/api/accounting/substitutions.csv") {
        JsonParseResult parsed = LoadRoot();
        if (!parsed.ok) return ErrorJson(500, "Internal Server Error", parsed.error);
        return MakeHttpResponse(200, "OK", "text/csv; charset=utf-8", BuildSubstitutionsCsv(parsed.value));
    }

    if (method == "GET" && path == "/api/schedule/quality") {
        const auto report = out_dir / "quality_report.json";
        if (!FileExists(report)) return ErrorJson(404, "Not Found", "Отчёт качества ещё не создан");
        return JsonResponse(200, "OK", ReadFileUtf8(report));
    }

    if (method == "GET" && path == "/api/schedule/rooms") {
        const auto report = out_dir / "room_allocation.json";
        if (!FileExists(report)) return ErrorJson(404, "Not Found", "Распределение аудиторий ещё не создано");
        return JsonResponse(200, "OK", ReadFileUtf8(report));
    }

    if (method == "GET" && path == "/api/schedule/solver-metrics") {
        const auto report = out_dir / "solver_metrics.json";
        if (!FileExists(report)) return ErrorJson(404, "Not Found", "Метрики решателя ещё не созданы");
        return JsonResponse(200, "OK", ReadFileUtf8(report));
    }

    if (method == "GET" && path == "/api/schedule/preflight") {
        const auto report = out_dir / "solver_preflight.json";
        if (!FileExists(report)) return ErrorJson(404, "Not Found", "Недельная проверка ещё не запускалась");
        return JsonResponse(200, "OK", ReadFileUtf8(report));
    }

    if (method == "GET" && path == "/api/schedule/quota-balance") {
        const auto report = out_dir / "quota_balance.json";
        if (!FileExists(report)) return ErrorJson(404, "Not Found", "Балансировка квот ещё не запускалась");
        return JsonResponse(200, "OK", ReadFileUtf8(report));
    }

    if (method == "GET" && path == "/api/versions") return OkJson(ListDataVersions());

    const std::string versions_prefix = "/api/versions/";
    const std::string restore_suffix = "/restore";
    if (method == "POST" && path.rfind(versions_prefix, 0) == 0 &&
        path.size() > versions_prefix.size() + restore_suffix.size() &&
        path.substr(path.size() - restore_suffix.size()) == restore_suffix) {
        const std::string filename = UrlDecode(path.substr(
            versions_prefix.size(), path.size() - versions_prefix.size() - restore_suffix.size()));
        std::string error;
        if (!RestoreDataVersion(filename, error)) return ErrorJson(400, "Bad Request", error);
        return OkJson(ResponseEnvelope(true, "Версия данных восстановлена. Перегенерируй расписание."));
    }

    if (method == "GET" && path == "/api/settings") return GetSettings();
    if ((method == "PUT" || method == "PATCH") && path == "/api/settings") return UpdateSettings(body, method == "PATCH");

    if (method == "GET" && path == "/api/settings/solver-config") return GetSolverConfig();
    if ((method == "PUT" || method == "PATCH") && path == "/api/settings/solver-config") {
        return UpdateSolverConfig(body, false);
    }
    if (method == "POST" && path == "/api/settings/solver-config/reset") {
        return UpdateSolverConfig("", true);
    }
    if (method == "GET" && path == "/api/settings/solver-profiles") return GetSolverProfiles();
    const std::string profile_prefix = "/api/settings/solver-profile/";
    if (method == "POST" && path.rfind(profile_prefix, 0) == 0) {
        return ApplySolverProfile(UrlDecode(path.substr(profile_prefix.size())));
    }

    if (method == "PATCH" && path == "/api/groups/bulk")
        return BulkPatchArrayEndpoint("groups", body);
    if (method == "PATCH" && path == "/api/teachers/bulk")
        return BulkPatchArrayEndpoint("teachers", body);

    std::string crud;
    crud = HandleCrud(method, path, body, "/api/groups", "groups");
    if (!crud.empty()) return crud;
    crud = HandleCrud(method, path, body, "/api/teachers", "teachers");
    if (!crud.empty()) return crud;
    crud = HandleCrud(method, path, body, "/api/lessons", "lessons");
    if (!crud.empty()) return crud;
    crud = HandleCrud(method, path, body, "/api/unavailable", "unavailable");
    if (!crud.empty()) return crud;
    crud = HandleCrud(method, path, body, "/api/teacher-unavailable", "teacher_unavailable");
    if (!crud.empty()) return crud;
    crud = HandleCrud(method, path, body, "/api/rooms", "rooms");
    if (!crud.empty()) return crud;
    crud = HandleCrud(method, path, body, "/api/room-types", "room_types");
    if (!crud.empty()) return crud;
    crud = HandleCrud(method, path, body, "/api/substitutions", "substitutions");
    if (!crud.empty()) return crud;
    crud = HandleCrud(method, path, body, "/api/accounting-adjustments", "accounting_adjustments");
    if (!crud.empty()) return crud;

    if (method == "POST" && path == "/api/schedule/validate") {
        if (g_gen.running.load()) {
            return ErrorJson(409, "Conflict", "Дождитесь завершения генерации перед отдельной проверкой");
        }
        JsonParseResult request = body.empty() ? JsonParseResult{} : ParseJson(body);
        if (!body.empty() && (!request.ok || !request.value.IsObject())) {
            return ErrorJson(400, "Bad Request", "Нужен JSON-объект с source или schedule");
        }
        const JsonValue empty_request = JsonValue::MakeObject();
        const JsonValue& request_value = body.empty() ? empty_request : request.value;
        std::string source = JsonString(request_value, "source", "auto");
        if (source != "auto" && source != "manual" && source != "published" && source != "payload") {
            return ErrorJson(400, "Bad Request", "source должен быть auto, manual, published или payload");
        }

        JsonValue schedule;
        std::filesystem::path source_dir;
        if (request_value.At("schedule").IsObject()) {
            schedule = request_value.At("schedule");
            source = "payload";
        } else {
            source_dir = source == "auto"
                ? out_dir
                : (std::filesystem::path("output") / source);
            const std::filesystem::path source_file = source_dir / "schedule_all.json";
            if (!FileExists(source_file)) {
                return ErrorJson(404, "Not Found", "Для выбранного источника нет schedule_all.json");
            }
            JsonParseResult parsed_schedule = ParseJson(ReadFileUtf8(source_file));
            if (!parsed_schedule.ok || !parsed_schedule.value.IsObject()) {
                return ErrorJson(422, "Unprocessable Entity", "schedule_all.json повреждён");
            }
            schedule = parsed_schedule.value;
        }

        ScheduleInputData input;
        std::string input_error;
        if (!LoadScheduleInputData(input, input_error)) {
            return ErrorJson(500, "Internal Server Error", "Не удалось загрузить входные данные: " + input_error);
        }
        ScheduleValidationOptions validation_options;
        validation_options.source = source;
        const ScheduleValidationResult validation =
            ValidateScheduleJson(input, g_solver_config, schedule, validation_options);
        if (!source_dir.empty()) {
            std::ofstream report_file(source_dir / "validation_report.json", std::ios::binary | std::ios::trunc);
            if (report_file) report_file << ToJson(validation.report, 2);
        }
        return JsonResponse(200, "OK", ToJson(validation.report, 2));
    }

    if (method == "POST" && path == "/api/schedule/regenerate") {
        // Уже идёт — отказываем
        if (g_gen.running.load()) {
            return ErrorJson(409, "Conflict", "Генерация уже запущена. Дождитесь завершения или отмените.");
        }

        JsonParseResult audit_source = LoadRoot();
        if (!audit_source.ok) return ErrorJson(500, "Internal Server Error", audit_source.error);
        JsonValue audit = BuildDataAudit(audit_source.value);
        if (!JsonBool(audit, "ok", false)) {
            audit.At("message") = JsonValue::MakeString(
                "Генерация остановлена: исправьте ошибки входных данных на странице «Данные → Аудит».");
            return JsonResponse(422, "Unprocessable Entity", ToJson(audit));
        }

        GenerationOptions opts;
        opts.lock_source = "none";
        std::string gen_mode = "weekly";
        if (!body.empty()) {
            JsonParseResult parsed = ParseJson(body);
            if (parsed.ok && parsed.value.IsObject()) {
                gen_mode = JsonString(parsed.value, "mode", "weekly");
                std::string lock_existing = JsonString(parsed.value, "lock_existing", "none");
                std::string lock_path;
                if (lock_existing == "manual") {
                    lock_path = (std::filesystem::path("output") / "manual" / "schedule_all.json").string();
                } else if (lock_existing == "auto") {
                    lock_path = (std::filesystem::path(output_dir) / "schedule_all.json").string();
                }
                if (!lock_path.empty() && FileExists(lock_path)) {
                    JsonParseResult sched = ParseJson(ReadFileUtf8(lock_path));
                    if (sched.ok && sched.value.IsObject()) {
                        const JsonValue& groups_arr = sched.value.At("groups");
                        if (groups_arr.IsArray()) {
                            for (const JsonValue& g : groups_arr.array_value) {
                                if (!g.IsObject()) continue;
                                const JsonValue& days = g.At("days");
                                if (!days.IsArray()) continue;
                                for (const JsonValue& day : days.array_value) {
                                    if (!day.IsObject()) continue;
                                    Date date{};
                                    if (!ParseDateIso(JsonString(day, "date_iso", ""), date)) {
                                        std::string disp = JsonString(day, "date", "");
                                        if (disp.size() == 10 && disp[2] == '.' && disp[5] == '.') {
                                            try {
                                                date.day = std::stoi(disp.substr(0, 2));
                                                date.month = std::stoi(disp.substr(3, 2));
                                                date.year = std::stoi(disp.substr(6, 4));
                                            } catch (...) { continue; }
                                        } else { continue; }
                                    }
                                    const JsonValue& slots = day.At("slots");
                                    if (!slots.IsArray()) continue;
                                    for (const JsonValue& slot : slots.array_value) {
                                        if (!slot.IsObject()) continue;
                                        int slot_num = JsonInt(slot, "slot", 0);
                                        if (slot_num < 1) continue;
                                        const JsonValue& lessons_arr = slot.At("lessons");
                                        if (!lessons_arr.IsArray()) continue;
                                        for (const JsonValue& l : lessons_arr.array_value) {
                                            if (!l.IsObject()) continue;
                                            int lid = JsonInt(l, "id", -1);
                                            if (lid < 0) continue;
                                            LockedAssignment a;
                                            a.lesson_id = lid;
                                            a.date = date;
                                            a.slot = slot_num - 1;
                                            opts.locked.push_back(a);
                                        }
                                    }
                                }
                            }
                        }
                        opts.lock_source = lock_existing;
                    }
                } else if (lock_existing == "manual" || lock_existing == "auto") {
                    return ErrorJson(404, "Not Found",
                        lock_existing == "manual"
                            ? "Ручное расписание пусто. Открой Конструктор и сохрани хотя бы одно занятие."
                            : "Автогенерации ещё нет — сначала сгенерируй обычное расписание.");
                }
            }
        }

        if (gen_mode == "monolithic") {
            // Монолитный режим — синхронно (без прогресса). Генератор всегда
            // пишет в отдельный кандидат; output/latest заменяется только
            // после независимой полной проверки.
            std::lock_guard<std::mutex> lock(g_schedule_mutex);
            const std::filesystem::path candidate = NewGenerationCandidate(out_dir);
            GenerationResult result = GenerateSchedule(candidate.string(), opts);
            FinalOutputValidation validation;
            if (result.success) {
                validation = ValidateFinalOutput(candidate);
                ApplyFinalOutputGate(result, validation);
                if (result.success) {
                    std::string promotion_error;
                    if (!PromoteValidatedCandidate(candidate, out_dir, promotion_error)) {
                        result.success = false;
                        result.status = "PROMOTION_FAILED";
                        result.message = promotion_error;
                    }
                }
            }
            std::ostringstream rb;
            rb << "{\"success\":" << (result.success ? "true" : "false")
               << ",\"status\":\"" << JsonEscape(result.status) << "\""
               << ",\"message\":\"" << JsonEscape(result.message) << "\""
               << ",\"mode\":\"monolithic\""
               << ",\"validation\":" << ToJson(validation.ToJsonValue(), 0)
               << ",\"async\":false}";
            const bool validation_failed = result.status == "OUTPUT_VALIDATION_FAILED";
            return JsonResponse(result.success ? 200 : (validation_failed ? 422 : 500),
                result.success ? "OK" : (validation_failed ? "Unprocessable Entity" : "Internal Server Error"), rb.str());
        }

        // Недельный режим — async
        g_gen.cancel_requested.store(false);
        g_gen.running.store(true);
        g_gen_start = std::chrono::steady_clock::now();

        // Инициализируем прогресс (предварительно 0 недель, уточнится при старте)
        {
            std::lock_guard<std::mutex> lk(g_gen.mu);
            g_gen.state = "running";
            g_gen.total_weeks = 0;
            g_gen.current_week = 0;
            g_gen.solved_weeks = 0;
            g_gen.weeks.clear();
            g_gen.result_message = "";
            g_gen.total_elapsed = 0.0;
            g_gen.result_success = false;
            g_gen.validation_checked = false;
            g_gen.validation_ok = false;
            g_gen.validation_remaining_hours = 0;
            g_gen.validation_incomplete_lessons = 0;
            g_gen.validation_unassigned_rooms = 0;
            g_gen.validation_hard_errors = 0;
            g_gen.validation_warnings = 0;
            g_gen.validation_hours_source.clear();
        }

        // Захватываем всё нужное для потока
        std::string cap_output_dir = output_dir;
        std::filesystem::path cap_candidate = NewGenerationCandidate(out_dir);
        GenerationOptions cap_opts = opts;

        std::thread([cap_output_dir, cap_candidate, cap_opts]() mutable {
            WeeklyGenCallbacks cbs;
            cbs.cancel_flag = &g_gen.cancel_requested;

            cbs.on_week_start = [](int w, int total, std::string df, std::string dt) {
                std::lock_guard<std::mutex> lk(g_gen.mu);
                // Если недель ещё не знаем — инициализируем
                if (g_gen.total_weeks != total) {
                    g_gen.total_weeks = total;
                    g_gen.weeks.resize(total);
                    for (int i = 0; i < total; i++) {
                        g_gen.weeks[i].num = i + 1;
                        g_gen.weeks[i].status = "pending";
                    }
                }
                g_gen.current_week = w + 1;
                if (w < (int)g_gen.weeks.size()) {
                    g_gen.weeks[w].date_from = df;
                    g_gen.weeks[w].date_to   = dt;
                    g_gen.weeks[w].status = "running";
                }
            };

            cbs.on_week_done = [](int w, int total, std::string df, std::string dt,
                                  std::string status, double elapsed) {
                std::lock_guard<std::mutex> lk(g_gen.mu);
                if (g_gen.total_weeks != total) {
                    g_gen.total_weeks = total;
                    g_gen.weeks.resize(total);
                    for (int i = 0; i < total; i++) {
                        g_gen.weeks[i].num = i + 1;
                        g_gen.weeks[i].status = "pending";
                    }
                }
                if (w < (int)g_gen.weeks.size()) {
                    g_gen.weeks[w].date_from = df;
                    g_gen.weeks[w].date_to   = dt;
                    g_gen.weeks[w].status = status;
                    g_gen.weeks[w].elapsed = elapsed;
                }
                if (status == "done" || status == "skipped") g_gen.solved_weeks++;
                g_gen.total_elapsed = std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - g_gen_start).count();
            };

            GenerationResult result = GenerateScheduleWeekly(cap_candidate.string(), cap_opts, cbs);
            FinalOutputValidation validation;
            if (result.success) {
                std::lock_guard<std::mutex> schedule_lock(g_schedule_mutex);
                validation = ValidateFinalOutput(cap_candidate);
                ApplyFinalOutputGate(result, validation);
                if (result.success) {
                    std::string promotion_error;
                    if (!PromoteValidatedCandidate(cap_candidate, cap_output_dir, promotion_error)) {
                        result.success = false;
                        result.status = "PROMOTION_FAILED";
                        result.message = promotion_error;
                    }
                }
            }

            {
                std::lock_guard<std::mutex> lk(g_gen.mu);
                g_gen.total_elapsed = std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - g_gen_start).count();
                g_gen.result_message = result.message;
                g_gen.result_success = result.success;
                g_gen.validation_checked = validation.checked;
                g_gen.validation_ok = validation.ok;
                g_gen.validation_remaining_hours = validation.remaining_hours;
                g_gen.validation_incomplete_lessons = validation.incomplete_lessons;
                g_gen.validation_unassigned_rooms = validation.unassigned_rooms;
                g_gen.validation_hard_errors = validation.hard_errors;
                g_gen.validation_warnings = validation.warnings;
                g_gen.validation_hours_source = validation.hours_source;
                if (result.status == "CANCELLED") {
                    g_gen.state = "cancelled";
                } else {
                    g_gen.state = result.success ? "done" : "failed";
                }
            }
            g_gen.running.store(false);
        }).detach();

        std::ostringstream rb;
        rb << "{\"started\":true,\"async\":true,\"mode\":\"weekly\""
           << ",\"lock_source\":\"" << JsonEscape(opts.lock_source) << "\""
           << ",\"locked_count\":" << opts.locked.size() << "}";
        return JsonResponse(202, "Accepted", rb.str());
    }

    if (method == "GET" && path == "/api/schedule/progress") {
        std::lock_guard<std::mutex> lk(g_gen.mu);
        std::ostringstream out;
        out << "{\"state\":\"" << JsonEscape(g_gen.state) << "\""
            << ",\"total_weeks\":" << g_gen.total_weeks
            << ",\"current_week\":" << g_gen.current_week
            << ",\"solved_weeks\":" << g_gen.solved_weeks
            << ",\"total_elapsed\":" << std::fixed << std::setprecision(1) << g_gen.total_elapsed
            << ",\"message\":\"" << JsonEscape(g_gen.result_message) << "\""
            << ",\"success\":" << (g_gen.result_success ? "true" : "false")
            << ",\"validation\":{\"checked\":" << (g_gen.validation_checked ? "true" : "false")
            << ",\"ok\":" << (g_gen.validation_ok ? "true" : "false")
            << ",\"remaining_hours\":" << g_gen.validation_remaining_hours
            << ",\"incomplete_lessons\":" << g_gen.validation_incomplete_lessons
            << ",\"unassigned_rooms\":" << g_gen.validation_unassigned_rooms
            << ",\"hard_errors\":" << g_gen.validation_hard_errors
            << ",\"warnings\":" << g_gen.validation_warnings
            << ",\"hours_source\":\"" << JsonEscape(g_gen.validation_hours_source) << "\"}"
            << ",\"weeks\":[";
        for (int i = 0; i < (int)g_gen.weeks.size(); i++) {
            const auto& w = g_gen.weeks[i];
            if (i > 0) out << ",";
            out << "{\"num\":" << w.num
                << ",\"date_from\":\"" << JsonEscape(w.date_from) << "\""
                << ",\"date_to\":\"" << JsonEscape(w.date_to) << "\""
                << ",\"status\":\"" << JsonEscape(w.status) << "\""
                << ",\"elapsed\":" << std::fixed << std::setprecision(1) << w.elapsed << "}";
        }
        out << "]}";
        return JsonResponse(200, "OK", out.str());
    }

    if (method == "POST" && path == "/api/schedule/cancel") {
        if (!g_gen.running.load()) {
            return JsonResponse(200, "OK", "{\"cancelled\":false,\"message\":\"Генерация не запущена\"}");
        }
        g_gen.cancel_requested.store(true);
        return JsonResponse(200, "OK", "{\"cancelled\":true,\"message\":\"Запрос на отмену отправлен\"}");
    }

    if (method == "POST" && path == "/api/schedule/publish") {
        if (g_gen.running.load()) {
            return ErrorJson(409, "Conflict", "Нельзя публиковать расписание во время генерации");
        }
        std::lock_guard<std::mutex> lock(g_schedule_mutex);
        const std::filesystem::path source = out_dir / "schedule_all.json";
        if (!FileExists(source)) return ErrorJson(404, "Not Found", "Сначала сгенерируй расписание");
        const FinalOutputValidation validation = ValidateFinalOutput(out_dir);
        if (!validation.ok) {
            JsonValue details = validation.ToJsonValue();
            JsonValue envelope = ResponseEnvelope(false, validation.message, &details);
            return JsonResponse(409, "Conflict", ToJson(envelope, 2));
        }
        const std::filesystem::path published = std::filesystem::path("output") / "published";
        JsonParseResult snapshot = ParseJson(ReadFileUtf8(source));
        std::string publish_error;
        if (!snapshot.ok || !WriteScheduleSnapshot(published, snapshot.value, publish_error)) {
            return ErrorJson(500, "Internal Server Error",
                publish_error.empty() ? "Не удалось прочитать итоговое расписание" : publish_error);
        }
        return OkJson(ResponseEnvelope(true, "Студенческая версия опубликована."));
    }

    if (method == "GET" && path == "/api/schedule/published") {
        const std::filesystem::path file = std::filesystem::path("output") / "published" / "schedule_all.json";
        if (!FileExists(file)) return ErrorJson(404, "Not Found", "Студенческая версия ещё не опубликована");
        return JsonResponse(200, "OK", ReadFileUtf8(file));
    }

    const std::string published_group_prefix = "/api/schedule/published/group/";
    if (method == "GET" && path.rfind(published_group_prefix, 0) == 0) {
        const std::string value = path.substr(published_group_prefix.size());
        const int group = GroupIndexFromPathValue(value);
        if (group < 0) return ErrorJson(404, "Not Found", "Группа не найдена");
        const std::filesystem::path file = std::filesystem::path("output") / "published" / "groups" /
            ("group_" + std::to_string(group) + ".json");
        if (!FileExists(file)) return ErrorJson(404, "Not Found", "Группа не опубликована");
        return JsonResponse(200, "OK", ReadFileUtf8(file));
    }

    if (method == "GET" && path == "/api/schedule") {
        std::lock_guard<std::mutex> lock(g_schedule_mutex);
        std::filesystem::path file = out_dir / "schedule_all.json";
        if (!FileExists(file)) return ErrorJson(404, "Not Found", "Расписание ещё не сгенерировано. Вызови POST /api/schedule/regenerate.");
        return JsonResponse(200, "OK", ReadFileUtf8(file));
    }

    // ── Manual (Конструктор) endpoints ──
    if (method == "GET" && path == "/api/schedule/manual") {
        std::lock_guard<std::mutex> lock(g_schedule_mutex);
        std::filesystem::path file = std::filesystem::path("output") / "manual" / "schedule_all.json";
        if (!FileExists(file)) return ErrorJson(404, "Not Found", "Ручное расписание пусто. Скопируй из автогенерации или начни с нуля.");
        return JsonResponse(200, "OK", ReadFileUtf8(file));
    }

    if (method == "POST" && path == "/api/schedule/manual") {
        std::lock_guard<std::mutex> lock(g_schedule_mutex);
        JsonParseResult parsed = ParseJson(body);
        if (!parsed.ok || !parsed.value.IsObject()) {
            return ErrorJson(400, "Bad Request", parsed.error.empty() ? "Нужен JSON-объект" : parsed.error);
        }
        std::filesystem::path manual_dir = std::filesystem::path("output") / "manual";
        std::filesystem::path groups_dir = manual_dir / "groups";
        std::error_code ec;
        std::filesystem::create_directories(groups_dir, ec);

        std::ofstream out_all(manual_dir / "schedule_all.json", std::ios::binary);
        if (!out_all) return ErrorJson(500, "Internal Server Error", "Не удалось открыть output/manual/schedule_all.json");
        out_all << ToJson(parsed.value, 2);
        out_all.close();

        const JsonValue& groups_arr = parsed.value.At("groups");
        if (groups_arr.IsArray()) {
            for (const JsonValue& group : groups_arr.array_value) {
                if (!group.IsObject()) continue;
                int gi = JsonInt(group, "group_index", -1);
                if (gi < 0) continue;
                std::ofstream go(groups_dir / ("group_" + std::to_string(gi) + ".json"), std::ios::binary);
                if (go) go << ToJson(group, 2);
            }
        }

        return OkJson(ResponseEnvelope(true, "Ручное расписание сохранено."));
    }

    if (method == "DELETE" && path == "/api/schedule/manual") {
        std::lock_guard<std::mutex> lock(g_schedule_mutex);
        std::filesystem::path manual_dir = std::filesystem::path("output") / "manual";
        std::error_code ec;
        std::filesystem::remove_all(manual_dir, ec);
        return OkJson(ResponseEnvelope(true, "Ручное расписание очищено."));
    }

    if (method == "POST" && path == "/api/schedule/manual/copy-from-auto") {
        std::lock_guard<std::mutex> lock(g_schedule_mutex);
        std::filesystem::path manual_dir = std::filesystem::path("output") / "manual";
        std::filesystem::path groups_dir = manual_dir / "groups";
        std::error_code ec;
        std::filesystem::create_directories(groups_dir, ec);
        std::filesystem::path src_all = out_dir / "schedule_all.json";
        if (!FileExists(src_all)) {
            return ErrorJson(404, "Not Found", "Автогенерации ещё нет. Сначала сгенерируй расписание.");
        }
        std::filesystem::copy_file(src_all, manual_dir / "schedule_all.json", std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) return ErrorJson(500, "Internal Server Error", "Не удалось скопировать schedule_all.json: " + ec.message());

        std::filesystem::path src_groups = out_dir / "groups";
        if (std::filesystem::exists(src_groups, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(src_groups, ec)) {
                if (!entry.is_regular_file()) continue;
                std::filesystem::copy_file(entry.path(), groups_dir / entry.path().filename(), std::filesystem::copy_options::overwrite_existing, ec);
            }
        }
        return OkJson(ResponseEnvelope(true, "Расписание скопировано из автогенерации в Конструктор."));
    }

    const std::string group_prefix = "/api/schedule/group/";
    if (method == "GET" && path.rfind(group_prefix, 0) == 0) {
        std::lock_guard<std::mutex> lock(g_schedule_mutex);
        std::string value = path.substr(group_prefix.size());
        int group = GroupIndexFromPathValue(value);
        if (group < 0) return ErrorJson(404, "Not Found", "Группа не найдена");

        std::filesystem::path file = out_dir / "groups" / ("group_" + std::to_string(group) + ".json");
        if (!FileExists(file)) return ErrorJson(404, "Not Found", "Расписание ещё не сгенерировано. Вызови POST /api/schedule/regenerate.");
        return JsonResponse(200, "OK", ReadFileUtf8(file));
    }

    return ErrorJson(404, "Not Found", "Неизвестный endpoint");
}

int HeaderContentLength(const std::string& request) {
    std::istringstream ss(request);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string lower = Lower(line);
        const std::string prefix = "content-length:";
        if (lower.rfind(prefix, 0) == 0) {
            std::string number = line.substr(prefix.size());
            try { return std::stoi(number); } catch (...) { return 0; }
        }
        if (line.empty()) break;
    }
    return 0;
}

void SendAll(SOCKET client, const std::string& data) {
    const char* ptr = data.data();
    int left = static_cast<int>(data.size());
    while (left > 0) {
        int sent = send(client, ptr, left, 0);
        if (sent == SOCKET_ERROR || sent == 0) break;
        ptr += sent;
        left -= sent;
    }
}

std::string ReceiveFullRequest(SOCKET client_socket) {
    std::string request;
    char buffer[8192];
    int received = recv(client_socket, buffer, sizeof(buffer), 0);
    if (received <= 0) return request;
    request.append(buffer, received);

    size_t header_end = request.find("\r\n\r\n");
    if (header_end == std::string::npos) return request;

    int content_length = HeaderContentLength(request.substr(0, header_end + 4));
    size_t body_start = header_end + 4;
    while (content_length > 0 && request.size() < body_start + static_cast<size_t>(content_length)) {
        received = recv(client_socket, buffer, sizeof(buffer), 0);
        if (received <= 0) break;
        request.append(buffer, received);
    }
    return request;
}

}  // namespace

int RunApiServer(const std::string& host, int port, const std::string& output_dir) {
    EnsureDataFileExists();
    std::string auth_error;
    if (!EnsureAuthConfig(auth_error)) {
        std::cerr << "Authentication initialization failed: " << auth_error << "\n";
        return 1;
    }

    WSADATA wsa_data;
    int startup_result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    if (startup_result != 0) {
        std::cerr << "WSAStartup failed: " << startup_result << "\n";
        return 1;
    }

    SOCKET listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_socket == INVALID_SOCKET) {
        std::cerr << "socket failed: " << WSAGetLastError() << "\n";
        WSACleanup();
        return 1;
    }

    sockaddr_in service;
    service.sin_family = AF_INET;
    service.sin_port = htons(static_cast<u_short>(port));
    inet_pton(AF_INET, host.c_str(), &service.sin_addr);

    if (bind(listen_socket, reinterpret_cast<SOCKADDR*>(&service), sizeof(service)) == SOCKET_ERROR) {
        std::cerr << "bind failed: " << WSAGetLastError() << "\n";
        closesocket(listen_socket);
        WSACleanup();
        return 1;
    }

    if (listen(listen_socket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "listen failed: " << WSAGetLastError() << "\n";
        closesocket(listen_socket);
        WSACleanup();
        return 1;
    }

    std::filesystem::create_directories(output_dir);

    std::cout << "API запущено: http://" << host << ":" << port << "\n";
    std::cout << "Генерация НЕ запускается автоматически. Запусти POST /api/schedule/regenerate\n";
    std::cout << "Файл данных: " << DataFilePath() << "\n";
    std::cout << "Файлы расписания будут в папке: " << output_dir << "\n";

    while (true) {
        SOCKET client_socket = accept(listen_socket, nullptr, nullptr);
        if (client_socket == INVALID_SOCKET) {
            std::cerr << "accept failed: " << WSAGetLastError() << "\n";
            continue;
        }

        std::string request = ReceiveFullRequest(client_socket);
        if (!request.empty()) {
            std::string response = HandleRequest(request, output_dir);
            SendAll(client_socket, response);
        }

        shutdown(client_socket, SD_SEND);
        closesocket(client_socket);
    }

    closesocket(listen_socket);
    WSACleanup();
    return 0;
}

}  // namespace timetable
