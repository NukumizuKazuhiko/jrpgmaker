find_package(SDL3 CONFIG REQUIRED)

add_executable(jrpgmaker_app src/main.cpp)

target_link_libraries(
  jrpgmaker_app
  PRIVATE jrpgmaker::core jrpgmaker::rhi jrpgmaker::shaders SDL3::SDL3)

if(WIN32)
  target_link_libraries(jrpgmaker_app PRIVATE jrpgmaker::rhi_d3d12)
else()
  target_link_libraries(jrpgmaker_app PRIVATE jrpgmaker::rhi_vulkan)
endif()

if(WIN32 AND SDL3_SHARED)
  add_custom_command(
    TARGET jrpgmaker_app POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:SDL3::SDL3>" "$<TARGET_FILE_DIR:jrpgmaker_app>")
endif()
