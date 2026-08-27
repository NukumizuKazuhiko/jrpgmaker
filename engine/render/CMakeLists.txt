find_package(glm REQUIRED CONFIG)

add_library(jrpgmaker_render)
add_library(jrpgmaker::render ALIAS jrpgmaker_render)

target_sources(
  jrpgmaker_render
  PRIVATE src/style.cpp src/texture_resource.cpp
  PUBLIC FILE_SET HEADERS BASE_DIRS include FILES include/jrpgmaker/render/style.hpp
         include/jrpgmaker/render/texture_resource.hpp)

target_include_directories(jrpgmaker_render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(jrpgmaker_render PUBLIC glm::glm jrpgmaker::rhi jrpgmaker::plugin)
target_compile_features(jrpgmaker_render PUBLIC cxx_std_20)
