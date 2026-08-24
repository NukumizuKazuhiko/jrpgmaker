add_library(jrpgmaker_golden STATIC golden_image.cpp)
add_library(jrpgmaker::golden ALIAS jrpgmaker_golden)
target_include_directories(jrpgmaker_golden PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(jrpgmaker_goldenimage main.cpp)
target_link_libraries(
  jrpgmaker_goldenimage
  PRIVATE jrpgmaker::core jrpgmaker::rhi jrpgmaker::shaders jrpgmaker::golden)
if(WIN32)
  target_link_libraries(jrpgmaker_goldenimage PRIVATE jrpgmaker::rhi_d3d12)
else()
  target_link_libraries(jrpgmaker_goldenimage PRIVATE jrpgmaker::rhi_vulkan)
endif()