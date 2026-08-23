add_library(jrpgmaker_rhi_d3d12 STATIC)
add_library(jrpgmaker::rhi_d3d12 ALIAS jrpgmaker_rhi_d3d12)

target_sources(jrpgmaker_rhi_d3d12 PRIVATE src/d3d12_device.cpp)

target_include_directories(jrpgmaker_rhi_d3d12 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(
  jrpgmaker_rhi_d3d12
  PUBLIC jrpgmaker::rhi
  PRIVATE d3d12.lib dxgi.lib dxguid.lib)

target_compile_definitions(jrpgmaker_rhi_d3d12 PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
