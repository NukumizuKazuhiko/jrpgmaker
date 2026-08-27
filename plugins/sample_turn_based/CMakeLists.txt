add_library(jrpgmaker_sample_turn_based)
add_library(jrpgmaker::sample_turn_based ALIAS jrpgmaker_sample_turn_based)

target_sources(
  jrpgmaker_sample_turn_based
  PRIVATE src/turn_based.cpp
  PUBLIC FILE_SET HEADERS BASE_DIRS include
         FILES include/jrpgmaker/plugins/sample_turn_based/turn_based.hpp)

target_include_directories(jrpgmaker_sample_turn_based PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(jrpgmaker_sample_turn_based PUBLIC jrpgmaker::plugin)
target_compile_features(jrpgmaker_sample_turn_based PUBLIC cxx_std_20)
