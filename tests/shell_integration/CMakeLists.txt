find_package(Qt6 ${Z7_QT_VERSION_MIN} REQUIRED COMPONENTS Test Core)

add_executable(z7_shell_integration_behavior
  behavior/main.cpp
)

target_link_libraries(z7_shell_integration_behavior
  PRIVATE
    z7_shell_integration_menu
    Qt6::Core
    Qt6::Test
)

z7_set_default_target_options(z7_shell_integration_behavior)

add_test(NAME z7.shell_integration.behavior COMMAND z7_shell_integration_behavior)
set_tests_properties(
  z7.shell_integration.behavior
  PROPERTIES
    LABELS "shell_integration"
    RUN_SERIAL TRUE)
