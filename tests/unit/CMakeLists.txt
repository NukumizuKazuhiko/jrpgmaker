find_package(Catch2 REQUIRED CONFIG)

add_executable(jrpgmaker_unit_tests)
target_sources(
  jrpgmaker_unit_tests
  PRIVATE smoke_test.cpp rhi_contract_test.cpp stage_test.cpp asset_import_test.cpp scene_test.cpp
          asset_test.cpp camera_test.cpp async_loader_test.cpp event_bus_test.cpp flag_store_test.cpp
          event_script_test.cpp event_runner_test.cpp event_lint_test.cpp encounter_test.cpp flag_trigger_test.cpp
          text_layout_test.cpp font_shaper_test.cpp lua_binding_test.cpp widget_test.cpp
          animation_test.cpp character_controller_test.cpp pathfinding_test.cpp camera_rig_test.cpp
          interaction_test.cpp map_data_test.cpp render_style_test.cpp render_style_adapters_test.cpp
          cutscene_audio_ui_test.cpp dialog_presentation_test.cpp runtime_overlay_test.cpp editor_resources_test.cpp
          editor_shell_test.cpp editor_session_test.cpp ui_interaction_test.cpp project_workspace_test.cpp form_projection_test.cpp ui_draw_adapter_test.cpp
          rmlui_editor_document_test.cpp
          editor_user_settings_test.cpp
          plugin_test.cpp battle_plugins_test.cpp calendar_test.cpp save_test.cpp schedule_test.cpp p7_vertical_slice_test.cpp
          vertical_slice_test.cpp localization_test.cpp p12_acceptance_test.cpp)
target_sources(jrpgmaker_unit_tests PRIVATE input_actions_test.cpp)
target_link_libraries(jrpgmaker_unit_tests PRIVATE jrpgmaker::core jrpgmaker::domain jrpgmaker::audio jrpgmaker::rhi
                                                jrpgmaker::shaders jrpgmaker::assetimport
                                                jrpgmaker::ui jrpgmaker::render jrpgmaker::plugin
                                                jrpgmaker::plugins jrpgmaker::project
                                                jrpgmaker::sample_unlit jrpgmaker::sample_style
                           jrpgmaker::sample_instant jrpgmaker::sample_turn_based
                                                jrpgmaker::editor_host Catch2::Catch2WithMain)

if(WIN32)
  target_sources(jrpgmaker_unit_tests PRIVATE d3d12_backend_test.cpp)
  target_link_libraries(jrpgmaker_unit_tests PRIVATE jrpgmaker::rhi_d3d12)
  # UTF-8 source literals (CJK layout tests) must not trip C4819.
  target_compile_options(jrpgmaker_unit_tests PRIVATE /utf-8)
else()
  target_sources(jrpgmaker_unit_tests PRIVATE vulkan_backend_test.cpp)
  target_link_libraries(jrpgmaker_unit_tests PRIVATE jrpgmaker::rhi_vulkan)
endif()

target_sources(jrpgmaker_unit_tests PRIVATE clear_baseline_test.cpp triangle_test.cpp
                                               scene_golden_test.cpp camera_golden_test.cpp
                                               texture_quad_test.cpp skin_golden_test.cpp)

target_link_libraries(jrpgmaker_unit_tests PRIVATE jrpgmaker::golden)
target_include_directories(jrpgmaker_unit_tests PRIVATE
                           ${CMAKE_CURRENT_SOURCE_DIR}/../../templates/plugin_minimal)
target_compile_definitions(jrpgmaker_unit_tests PRIVATE
                           JRPGMAKER_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../golden"
                           JRPGMAKER_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../assets"
                           JRPGMAKER_EDITOR_RESOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../tools/editor/resources"
                           JRPGMAKER_EDITOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../tools/editor"
                           JRPGMAKER_RUNTIME_EXECUTABLE="$<TARGET_FILE:jrpgmaker_app>")
add_dependencies(jrpgmaker_unit_tests jrpgmaker_app)

list(APPEND CMAKE_MODULE_PATH "${Catch2_DIR}")
include(Catch)
catch_discover_tests(jrpgmaker_unit_tests)
