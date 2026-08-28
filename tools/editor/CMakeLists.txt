find_package(SDL3 CONFIG REQUIRED)

add_library(jrpgmaker_editor_host STATIC src/editor_shell.cpp src/form_projection.cpp)
add_library(jrpgmaker::editor_host ALIAS jrpgmaker_editor_host)
target_sources(
  jrpgmaker_editor_host
  PUBLIC FILE_SET HEADERS BASE_DIRS include FILES include/jrpgmaker/editor/editor_shell.hpp
         include/jrpgmaker/editor/form_projection.hpp)
target_include_directories(jrpgmaker_editor_host PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(jrpgmaker_editor_host PUBLIC jrpgmaker::ui jrpgmaker::project)
target_compile_features(jrpgmaker_editor_host PUBLIC cxx_std_20)

add_executable(jrpgmaker_editor main.cpp)
target_link_libraries(jrpgmaker_editor PRIVATE jrpgmaker::ui jrpgmaker::project
                                                jrpgmaker::editor_host SDL3::SDL3)
target_compile_features(jrpgmaker_editor PRIVATE cxx_std_20)
target_compile_definitions(
  jrpgmaker_editor
  PRIVATE JRPGMAKER_EDITOR_RESOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/resources")

if(WIN32 AND SDL3_SHARED)
  add_custom_command(
    TARGET jrpgmaker_editor POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:SDL3::SDL3>" "$<TARGET_FILE_DIR:jrpgmaker_editor>")
endif()
