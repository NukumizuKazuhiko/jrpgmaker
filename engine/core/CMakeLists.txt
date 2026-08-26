find_package(EnTT REQUIRED CONFIG)
find_package(glm REQUIRED CONFIG)
find_package(nlohmann_json CONFIG REQUIRED)

add_library(jrpgmaker_core)
add_library(jrpgmaker::core ALIAS jrpgmaker_core)

target_sources(
  jrpgmaker_core
  PRIVATE src/version.cpp src/stage.cpp src/scene.cpp src/asset.cpp src/camera.cpp
          src/animation.cpp src/character_controller.cpp src/pathfinding.cpp src/camera_rig.cpp
          src/map_data.cpp
  PUBLIC FILE_SET HEADERS BASE_DIRS include
                FILES include/jrpgmaker/core/version.hpp include/jrpgmaker/core/stage.hpp
                     include/jrpgmaker/core/mesh.hpp include/jrpgmaker/core/scene.hpp
                     include/jrpgmaker/core/asset.hpp include/jrpgmaker/core/camera.hpp
                     include/jrpgmaker/core/event_bus.hpp include/jrpgmaker/core/animation.hpp
                     include/jrpgmaker/core/character_controller.hpp
                     include/jrpgmaker/core/pathfinding.hpp include/jrpgmaker/core/camera_rig.hpp
                     include/jrpgmaker/core/map_data.hpp)

target_include_directories(jrpgmaker_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(jrpgmaker_core PUBLIC EnTT::EnTT glm::glm nlohmann_json::nlohmann_json)

target_compile_features(jrpgmaker_core PUBLIC cxx_std_20)
