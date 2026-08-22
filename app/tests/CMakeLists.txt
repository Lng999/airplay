# app/tests — lane H test binaries.
#
# Added either by app/src/host/CMakeLists.txt (standalone host build) or by app/CMakeLists.txt
# (lane U, via add_subdirectory(tests)). Requires the target `airplay_host` to exist already.
cmake_minimum_required(VERSION 3.16)

if(NOT TARGET airplay_host)
    message(FATAL_ERROR "app/tests requires the airplay_host target (add app/src/host first)")
endif()

# ---- unit tests: pure parser + argv builder, no child process ------------------------------------
add_executable(airplay_host_tests test_line_parser.cpp)
target_link_libraries(airplay_host_tests PRIVATE airplay_host)
target_include_directories(airplay_host_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src/host)
target_compile_options(airplay_host_tests PRIVATE -Wall -Wextra)
set_target_properties(airplay_host_tests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)

# ---- live test: spawns the real uxplay.exe on the LAN for a few seconds ---------------------------
# Deliberately NOT registered with add_test(): it needs a built receiver, opens real sockets and
# advertises over mDNS. Run it by hand:
#   ./build-host/tests/airplay_host_live.exe C:/Users/pc/Desktop/airplay/build/uxplay.exe
add_executable(airplay_host_live test_host_live.cpp)
target_link_libraries(airplay_host_live PRIVATE airplay_host shell32)
target_include_directories(airplay_host_live PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src/host)
target_compile_options(airplay_host_live PRIVATE -Wall -Wextra)
set_target_properties(airplay_host_live PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)

# ---- live test: the cross-process window surgery behind embed_video ------------------------------
# Stands a gst-launch videotestsrc pipeline in for the receiver and adopts its window, so it needs
# GStreamer on disk and a GPU that can present. Also not registered with add_test():
#   ./build-app/tests/airplay_embed_live.exe [C:/msys64/ucrt64/bin/gst-launch-1.0.exe]
add_executable(airplay_embed_live
    test_embed_live.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/video_embed.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/video_window.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/device_frames.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/config_store.cpp)
target_link_libraries(airplay_embed_live PRIVATE airplay_host shell32 ole32 user32 gdi32)
target_include_directories(airplay_embed_live PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_compile_options(airplay_embed_live PRIVATE -Wall -Wextra)
target_link_options(airplay_embed_live PRIVATE -municode -static)
set_target_properties(airplay_embed_live PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)

# ---- unit tests: the device-frame table and geometry (no window, no receiver) --------------------
add_executable(airplay_frame_tests
    test_device_frames.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/device_frames.cpp)
target_link_libraries(airplay_frame_tests PRIVATE airplay_host)
target_include_directories(airplay_frame_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_compile_options(airplay_frame_tests PRIVATE -Wall -Wextra)
set_target_properties(airplay_frame_tests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)

add_test(NAME airplay_host_tests COMMAND airplay_host_tests)
add_test(NAME airplay_frame_tests COMMAND airplay_frame_tests)
