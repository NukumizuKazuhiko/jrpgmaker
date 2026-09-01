include(FetchContent)

set(JRPGMAKER_RMLUI_SOURCE_DIR "" CACHE PATH
    "Optional local RmlUi source tree; otherwise fetch the pinned upstream revision")

set(RMLUI_SAMPLES OFF CACHE BOOL "Build RmlUi samples" FORCE)
set(RMLUI_LUA_BINDINGS OFF CACHE BOOL "Build RmlUi Lua bindings" FORCE)
set(RMLUI_LOTTIE_PLUGIN OFF CACHE BOOL "Build RmlUi Lottie plugin" FORCE)
set(RMLUI_SVG_PLUGIN OFF CACHE BOOL "Build RmlUi SVG plugin" FORCE)
set(RMLUI_HARFBUZZ_SAMPLE OFF CACHE BOOL "Build RmlUi HarfBuzz sample" FORCE)
set(RMLUI_PRECOMPILED_HEADERS OFF CACHE BOOL "Build RmlUi precompiled headers" FORCE)
set(RMLUI_COMPILER_OPTIONS OFF CACHE BOOL "Use RmlUi compiler options" FORCE)
set(RMLUI_FONT_ENGINE "freetype" CACHE STRING "RmlUi font engine" FORCE)

if(JRPGMAKER_RMLUI_SOURCE_DIR)
    if(NOT EXISTS "${JRPGMAKER_RMLUI_SOURCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
                "JRPGMAKER_RMLUI_SOURCE_DIR must point to an RmlUi source tree")
    endif()
    FetchContent_Declare(rmlui SOURCE_DIR "${JRPGMAKER_RMLUI_SOURCE_DIR}")
else()
    FetchContent_Declare(
        rmlui
        GIT_REPOSITORY https://github.com/mikke89/RmlUi.git
        GIT_TAG b7b4a06
        GIT_SHALLOW TRUE)
endif()

FetchContent_MakeAvailable(rmlui)

if(NOT TARGET RmlUi::Core)
    message(FATAL_ERROR "RmlUi Core target was not created")
endif()

if(MSVC)
    target_compile_options(rmlui_core PRIVATE /utf-8)
endif()
