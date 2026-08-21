cmake_minimum_required(VERSION 3.20)
project(airplay_gui CXX)
enable_language(RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(AIRPLAY_HOST_STUB "build UI against an in-tree stub host (lane U dev only)" OFF)

add_compile_options(-Wall -Wextra)
# NOTE: _WIN32_WINNT is deliberately NOT set here. airplay_host exports it PUBLIC-ly as
# 0x0601 and a second -D on the command line makes gcc warn in every translation unit;
# src/ui/config_store.h raises it to 0x0A00 for the UI sources only (WM_DPICHANGED).
add_compile_definitions(UNICODE _UNICODE)

# The .rc includes app.manifest by name; windres needs the resource dir on its include path.
set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} -I \"${CMAKE_CURRENT_SOURCE_DIR}/res\"")

# --- host implementation ------------------------------------------------------
# Lane H owns src/host (the real child-process host) and tests. Until it lands - or when
# -DAIRPLAY_HOST_STUB=ON - the UI is linked against the in-tree stub so it can be run and
# debugged on its own.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/host/CMakeLists.txt" AND NOT AIRPLAY_HOST_STUB)
    message(STATUS "airplay: using the real host from src/host")
    add_subdirectory(src/host)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
        enable_testing()
        add_subdirectory(tests)
    endif()
else()
    message(STATUS "airplay: using the UI development stub host (src/ui/host_stub.cpp)")
    add_library(airplay_host STATIC src/ui/host_stub.cpp)
    target_include_directories(airplay_host PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
endif()

# --- GUI ----------------------------------------------------------------------
add_executable(airplay-gui WIN32
    src/ui/main.cpp
    src/ui/main_window.cpp
    src/ui/tray.cpp
    src/ui/config_store.cpp
    src/ui/audio_mute.cpp
    src/ui/single_instance.cpp
    src/ui/ui_log.cpp
    res/app.rc
)

target_include_directories(airplay-gui PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui"
)

target_link_libraries(airplay-gui PRIVATE
    airplay_host
    shell32 ole32 comctl32 user32 gdi32 advapi32 iphlpapi ws2_32
)

# WIN32 already implies -mwindows; -municode selects the wWinMain entry point and -static
# removes the MSYS2 runtime DLL dependency (the uxplay.exe child finds its own via PATH).
target_link_options(airplay-gui PRIVATE -municode -static -mwindows)
