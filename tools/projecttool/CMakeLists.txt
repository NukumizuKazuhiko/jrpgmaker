add_executable(jrpgmaker_projecttool main.cpp)
target_link_libraries(jrpgmaker_projecttool PRIVATE jrpgmaker::plugin jrpgmaker::domain
                                                    jrpgmaker::core)
