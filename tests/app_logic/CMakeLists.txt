find_package(Qt6 ${Z7_QT_VERSION_MIN} REQUIRED COMPONENTS Test Core)

add_executable(z7_app_logic_hash_behavior
  hash_behavior/internal.h
  hash_behavior/setup_helpers.cpp
  hash_behavior/setup_hash.cpp
  hash_behavior/setup_list.cpp
  hash_behavior/setup_list_followups.cpp
  hash_behavior/setup.cpp
  hash_behavior/cases_benchmark_split.cpp
  hash_behavior/cases_fileop_models.cpp
  hash_behavior/cases_fileop_models_overwrite.cpp
  hash_behavior/cases_update_mode.cpp
  hash_behavior/cases_events_mapping.cpp
  hash_behavior/cases_nested_archive_sessions.cpp
  hash_behavior/cases_extract_materialized_paths.cpp
  hash_behavior/cases_extract_output_helpers.cpp
  hash_behavior/cases_get_entry_info.cpp
  hash_behavior/cases_extract_budget.cpp
  hash_behavior/cases_list_streaming.cpp
  hash_behavior/benchmark.cpp
  hash_behavior/matrix_coverage.cpp
  hash_behavior/main.cpp
)

target_link_libraries(z7_app_logic_hash_behavior
  PRIVATE
    z7_app_logic
    z7::common
    Qt6::Core
    Qt6::Test
)

target_include_directories(z7_app_logic_hash_behavior
  PRIVATE
    "${CMAKE_SOURCE_DIR}/src/archive_application/src"
    "${CMAKE_SOURCE_DIR}/src/archive_application/src/native_7z"
    "${CMAKE_SOURCE_DIR}/src/third_party/original_7zip/CPP"
)

target_compile_definitions(z7_app_logic_hash_behavior
  PRIVATE
    Z7_APPLOGIC_OUTCOME_MATRIX_PATH="${CMAKE_CURRENT_SOURCE_DIR}/hash_behavior/operation_outcome_matrix.json"
)

z7_set_default_target_options(z7_app_logic_hash_behavior)

add_test(NAME z7.app_logic.hash.behavior COMMAND z7_app_logic_hash_behavior)
