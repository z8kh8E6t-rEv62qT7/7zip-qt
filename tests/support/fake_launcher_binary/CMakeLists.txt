find_package(Qt6 ${Z7_QT_VERSION_MIN} REQUIRED COMPONENTS Core)

add_executable(z7_fake_open_tracker
  main.cpp
)

target_link_libraries(z7_fake_open_tracker
  PRIVATE
    z7_task_ipc_runtime
    Qt6::Core
)

z7_set_default_target_options(z7_fake_open_tracker)

set(_z7_fake_tracker_dir "${CMAKE_CURRENT_BINARY_DIR}")
set(_z7_fake_tracker_program "${_z7_fake_tracker_dir}/z7_fake_open_tracker")

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/launcher_wrapper.sh.in"
  "${_z7_fake_tracker_dir}/open"
  @ONLY
)

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/launcher_wrapper.sh.in"
  "${_z7_fake_tracker_dir}/xdg-open"
  @ONLY
)

file(CHMOD
  "${_z7_fake_tracker_dir}/open"
  "${_z7_fake_tracker_dir}/xdg-open"
  PERMISSIONS
    OWNER_READ OWNER_WRITE OWNER_EXECUTE
    GROUP_READ GROUP_EXECUTE
    WORLD_READ WORLD_EXECUTE
)

set(Z7_FAKE_LAUNCHER_BINARY_DIR "${_z7_fake_tracker_dir}" PARENT_SCOPE)
