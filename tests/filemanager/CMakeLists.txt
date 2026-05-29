find_package(Qt6 ${Z7_QT_VERSION_MIN} REQUIRED COMPONENTS Test Widgets)

add_executable(z7_filemanager_behavior
  behavior/internal.h
  behavior/setup.cpp
  behavior/setup_capability.cpp
  behavior/setup_favorites.cpp
  behavior/setup_temp_files.cpp
  behavior/setup_temp_files_root_filter.cpp
  behavior/setup_helpers_archive.cpp
  behavior/setup_helpers_ui.cpp
  behavior/runtime.cpp
  behavior/temp_files_dialog.cpp
  behavior/context_menu.cpp
  behavior/runtime_options.cpp
  behavior/runtime_options_language.cpp
  behavior/runtime_view.cpp
  behavior/runtime_toolbar.cpp
  behavior/archive.cpp
  behavior/archive_commands.cpp
  behavior/archive_flow.cpp
  behavior/archive_flow_navigation.cpp
  behavior/archive_open.cpp
  behavior/archive_open_platform_tracking.cpp
  behavior/startup_open.cpp
  behavior/runner_archive_embedded.cpp
  behavior/runner_open_behavior.cpp
  behavior/runner_extract_command.cpp
  behavior/benchmark.cpp
  behavior/runner_task.cpp
  behavior/runner_path.cpp
  behavior/drag_drop.cpp
  behavior/drag_drop_effect_report.cpp
  behavior/drag_drop_right_button.cpp
  behavior/drag_drop_archive_source.cpp
  behavior/drag_drop_internal_source.cpp
  behavior/drag_drop_internal_source_mixed.cpp
  behavior/drag_drop_internal_source_preview.cpp
  behavior/cases_hash_dialog.cpp
  behavior/cases_sevenzip_menu.cpp
  behavior/main.cpp
)

target_link_libraries(z7_filemanager_behavior
  PRIVATE
    z7_7zfm_app_support
    z7_ui_filemanager
    z7_ui_widgets
    z7_platform_qt
    Qt6::Test
    Qt6::Widgets
)

add_dependencies(z7_filemanager_behavior z7_fake_open_tracker)

add_custom_command(TARGET z7_filemanager_behavior POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "$<TARGET_FILE:z7_fake_open_tracker>"
          "$<TARGET_FILE_DIR:z7_filemanager_behavior>/7zG${CMAKE_EXECUTABLE_SUFFIX}"
  COMMENT "Copying fake 7zG next to z7_filemanager_behavior for bridge launch tests"
  VERBATIM)

# Behaviour tests reach into MainWindow private state and therefore need the
# private main_window implementation headers (DragAwareStructuredListView, ...).
target_include_directories(z7_filemanager_behavior
  PRIVATE
    "${CMAKE_SOURCE_DIR}/src/ui/filemanager/src"
    "${CMAKE_SOURCE_DIR}/src/ui/gui/include"
    "${CMAKE_SOURCE_DIR}/src/ui/gui/src"
    "${CMAKE_SOURCE_DIR}/src/third_party/original_7zip/CPP"
)

z7_set_default_target_options(z7_filemanager_behavior)

target_compile_definitions(z7_filemanager_behavior
  PRIVATE
    Z7_FAKE_LAUNCHER_BIN_DIR="${Z7_FAKE_LAUNCHER_BINARY_DIR}"
)

if(APPLE AND TARGET z7_macos_integration_native_drag)
  set_source_files_properties(
    behavior/drag_drop_archive_source.cpp
    PROPERTIES
      COMPILE_OPTIONS "-x;objective-c++")
  target_link_libraries(z7_filemanager_behavior
    PRIVATE
      z7_macos_integration_native_drag)
  target_compile_definitions(z7_filemanager_behavior
    PRIVATE
      Z7_MACOS_INTEGRATION_ENABLED)
endif()

set(_z7_filemanager_macos_integration_cases
  archiveDragMimeDataDefersMaterializationUntilUrlsRequested
  archiveDragMimeDataIncludesMacPromiseMarkerWhenEnabled
  macArchiveNativePromiseBackendRejectsInvalidRequest
  macNativeDragPolicyAndItemNamesFollowPlatformContract
  macNativeDragFilesystemPromiseCopiesSourceWithoutWriter
  macNativeDragArchivePromiseUsesDirectExportWriter
  macArchiveNativePromiseGraceWaitsForLateRequest
  archiveDragStrictFailureShowsWarningWhenDropRejected
  archiveDragFailureClassTracksRejectWriteFailureAndCancel
)

function(z7_add_filemanager_behavior_case test_case)
  add_test(
    NAME z7.filemanager.behavior.${test_case}
    COMMAND z7_filemanager_behavior ${test_case})
  set(_labels "filemanager")
  if(test_case IN_LIST _z7_filemanager_macos_integration_cases)
    list(APPEND _labels "macos_integration")
  endif()
  set_tests_properties(
    z7.filemanager.behavior.${test_case}
    PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
      LABELS "${_labels}"
      RUN_SERIAL TRUE
      TIMEOUT 120)
endfunction()

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/behavior/internal.h")
file(STRINGS
  "${CMAKE_CURRENT_SOURCE_DIR}/behavior/internal.h"
  _z7_filemanager_behavior_slot_lines
  REGEX "^  void [A-Za-z0-9_]+\\(\\);$")
foreach(_slot_line IN LISTS _z7_filemanager_behavior_slot_lines)
  string(REGEX REPLACE
    "^  void ([A-Za-z0-9_]+)\\(\\);$"
    "\\1"
    _test_case
    "${_slot_line}")
  if(_test_case STREQUAL "init" OR
     _test_case STREQUAL "cleanup" OR
     _test_case STREQUAL "initTestCase" OR
     _test_case STREQUAL "cleanupTestCase")
    continue()
  endif()
  z7_add_filemanager_behavior_case("${_test_case}")
endforeach()
