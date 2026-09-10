find_package(SDL3 CONFIG REQUIRED)

include(${CMAKE_SOURCE_DIR}/cmake/rmlui.cmake)

add_library(jrpgmaker_editor_host STATIC src/editor_shell.cpp src/form_projection.cpp src/editor_session.cpp src/editor_workspace_controller.cpp src/preview_process.cpp src/editor_window.cpp src/editor_user_settings.cpp src/rmlui_input.cpp src/rmlui_editor_view.cpp src/rmlui_editor_document.cpp)
add_library(jrpgmaker::editor_host ALIAS jrpgmaker_editor_host)
target_sources(
  jrpgmaker_editor_host
  PUBLIC FILE_SET HEADERS BASE_DIRS include FILES include/jrpgmaker/editor/editor_shell.hpp
         include/jrpgmaker/editor/form_projection.hpp include/jrpgmaker/editor/editor_session.hpp
         include/jrpgmaker/editor/editor_workspace_controller.hpp
         include/jrpgmaker/editor/editor_window.hpp
         include/jrpgmaker/editor/editor_user_settings.hpp
         include/jrpgmaker/editor/preview_process.hpp
         include/jrpgmaker/editor/rmlui_input.hpp
         include/jrpgmaker/editor/rmlui_editor_view.hpp
         include/jrpgmaker/editor/rmlui_editor_document.hpp)
target_include_directories(jrpgmaker_editor_host PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(jrpgmaker_editor_host PUBLIC jrpgmaker::ui jrpgmaker::project jrpgmaker::render
                                                   jrpgmaker::rhi RmlUi::Core SDL3::SDL3)
target_compile_features(jrpgmaker_editor_host PUBLIC cxx_std_20)
if(MSVC)
  target_compile_options(jrpgmaker_editor_host PRIVATE /utf-8)
endif()

add_executable(jrpgmaker_editor main.cpp)
target_link_libraries(jrpgmaker_editor PRIVATE jrpgmaker::ui jrpgmaker::project
                                                jrpgmaker::editor_host jrpgmaker::render
                                                jrpgmaker::rhi jrpgmaker::shaders jrpgmaker::plugins
                                                SDL3::SDL3)
target_compile_features(jrpgmaker_editor PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(jrpgmaker_editor PRIVATE /utf-8)
endif()
target_compile_definitions(
  jrpgmaker_editor
  PRIVATE JRPGMAKER_EDITOR_RESOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/resources")
target_compile_definitions(jrpgmaker_editor PRIVATE
                           JRPGMAKER_RUNTIME_EXECUTABLE="$<TARGET_FILE:jrpgmaker_app>")

if(WIN32 AND SDL3_SHARED)
  add_custom_command(
    TARGET jrpgmaker_editor POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:SDL3::SDL3>" "$<TARGET_FILE_DIR:jrpgmaker_editor>")
endif()

if(WIN32)
  add_custom_command(
    TARGET jrpgmaker_editor POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:RmlUi::Core>" "$<TARGET_FILE_DIR:jrpgmaker_editor>")
endif()

if(WIN32)
  target_link_libraries(jrpgmaker_editor PRIVATE jrpgmaker::rhi_d3d12)
else()
  target_link_libraries(jrpgmaker_editor PRIVATE jrpgmaker::rhi_vulkan)
endif()
