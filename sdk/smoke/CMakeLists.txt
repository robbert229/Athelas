# The SDK runtime linked without a game: the whole host side of a port (window, renderer, sound, kernel, XDK) around a
# stand in for the recompiled code (cmake/XbeStandIn.cmake). Checks the runtime builds and starts on a platform before
# any game can be recompiled there (notes/mac-linux-plan.md, M1). README.md says how to build and run it.
cmake_minimum_required(VERSION 3.25)
project(xbe_smoke LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_LIST_DIR}/../cmake/XbeRecomp.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/XbeStandIn.cmake)

# every XDK symbol the SDK's signatures know, none of them in this "image"
file(GLOB signatures CONFIGURE_DEPENDS ${XBE_SDK_DIR}/xdk/*/signatures.toml)
set(GEN ${CMAKE_CURRENT_BINARY_DIR}/gen)
xbe_stand_in(${GEN} TOML ${signatures})

add_executable(xbe_smoke ${XBE_RUNTIME_SOURCES} ${CMAKE_CURRENT_LIST_DIR}/smoke.cpp ${CMAKE_CURRENT_LIST_DIR}/smoke_av.cpp ${GEN}/x86_func_mapping.cpp
               ${XBE_IMGUI_SOURCES})
target_include_directories(xbe_smoke PRIVATE ${XBE_SDK_DIR}/runtime ${GEN})
xbe_host_runtime(xbe_smoke)
target_compile_definitions(xbe_smoke PRIVATE XBE_SMOKE=1)

# XBE_PORT=<a port's project folder> (ports/rotk-xbox/rotkxbox): xbe_port_check compiles that port's own sources
# (src/*/*.cpp) and links them with the runtime and a stand in made from its config/game.toml, without its xbe. It
# catches what a game's generated code would hide: a port file that doesn't compile here, a symbol nothing provides.
set(XBE_PORT "" CACHE PATH "A port's project folder for xbe_port_check")
if(XBE_PORT)
    get_filename_component(port ${XBE_PORT} ABSOLUTE)
    file(GLOB port_sources CONFIGURE_DEPENDS ${port}/src/*/*.cpp)
    set(PORT_GEN ${CMAKE_CURRENT_BINARY_DIR}/port_gen)
    xbe_stand_in(${PORT_GEN} TOML ${port}/config/game.toml SCAN ${port_sources})
    file(CONFIGURE OUTPUT ${PORT_GEN}/xbe_hash.cpp CONTENT "extern const char kImageSha1[] = \"\";\n")
    add_executable(xbe_port_check ${XBE_RUNTIME_SOURCES} ${port_sources} ${PORT_GEN}/x86_func_mapping.cpp
                   ${XBE_IMGUI_SOURCES} ${PORT_GEN}/xbe_hash.cpp)
    target_include_directories(xbe_port_check PRIVATE ${port}/src ${XBE_SDK_DIR}/runtime ${PORT_GEN})
    xbe_host_runtime(xbe_port_check)
    # as the ports build themselves (ROTK: lockstep on, drawing on the title's thread)
    target_compile_definitions(xbe_port_check PRIVATE XBE_LOCKSTEP=1 XBE_RENDER_THREAD=0)
    message(STATUS "xbe_port_check: ${port}")
endif()
