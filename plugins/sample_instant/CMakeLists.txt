add_library(jrpgmaker_sample_instant)
add_library(jrpgmaker::sample_instant ALIAS jrpgmaker_sample_instant)

target_sources(
  jrpgmaker_sample_instant
  PRIVATE src/instant.cpp
  PUBLIC FILE_SET HEADERS BASE_DIRS include
         FILES include/jrpgmaker/plugins/sample_instant/instant.hpp)

target_include_directories(jrpgmaker_sample_instant PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(jrpgmaker_sample_instant PUBLIC jrpgmaker::plugin)
target_compile_features(jrpgmaker_sample_instant PUBLIC cxx_std_20)
