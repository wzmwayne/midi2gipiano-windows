cmake_minimum_required(VERSION 3.10)
project(midi2gipiano C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mwindows")

add_executable(midi2gipiano
    src/main.c
    src/gui.c
    src/midi_parser.c
    src/note_mapper.c
    src/key_input.c
    src/playback.c
    resources/resource.rc
)

target_link_libraries(midi2gipiano PRIVATE winmm)
target_sources(midi2gipiano PRIVATE resources/resource.rc)
