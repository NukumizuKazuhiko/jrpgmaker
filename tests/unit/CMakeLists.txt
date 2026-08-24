find_package(Catch2 REQUIRED CONFIG)

add_executable(jrpgmaker_unit_tests)
target_sources(
  jrpgmaker_unit_tests
  PRIVATE smoke_test.cpp rhi_contract_test.cpp stage_test.cpp asset_import_test.cpp scene_test.cpp
          asset_test.cpp)
target_link_libraries(jrpgmaker_unit_tests PRIVATE jrpgmaker::core jrpgmaker::rhi jrpgmaker::shaders
                                                jrpgmaker::assetimport Catch2::Catch2WithMain)

if(WIN32)
  target_sources(jrpgmaker_unit_tests PRIVATE d3d12_backend_test.cpp)
  target_link_libraries(jrpgmaker_unit_tests PRIVATE jrpgmaker::rhi_d3d12)
else()
  target_sources(jrpgmaker_unit_tests PRIVATE vulkan_backend_test.cpp)
  target_link_libraries(jrpgmaker_unit_tests PRIVATE jrpgmaker::rhi_vulkan)
endif()

target_sources(jrpgmaker_unit_tests PRIVATE clear_baseline_test.cpp triangle_test.cpp)

target_link_libraries(jrpgmaker_unit_tests PRIVATE jrpgmaker::golden)
target_compile_definitions(jrpgmaker_unit_tests PRIVATE
                           JRPGMAKER_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../golden"
                           JRPGMAKER_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../assets")

list(APPEND CMAKE_MODULE_PATH "${Catch2_DIR}")
include(Catch)
catch_discover_tests(jrpgmaker_unit_tests)
