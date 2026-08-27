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
          cutscene_audio_ui_test.cpp
          plugin_test.cpp battle_plugins_test.cpp calendar_test.cpp save_test.cpp schedule_test.cpp p7_vertical_slice_test.cpp
          vertical_slice_test.cpp)
target_link_libraries(jrpgmaker_unit_tests PRIVATE jrpgmaker::core jrpgmaker::domain jrpgmaker::audio jrpgmaker::rhi
                                                jrpgmaker::shaders jrpgmaker::assetimport
                                                jrpgmaker::ui jrpgmaker::render jrpgmaker::plugin
                                                jrpgmaker::sample_unlit jrpgmaker::sample_style
                                                jrpgmaker::sample_instant jrpgmaker::sample_turn_based
                                                Catch2::Catch2WithMain)

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
target_compile_definitions(jrpgmaker_unit_tests PRIVATE
                           JRPGMAKER_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../golden"
                           JRPGMAKER_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../assets")

list(APPEND CMAKE_MODULE_PATH "${Catch2_DIR}")
include(Catch)
catch_discover_tests(jrpgmaker_unit_tests)
