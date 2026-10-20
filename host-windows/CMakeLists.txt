cmake_minimum_required(VERSION 3.20)
project(orc_host CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32)
  message(FATAL_ERROR "orc-host only builds on Windows (DXGI, SendInput, Winsock).")
endif()

# FFmpeg via vcpkg: vcpkg install ffmpeg[nvcodec]:x64-windows
find_path(AVCODEC_INCLUDE_DIR libavcodec/avcodec.h REQUIRED)
find_library(AVCODEC_LIBRARY avcodec REQUIRED)
find_library(AVUTIL_LIBRARY avutil REQUIRED)

add_executable(orc-host
  src/main.cpp
  src/capture.cpp
  src/encoder.cpp
  src/input.cpp
  src/net.cpp
)

target_include_directories(orc-host PRIVATE src ${AVCODEC_INCLUDE_DIR})
target_link_libraries(orc-host PRIVATE
  ${AVCODEC_LIBRARY} ${AVUTIL_LIBRARY}
  d3d11 dxgi ws2_32 user32)
target_compile_definitions(orc-host PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)

if(MSVC)
  target_compile_options(orc-host PRIVATE /W4 /permissive-)
endif()
