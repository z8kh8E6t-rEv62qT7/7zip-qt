add_subdirectory(support/fake_launcher_binary)
add_subdirectory(smoke)
add_subdirectory(filemanager)
add_subdirectory(app_logic)
add_subdirectory(gui_matrix)
add_subdirectory(shell_integration)

add_test(
  NAME z7.boundary.third_party.includes
  COMMAND ${CMAKE_COMMAND}
    -DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}
    -P ${PROJECT_SOURCE_DIR}/cmake/VerifyThirdPartyBoundary.cmake)

add_test(
  NAME z7.boundary.third_party.symbols.public
  COMMAND ${CMAKE_COMMAND}
    -DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}
    -P ${PROJECT_SOURCE_DIR}/cmake/VerifyThirdPartySymbolBoundary.cmake)

add_test(
  NAME z7.boundary.native.bridge_usage
  COMMAND ${CMAKE_COMMAND}
    -DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}
    -P ${PROJECT_SOURCE_DIR}/cmake/VerifyNativeBridgeUsage.cmake)
