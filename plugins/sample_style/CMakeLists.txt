add_library(jrpgmaker_sample_style)
add_library(jrpgmaker::sample_style ALIAS jrpgmaker_sample_style)

target_sources(
  jrpgmaker_sample_style
  PRIVATE src/style.cpp
  PUBLIC FILE_SET HEADERS BASE_DIRS include FILES include/jrpgmaker/plugins/sample_style/style.hpp)

target_include_directories(jrpgmaker_sample_style PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(jrpgmaker_sample_style PUBLIC jrpgmaker::render)
target_compile_features(jrpgmaker_sample_style PUBLIC cxx_std_20)
