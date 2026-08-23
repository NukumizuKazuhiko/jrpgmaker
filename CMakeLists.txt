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
add_subdirectory(app)

if(JRPGMAKER_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests/unit)
endif()
