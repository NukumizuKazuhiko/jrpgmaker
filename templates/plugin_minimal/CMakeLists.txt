cmake_minimum_required(VERSION 3.24)

project(vendor_minimal_plugin LANGUAGES CXX)

add_library(vendor_minimal_plugin minimal.cpp)
target_compile_features(vendor_minimal_plugin PUBLIC cxx_std_20)
target_link_libraries(vendor_minimal_plugin PUBLIC jrpgmaker::plugin)
target_include_directories(vendor_minimal_plugin PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
