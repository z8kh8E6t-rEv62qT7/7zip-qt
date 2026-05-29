find_package(Qt6 ${Z7_QT_VERSION_MIN} REQUIRED COMPONENTS Test Widgets)

add_executable(z7_smoke
  smoke.cpp
)

add_dependencies(z7_smoke 7zG)
if(TARGET z7_sfx_module_7z)
  add_dependencies(z7_smoke z7_sfx_module_7z)
endif()

add_custom_command(TARGET z7_smoke POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "$<TARGET_FILE:7zG>"
          "$<TARGET_FILE_DIR:z7_smoke>/7zG${CMAKE_EXECUTABLE_SUFFIX}"
  COMMENT "Copying real 7zG next to z7_smoke for File Manager launch smoke tests"
  VERBATIM)

if(TARGET z7_sfx_module_7z)
  add_custom_target(z7_smoke_copy_sfx_module
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:z7_smoke>"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:z7_sfx_module_7z>"
            "$<TARGET_FILE_DIR:z7_smoke>/7z.sfx"
    DEPENDS z7_sfx_module_7z
    COMMENT "Copying 7z SFX module next to z7_smoke"
    VERBATIM)
  add_dependencies(z7_smoke z7_smoke_copy_sfx_module)
endif()

target_link_libraries(z7_smoke
  PRIVATE
    z7_ui_filemanager
    z7_ui_gui
    Qt6::Test
    Qt6::Widgets
)

target_include_directories(z7_smoke
  PRIVATE
    "${CMAKE_SOURCE_DIR}/src/qt_application_support/src"
)

target_compile_definitions(z7_smoke
  PRIVATE
    Z7_REAL_7ZG_PATH="$<TARGET_FILE:7zG>"
)

z7_set_default_target_options(z7_smoke)

add_test(NAME z7.smoke COMMAND z7_smoke)
set_tests_properties(z7.smoke PROPERTIES
  ENVIRONMENT "QT_SCALE_FACTOR=1")

add_test(NAME z7.smoke.hidpi.150 COMMAND z7_smoke)
set_tests_properties(z7.smoke.hidpi.150 PROPERTIES
  ENVIRONMENT "QT_SCALE_FACTOR=1.5")

add_test(NAME z7.smoke.hidpi.200 COMMAND z7_smoke)
set_tests_properties(z7.smoke.hidpi.200 PROPERTIES
  ENVIRONMENT "QT_SCALE_FACTOR=2")
