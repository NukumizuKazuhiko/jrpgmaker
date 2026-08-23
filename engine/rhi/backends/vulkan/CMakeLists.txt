find_package(volk CONFIG REQUIRED)

add_library(jrpgmaker_rhi_vulkan STATIC)
add_library(jrpgmaker::rhi_vulkan ALIAS jrpgmaker_rhi_vulkan)

target_sources(jrpgmaker_rhi_vulkan PRIVATE src/vulkan_device.cpp)

target_include_directories(jrpgmaker_rhi_vulkan PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(
  jrpgmaker_rhi_vulkan
  PUBLIC jrpgmaker::rhi
  PRIVATE volk::volk)
