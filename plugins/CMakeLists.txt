add_subdirectory(sample_unlit)
add_subdirectory(sample_style)
add_subdirectory(sample_instant)
add_subdirectory(sample_turn_based)

add_library(jrpgmaker_plugins src/register.cpp)
add_library(jrpgmaker::plugins ALIAS jrpgmaker_plugins)
target_sources(jrpgmaker_plugins PUBLIC FILE_SET HEADERS BASE_DIRS include FILES
               include/jrpgmaker/plugins/register.hpp)
target_include_directories(jrpgmaker_plugins PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(jrpgmaker_plugins PUBLIC jrpgmaker::plugin jrpgmaker::sample_unlit
                                               jrpgmaker::sample_style jrpgmaker::sample_instant
                                               jrpgmaker::sample_turn_based)
target_compile_features(jrpgmaker_plugins PUBLIC cxx_std_20)
