cmake_minimum_required(VERSION 3.20)

project(timetable_solver LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# OR-Tools должен быть установлен в системе или доступен через CMAKE_PREFIX_PATH.
find_package(ortools CONFIG REQUIRED)

add_executable(timetable_solver
    src/main.cpp
    src/config.cpp
    src/types.cpp
    src/date_utils.cpp
    src/format_utils.cpp
    src/model_utils.cpp
    src/diagnostics.cpp
    src/output_writers.cpp
    src/lessons_data.cpp
    src/scheduler.cpp
    src/http_server.cpp
    src/json_utils.cpp
    src/data_store.cpp
    src/runtime_config.cpp
    src/scheduler_rules.cpp
    src/schedule_validator.cpp
    src/room_allocator.cpp
    src/auth.cpp
)

# Small CP-SAT companion used by the short-period preparation scripts.  Keeping
# quota selection in an exact model avoids the teacher-load shortfalls that a
# greedy selector can leave behind before the timetable model even starts.
add_executable(quota_optimizer
    src/quota_optimizer.cpp
    src/json_utils.cpp
)

target_include_directories(quota_optimizer PRIVATE include)
target_link_libraries(quota_optimizer PRIVATE ortools::ortools)

target_include_directories(timetable_solver PRIVATE include)

target_link_libraries(timetable_solver PRIVATE ortools::ortools)

if (WIN32)
    target_link_libraries(timetable_solver PRIVATE ws2_32 bcrypt)
endif()

include(CTest)
if (BUILD_TESTING)
    add_executable(scheduler_rules_test
        tests/scheduler_rules_test.cpp
        src/scheduler_rules.cpp
        src/types.cpp
    )
    target_include_directories(scheduler_rules_test PRIVATE include)
    target_link_libraries(scheduler_rules_test PRIVATE ortools::ortools)
    add_test(NAME scheduler_rules_cpp_regression COMMAND scheduler_rules_test)
    set_tests_properties(scheduler_rules_cpp_regression PROPERTIES TIMEOUT 15)

    add_executable(schedule_validator_test
        tests/schedule_validator_test.cpp
        src/schedule_validator.cpp
        src/runtime_config.cpp
        src/json_utils.cpp
        src/data_store.cpp
        src/date_utils.cpp
        src/types.cpp
        src/config.cpp
    )
    target_include_directories(schedule_validator_test PRIVATE include)
    target_link_libraries(schedule_validator_test PRIVATE ortools::ortools)
    if (WIN32)
        target_link_libraries(schedule_validator_test PRIVATE bcrypt)
    endif()
    add_test(NAME schedule_validator_cpp_regression COMMAND schedule_validator_test)
    set_tests_properties(schedule_validator_cpp_regression PROPERTIES TIMEOUT 15)
endif()

if (BUILD_TESTING AND WIN32)
    add_test(
        NAME fake_grid_api_smoke
        COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_SOURCE_DIR}/tests/fake_grid_smoke.ps1"
            -SolverPath "$<TARGET_FILE:timetable_solver>"
    )
    set_tests_properties(fake_grid_api_smoke PROPERTIES TIMEOUT 45)
    add_test(
        NAME transfer_bundle_api_smoke
        COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_SOURCE_DIR}/tests/transfer_bundle_smoke.ps1"
            -SolverPath "$<TARGET_FILE:timetable_solver>"
    )
    set_tests_properties(transfer_bundle_api_smoke PROPERTIES TIMEOUT 45)
    add_test(
        NAME daily_limits_solver_regression
        COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_SOURCE_DIR}/tests/daily_limits_smoke.ps1"
            -SolverPath "$<TARGET_FILE:timetable_solver>"
    )
    set_tests_properties(daily_limits_solver_regression PROPERTIES TIMEOUT 60)
endif()
