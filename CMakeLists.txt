cmake_minimum_required(VERSION 3.10)

project(UltraLightGameScreenRecorder)

set(CMAKE_CXX_STANDARD 17)

option(USE_TURBOJPEG "Use libjpeg-turbo (TurboJPEG API) if found" ON)

# If a vcpkg toolchain file is provided via CMAKE_TOOLCHAIN_FILE, vcpkg will manage dependencies.
if(DEFINED CMAKE_TOOLCHAIN_FILE)
    message(STATUS "CMake toolchain detected: ${CMAKE_TOOLCHAIN_FILE}")
    # vcpkg will provide libjpeg-turbo if installed
endif()

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

# Try to find libjpeg-turbo (turbojpeg)
set(EXTRA_LIBS "")
if(USE_TURBOJPEG)
    # Prefer turbojpeg provided by vcpkg/CMAKE environment; otherwise fallback to system paths
    find_path(TURBOJPEG_INCLUDE_DIR NAMES turbojpeg.h)
    find_library(TURBOJPEG_LIBRARY NAMES turbojpeg)
    if(TURBOJPEG_INCLUDE_DIR AND TURBOJPEG_LIBRARY)
        message(STATUS "Found libjpeg-turbo include: ${TURBOJPEG_INCLUDE_DIR}")
        message(STATUS "Found libjpeg-turbo library: ${TURBOJPEG_LIBRARY}")
        add_definitions(-DHAVE_TURBOJPEG)
        include_directories(${TURBOJPEG_INCLUDE_DIR})
        list(APPEND EXTRA_LIBS ${TURBOJPEG_LIBRARY})
    else()
        message(WARNING "libjpeg-turbo not found. Falling back to GDI+ encoder. To install via vcpkg: git clone https://github.com/microsoft/vcpkg.git && .\\vcpkg\\bootstrap-vcpkg.bat && .\\vcpkg\\vcpkg.exe install libjpeg-turbo")
    endif()
endif()

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

# Link libraries
# Always link Gdiplus on Windows for fallback encoder
if (WIN32)
    list(APPEND EXTRA_LIBS gdiplus)
endif()

if(EXTRA_LIBS)
    target_link_libraries(UltraLightGameScreenRecorder ${EXTRA_LIBS})
endif()