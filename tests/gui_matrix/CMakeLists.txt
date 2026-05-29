find_package(Qt6 ${Z7_QT_VERSION_MIN} REQUIRED COMPONENTS Test Core)

add_executable(z7_gui_behavior_matrix
  matrix/internal.h
  matrix/matrix.cpp
  matrix/main.cpp
)

target_link_libraries(z7_gui_behavior_matrix
  PRIVATE
    Qt6::Core
    Qt6::Test
)

target_compile_definitions(z7_gui_behavior_matrix
  PRIVATE
    Z7_GUI_BEHAVIOR_MATRIX_PATH="${CMAKE_CURRENT_SOURCE_DIR}/gui_behavior_matrix.json"
    Z7_SOURCE_ROOT_PATH="${CMAKE_SOURCE_DIR}"
)

z7_set_default_target_options(z7_gui_behavior_matrix)

add_test(NAME z7.gui.behavior.matrix COMMAND z7_gui_behavior_matrix)
