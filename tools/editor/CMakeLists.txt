find_package(SDL3 CONFIG REQUIRED)

add_executable(jrpgmaker_editor main.cpp)
target_link_libraries(jrpgmaker_editor PRIVATE jrpgmaker::ui jrpgmaker::project SDL3::SDL3)
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
