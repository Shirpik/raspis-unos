#include <clocale>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "data_store.h"
#include "http_server.h"
#include "json_utils.h"
#include "schedule_validator.h"
#include "scheduler.h"

int main(int argc, char* argv[]) {
    std::setlocale(LC_ALL, "ru_RU.UTF-8");

    if (argc > 1 && std::string(argv[1]) == "--validate") {
        std::string schedule_path = "output/latest/schedule_all.json";
        std::string report_path;
        std::string source = "cli";
        for (int index = 2; index < argc; ++index) {
            const std::string argument = argv[index];
            if (argument == "--schedule" && index + 1 < argc) schedule_path = argv[++index];
            else if (argument == "--report" && index + 1 < argc) report_path = argv[++index];
            else if (argument == "--source" && index + 1 < argc) source = argv[++index];
            else {
                std::cerr << "Неизвестный аргумент проверки: " << argument << "\n";
                return 2;
            }
        }
        timetable::ScheduleInputData input;
        std::string error;
        if (!timetable::LoadScheduleInputData(input, error)) {
            std::cerr << "Не удалось загрузить данные: " << error << "\n";
            return 2;
        }
        std::ifstream schedule_file(schedule_path, std::ios::binary);
        std::ostringstream buffer;
        buffer << schedule_file.rdbuf();
        const timetable::JsonParseResult parsed = timetable::ParseJson(buffer.str());
        if (!schedule_file || !parsed.ok || !parsed.value.IsObject()) {
            std::cerr << "Не удалось прочитать расписание: " << schedule_path << "\n";
            return 2;
        }
        timetable::ScheduleValidationOptions options;
        options.source = source;
        const timetable::ScheduleValidationResult validation =
            timetable::ValidateScheduleJson(input, timetable::g_solver_config, parsed.value, options);
        const std::string rendered = timetable::ToJson(validation.report, 2);
        if (!report_path.empty()) {
            std::ofstream report_file(report_path, std::ios::binary | std::ios::trunc);
            if (!report_file) {
                std::cerr << "Не удалось записать отчёт: " << report_path << "\n";
                return 2;
            }
            report_file << rendered;
        }
        std::cout << rendered << "\n";
        return validation.ok ? 0 : 1;
    }

    // Headless fail-closed generation is useful for repeatable production
    // builds and independent validation; the desktop/API mode remains the
    // default for a numeric port argument.
    if (argc > 1 && std::string(argv[1]) == "--generate") {
        std::string output_dir = "output/latest";
        timetable::GenerationOptions options;
        options.lock_source = "none";
        for (int index = 2; index < argc; ++index) {
            const std::string argument = argv[index];
            if (argument == "--output" && index + 1 < argc) {
                output_dir = argv[++index];
                continue;
            }
            if (argument == "--locks" && index + 1 < argc) {
                const std::string path = argv[++index];
                std::ifstream input(path, std::ios::binary);
                std::ostringstream buffer;
                buffer << input.rdbuf();
                const timetable::JsonParseResult parsed = timetable::ParseJson(buffer.str());
                const timetable::JsonValue& assignments = parsed.value.At("assignments");
                if (!parsed.ok || !assignments.IsArray()) {
                    std::cerr << "Некорректный файл фиксаций: " << path << "\n";
                    return 2;
                }
                for (const timetable::JsonValue& item : assignments.array_value) {
                    timetable::LockedAssignment assignment;
                    assignment.lesson_id = timetable::JsonInt(item, "lesson_id", -1);
                    assignment.slot = timetable::JsonInt(item, "slot", -1);
                    if (assignment.lesson_id < 0 || assignment.slot < 0 ||
                        !timetable::ParseDateIso(
                            timetable::JsonString(item, "date", ""), assignment.date)) {
                        std::cerr << "Некорректная фиксация в " << path << "\n";
                        return 2;
                    }
                    options.locked.push_back(assignment);
                }
                options.lock_source = timetable::JsonString(parsed.value, "source", "manual");
                continue;
            }
            std::cerr << "Неизвестный аргумент генерации: " << argument << "\n";
            return 2;
        }
        const timetable::GenerationResult result =
            timetable::GenerateScheduleWeekly(output_dir, options);
        if (!result.success) {
            std::cerr << result.status << ": " << result.message << "\n";
        }
        return result.success ? 0 : 1;
    }

    const std::string host = "127.0.0.1";
    int port = 8080;
    if (argc > 1) {
        try {
            const int requested = std::stoi(argv[1]);
            if (requested >= 1024 && requested <= 65535) port = requested;
        } catch (...) {
            std::cerr << "Некорректный порт, используется 8080\n";
        }
    }
    const std::string output_dir = "output/latest";

    std::cout << "Timetable Solver API\n";
    return timetable::RunApiServer(host, port, output_dir);
}
