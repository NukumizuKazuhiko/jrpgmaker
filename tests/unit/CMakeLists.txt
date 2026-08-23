find_package(Catch2 REQUIRED CONFIG)

add_executable(jrpgmaker_unit_tests)
target_sources(jrpgmaker_unit_tests PRIVATE smoke_test.cpp rhi_contract_test.cpp)
target_link_libraries(jrpgmaker_unit_tests PRIVATE jrpgmaker::core jrpgmaker::rhi Catch2::Catch2WithMain)

list(APPEND CMAKE_MODULE_PATH "${Catch2_DIR}")
include(Catch)
catch_discover_tests(jrpgmaker_unit_tests)
