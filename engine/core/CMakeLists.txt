find_package(EnTT REQUIRED CONFIG)
find_package(glm REQUIRED CONFIG)

add_library(jrpgmaker_core)
add_library(jrpgmaker::core ALIAS jrpgmaker_core)

target_sources(
  jrpgmaker_core
  PRIVATE src/version.cpp src/stage.cpp src/scene.cpp src/asset.cpp
  PUBLIC FILE_SET HEADERS BASE_DIRS include
                FILES include/jrpgmaker/core/version.hpp include/jrpgmaker/core/stage.hpp
                     include/jrpgmaker/core/mesh.hpp include/jrpgmaker/core/scene.hpp
                     include/jrpgmaker/core/asset.hpp)

target_include_directories(jrpgmaker_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(jrpgmaker_core PUBLIC EnTT::EnTT glm::glm)

target_compile_features(jrpgmaker_core PUBLIC cxx_std_20)
