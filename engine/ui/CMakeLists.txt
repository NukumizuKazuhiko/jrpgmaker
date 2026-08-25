add_library(jrpgmaker_ui src/text.cpp)
add_library(jrpgmaker::ui ALIAS jrpgmaker_ui)

target_sources(
  jrpgmaker_ui
  PUBLIC FILE_SET HEADERS BASE_DIRS include
                FILES include/jrpgmaker/ui/text.hpp)

target_include_directories(jrpgmaker_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

find_package(Freetype REQUIRED)
find_package(harfbuzz CONFIG REQUIRED)
target_link_libraries(jrpgmaker_ui PUBLIC jrpgmaker::core freetype harfbuzz::harfbuzz)

# MSVC: source may contain UTF-8 in comments (kinsoku table); /utf-8 stops
# C4819 (would be promoted to an error by /WX).
if(MSVC)
  target_compile_options(jrpgmaker_ui PRIVATE /utf-8)
endif()

target_compile_features(jrpgmaker_ui PUBLIC cxx_std_20)