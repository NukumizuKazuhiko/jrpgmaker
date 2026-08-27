add_library(jrpgmaker_sample_unlit)
add_library(jrpgmaker::sample_unlit ALIAS jrpgmaker_sample_unlit)

target_sources(
  jrpgmaker_sample_unlit
  PRIVATE src/unlit.cpp
  PUBLIC FILE_SET HEADERS BASE_DIRS include FILES include/jrpgmaker/plugins/sample_unlit/unlit.hpp)

target_include_directories(jrpgmaker_sample_unlit PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(jrpgmaker_sample_unlit PUBLIC jrpgmaker::render)
target_compile_features(jrpgmaker_sample_unlit PUBLIC cxx_std_20)
