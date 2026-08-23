add_library(jrpgmaker_rhi INTERFACE)
add_library(jrpgmaker::rhi ALIAS jrpgmaker_rhi)

target_sources(
  jrpgmaker_rhi
  INTERFACE FILE_SET HEADERS
            BASE_DIRS include
            FILES include/jrpgmaker/rhi/command_list.hpp
                  include/jrpgmaker/rhi/common.hpp
                  include/jrpgmaker/rhi/descriptors.hpp
                  include/jrpgmaker/rhi/device.hpp
                  include/jrpgmaker/rhi/device_factory.hpp
                  include/jrpgmaker/rhi/handles.hpp
                  include/jrpgmaker/rhi/swapchain.hpp)

target_include_directories(jrpgmaker_rhi INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_compile_features(jrpgmaker_rhi INTERFACE cxx_std_20)
