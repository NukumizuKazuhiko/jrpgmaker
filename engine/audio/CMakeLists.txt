add_library(jrpgmaker_audio src/audio.cpp)
add_library(jrpgmaker::audio ALIAS jrpgmaker_audio)
target_sources(jrpgmaker_audio PUBLIC FILE_SET HEADERS BASE_DIRS include FILES
               include/jrpgmaker/audio/audio.hpp)
target_include_directories(jrpgmaker_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(jrpgmaker_audio PUBLIC cxx_std_20)
