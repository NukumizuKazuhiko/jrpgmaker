find_package(nlohmann_json CONFIG REQUIRED)
find_package(sol2 CONFIG REQUIRED)
find_package(Lua REQUIRED)

add_library(jrpgmaker_domain src/event_script.cpp src/flag_store.cpp src/event_runner.cpp
                            src/event_lint.cpp src/flag_trigger.cpp src/lua_binding.cpp)
add_library(jrpgmaker::domain ALIAS jrpgmaker_domain)

target_sources(
  jrpgmaker_domain
  PUBLIC FILE_SET HEADERS BASE_DIRS include
                FILES include/jrpgmaker/domain/event_script.hpp
                     include/jrpgmaker/domain/flag_store.hpp
                     include/jrpgmaker/domain/event_runner.hpp
                     include/jrpgmaker/domain/event_lint.hpp
                     include/jrpgmaker/domain/flag_trigger.hpp
                     include/jrpgmaker/domain/lua_binding.hpp)

target_include_directories(jrpgmaker_domain PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(jrpgmaker_domain PUBLIC jrpgmaker::core nlohmann_json::nlohmann_json
                                                sol2::sol2 ${LUA_LIBRARIES})

target_compile_features(jrpgmaker_domain PUBLIC cxx_std_20)