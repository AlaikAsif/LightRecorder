cmake_minimum_required(VERSION 3.10)

project(UltraLightGameScreenRecorder)

set(CMAKE_CXX_STANDARD 17)

# Include directories
include_directories(
    app
    core/capture
    core/encode
    core/io
    core/audio
    core/util
    libs
)

# Source files
set(APP_SOURCES
    app/main.cpp
    app/auth_client.cpp
)

set(CORE_SOURCES
    core/capture/gdi_capture.cpp
    core/capture/hook_present.cpp
    core/encode/mjpeg.cpp
    core/encode/delta_tiles.cpp
    core/io/avi_mux.cpp
    core/io/writer.cpp
    core/audio/wasapi_capture.cpp
    core/core.cpp
)

# Add executable
add_executable(UltraLightGameScreenRecorder ${APP_SOURCES} ${CORE_SOURCES})

# Link libraries if needed
# target_link_libraries(UltraLightGameScreenRecorder <libraries>)