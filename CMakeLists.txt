
cmake_minimum_required(VERSION 2.8)
project(libed2k)
set(libed2k_version 0.0.1)
set(out_dir "${PROJECT_SOURCE_DIR}/bin")


####### Build customization

# Override from command line "CMake -D<OPTION>=TRUE/FALSE/0/1/ON/OFF"
option (PRODUCTION  "Build project in production mode." FALSE)
option (BUILD_TESTS "Build unit tests." TRUE)
option (BUILD_TOOLS "Build tools." TRUE)
option (BUILD_SHARED "Build shared library." TRUE)
option (DISABLE_DHT "Enable KAD support" FALSE)
option (UPNP_VERBOSE "Verbose output for UPnP" FALSE)
option (DHT_VERBOSE "Verbose output for DHT" FALSE)

set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS TRUE)
include(cmake/Environment.cmake)
include(cmake/Linux.cmake)
include(cmake/Windows.cmake)
include(cmake/Android.cmake)
include(cmake/Darwin.cmake)
include(cmake/Targets.cmake)
