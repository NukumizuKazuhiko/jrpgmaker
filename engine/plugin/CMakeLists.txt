find_package(nlohmann_json CONFIG REQUIRED)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_library(jrpgmaker_plugin)
add_library(jrpgmaker::plugin ALIAS jrpgmaker_plugin)
set_target_properties(jrpgmaker_plugin PROPERTIES EXPORT_NAME plugin)

target_sources(
  jrpgmaker_plugin
  PRIVATE src/plugin.cpp src/battle.cpp
  PUBLIC FILE_SET HEADERS BASE_DIRS include FILES include/jrpgmaker/plugin/plugin.hpp
                                               include/jrpgmaker/plugin/battle.hpp)

target_include_directories(
  jrpgmaker_plugin
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(jrpgmaker_plugin PUBLIC nlohmann_json::nlohmann_json)
target_compile_features(jrpgmaker_plugin PUBLIC cxx_std_20)

install(
  TARGETS jrpgmaker_plugin
  EXPORT jrpgmakerPluginTargets
  FILE_SET HEADERS DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(
  EXPORT jrpgmakerPluginTargets
  FILE jrpgmakerPluginTargets.cmake
  NAMESPACE jrpgmaker::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/jrpgmaker)

configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/jrpgmakerConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/jrpgmakerConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/jrpgmaker
  NO_CHECK_REQUIRED_COMPONENTS_MACRO)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/jrpgmakerConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)
install(
  FILES ${CMAKE_CURRENT_BINARY_DIR}/jrpgmakerConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/jrpgmakerConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/jrpgmaker)
