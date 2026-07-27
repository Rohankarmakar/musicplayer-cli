cmake_minimum_required(VERSION 3.20)
project(musicplayer_cli VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(musicplayer
    src/main.cpp
    src/music_library.cpp
    src/player.cpp
    src/terminal.cpp
)

target_include_directories(musicplayer PRIVATE include)
target_compile_options(musicplayer PRIVATE -Wall -Wextra -Wpedantic)

install(TARGETS musicplayer RUNTIME DESTINATION bin)
