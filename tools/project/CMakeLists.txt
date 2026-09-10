add_library(jrpgmaker_project src/workspace.cpp src/plugin_registry.cpp
                              src/transient_data_adapter.cpp)
add_library(jrpgmaker::project ALIAS jrpgmaker_project)

target_sources(
  jrpgmaker_project
  PUBLIC FILE_SET HEADERS BASE_DIRS include FILES include/jrpgmaker/project/workspace.hpp
                                                  include/jrpgmaker/project/plugin_registry.hpp
                                                  include/jrpgmaker/project/transient_data_adapter.hpp)
target_include_directories(jrpgmaker_project PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(jrpgmaker_project PUBLIC jrpgmaker::core jrpgmaker::domain jrpgmaker::plugin)
target_compile_features(jrpgmaker_project PUBLIC cxx_std_20)
