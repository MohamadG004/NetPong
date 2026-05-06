cmake_minimum_required(VERSION 3.14)
project(pong VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Find SDL2 ──────────────────────────────────────────────────
# Try cmake config first, then fall back to pkg-config / manual
find_package(SDL2 QUIET CONFIG)
if(NOT SDL2_FOUND)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED sdl2)
    set(SDL2_INCLUDE_DIRS ${SDL2_INCLUDE_DIRS})
    set(SDL2_LIBRARIES    ${SDL2_LIBRARIES})
endif()

# ── Sources ────────────────────────────────────────────────────
add_executable(pong src/main.cpp)

target_include_directories(pong PRIVATE ${SDL2_INCLUDE_DIRS})
target_link_libraries(pong PRIVATE ${SDL2_LIBRARIES})

# Link extra libs on macOS (needed for SDL2)
if(APPLE)
    target_link_libraries(pong PRIVATE
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreAudio"
        "-framework CoreVideo"
        "-framework AudioToolbox"
    )
endif()

# Windows: copy SDL2.dll next to the exe automatically if found
if(WIN32)
    find_file(SDL2_DLL SDL2.dll HINTS ${SDL2_INCLUDE_DIRS}/../bin)
    if(SDL2_DLL)
        add_custom_command(TARGET pong POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${SDL2_DLL}" $<TARGET_FILE_DIR:pong>)
    endif()
endif()

# ── Optimised release flags ────────────────────────────────────
target_compile_options(pong PRIVATE
    $<$<CONFIG:Release>:-O2>
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)

message(STATUS "SDL2 include dirs: ${SDL2_INCLUDE_DIRS}")
message(STATUS "SDL2 libraries:    ${SDL2_LIBRARIES}")
