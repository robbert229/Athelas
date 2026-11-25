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

add_executable(xbe_smoke ${XBE_RUNTIME_SOURCES} ${CMAKE_CURRENT_LIST_DIR}/smoke.cpp ${GEN}/x86_func_mapping.cpp
               ${XBE_IMGUI_SOURCES})
target_include_directories(xbe_smoke PRIVATE ${XBE_SDK_DIR}/runtime ${GEN})
xbe_host_runtime(xbe_smoke)
target_compile_definitions(xbe_smoke PRIVATE XBE_SMOKE=1)
