add_library(jrpgmaker_assetimport STATIC src/asset_import.cpp)
add_library(jrpgmaker::assetimport ALIAS jrpgmaker_assetimport)

target_sources(
  jrpgmaker_assetimport
  PUBLIC FILE_SET HEADERS BASE_DIRS include
                FILES include/jrpgmaker/assetimport/asset_import.hpp)

target_include_directories(jrpgmaker_assetimport PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# cgltf is header-only (CGLTF_IMPLEMENTATION in asset_import.cpp); vcpkg
# provides no CMake config, so resolve its include dir explicitly.
find_path(CGLTF_INCLUDE_DIRS NAMES cgltf.h REQUIRED)
find_package(glm REQUIRED CONFIG)
target_include_directories(jrpgmaker_assetimport PUBLIC ${CGLTF_INCLUDE_DIRS})

# cgltf is a vendored C header that legitimately uses fopen/strcpy; the
# deprecation warnings (C4996) would otherwise be promoted to errors by the
# repo-wide /WX. This target is the single TU that instantiates the
# implementation, so disabling the CRT security warning here is confined.
if(MSVC)
  target_compile_options(jrpgmaker_assetimport PRIVATE /D_CRT_SECURE_NO_WARNINGS)
endif()

target_link_libraries(jrpgmaker_assetimport PUBLIC jrpgmaker::core glm::glm)

target_compile_features(jrpgmaker_assetimport PUBLIC cxx_std_20)