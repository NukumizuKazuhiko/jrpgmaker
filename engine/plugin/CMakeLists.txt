find_package(nlohmann_json CONFIG REQUIRED)

add_library(jrpgmaker_plugin)
add_library(jrpgmaker::plugin ALIAS jrpgmaker_plugin)

target_sources(
  jrpgmaker_plugin
  PRIVATE src/plugin.cpp src/battle.cpp
  PUBLIC FILE_SET HEADERS BASE_DIRS include FILES include/jrpgmaker/plugin/plugin.hpp
                                               include/jrpgmaker/plugin/battle.hpp)

target_include_directories(jrpgmaker_plugin PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(jrpgmaker_plugin PUBLIC nlohmann_json::nlohmann_json)
target_compile_features(jrpgmaker_plugin PUBLIC cxx_std_20)
