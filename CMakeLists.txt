cmake_minimum_required(VERSION 3.28)

project(jrpgmaker VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4 /WX /permissive- /EHsc)
else()
  add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

option(JRPGMAKER_BUILD_TESTS "Build unit tests" ON)

add_subdirectory(engine/core)
add_subdirectory(engine/domain)
add_subdirectory(engine/audio)
add_subdirectory(engine/plugin)
add_subdirectory(engine/rhi)
add_subdirectory(engine/ui)
add_subdirectory(engine/render)
add_subdirectory(plugins)
add_subdirectory(shaders)
if(WIN32)
  add_subdirectory(engine/rhi/backends/d3d12)
else()
  add_subdirectory(engine/rhi/backends/vulkan)
endif()
add_subdirectory(tools/assetimport)
add_subdirectory(app)
add_subdirectory(tools/goldenimage)
add_subdirectory(tools/eventlint)

if(JRPGMAKER_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests/unit)
  add_test(NAME golden_style_unlit_matches_reference
           COMMAND jrpgmaker_goldenimage compare-project-style
                   "${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/style_unlit_64x64.ppm"
                   "${CMAKE_CURRENT_SOURCE_DIR}/assets/data/project_demo.json" 1)
  add_test(NAME golden_style_accent_matches_reference
           COMMAND jrpgmaker_goldenimage compare-project-style
                   "${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/style_accent_64x64.ppm"
                   "${CMAKE_CURRENT_SOURCE_DIR}/assets/data/project_accent.json" 1)
endif()
