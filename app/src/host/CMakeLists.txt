# app/src/host — the child-process host library (lane H).
#
# Two ways to configure this file:
#   1. from app/CMakeLists.txt via add_subdirectory(src/host)   -> only the airplay_host target
#   2. standalone, for host-only development and CI:
#        cmake -S app/src/host -B build-host -G Ninja
#        cmake --build build-host
#        ./build-host/tests/airplay_host_tests.exe
#      In that mode this file also pulls in app/tests, which lane U adds separately from app/.
cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(airplay_host CXX)
    set(AIRPLAY_HOST_STANDALONE TRUE)
endif()

add_library(airplay_host STATIC
    uxplay_host.cpp
    line_parser.cpp
    child_env.cpp
)

target_include_directories(airplay_host
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(airplay_host PUBLIC cxx_std_17)
set_target_properties(airplay_host PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)

target_compile_definitions(airplay_host PUBLIC
    UNICODE
    _UNICODE
    _WIN32_WINNT=0x0601   # AttachConsole / GenerateConsoleCtrlEvent / GetConsoleWindow
)

target_compile_options(airplay_host PRIVATE -Wall -Wextra)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

target_link_libraries(airplay_host PUBLIC user32 kernel32 Threads::Threads)

if(AIRPLAY_HOST_STANDALONE)
    enable_testing()
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../tests tests)
endif()
