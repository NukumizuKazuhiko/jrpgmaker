cmake_minimum_required(VERSION 3.24)
project(jrpgmaker_sdk_consumer LANGUAGES CXX)

find_package(jrpgmaker CONFIG REQUIRED)

add_executable(jrpgmaker_sdk_consumer main.cpp)
target_compile_features(jrpgmaker_sdk_consumer PRIVATE cxx_std_20)
target_link_libraries(jrpgmaker_sdk_consumer PRIVATE jrpgmaker::plugin)
