# CMakeLists.txt at the dependencies directory
cmake_minimum_required(VERSION 3.10)

add_library(dependencies INTERFACE)

# all dependencies' includes are exposed as an INTERFACE target, so that they can be used by core and libraries targets too

message(STATUS "Configuring dependencies:")

set(VARIABLE_NAMES 
    "ADD_LIBSNDFILE"
    "ADD_RTNEURAL" 
)

set(SEARCH_STRINGS
    "RTNeural/RTNeural.h\;rtneural" 
    "sndfile.h"
)

foreach(VAR_NAME IN LISTS VARIABLE_NAMES)
    set(${VAR_NAME} FALSE CACHE BOOL "Initially, ${VAR_NAME} is set to FALSE" FORCE)
endforeach()

include("${CMAKE_CURRENT_SOURCE_DIR}/check_dependencies_inclusion.cmake")

message(STATUS "LIBSNDFILE: ${ADD_LIBSNDFILE}")
message(STATUS "RTNeural: ${ADD_RTNEURAL}")

#Linking LIBSNDFILE
if (ADD_LIBSNDFILE)
    message(STATUS "\tConfiguring Libsndfile...")
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "Build tests" FORCE)
    set(BUILD_PROGRAMS OFF CACHE BOOL "Build programs" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "Build examples" FORCE)
    set(ENABLE_CPACK OFF CACHE BOOL "Enable CPack" FORCE)
    set(ENABLE_PACKAGE_CONFIG OFF CACHE BOOL "Generate and install package config file" FORCE)
    set(INSTALL_PKGCONFIG_MODULE OFF CACHE BOOL "Generate and install pkg-config module" FORCE)
    add_subdirectory(libsndfile)
    target_include_directories(dependencies INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/libsndfile/include)
    target_link_libraries(dependencies INTERFACE sndfile)
endif()

#Linking RTNeural
if (ADD_RTNEURAL)
    message(STATUS "\tConfiguring RTNeural...")
    set(CPM_SOURCE_CACHE "${CMAKE_BINARY_DIR}/cpm_cache")
    file(MAKE_DIRECTORY ${CPM_SOURCE_CACHE})
    set(RTNEURAL_EIGEN ON CACHE BOOL "Use RTNeural with this backend" FORCE)
    set(RTNEURAL_XSIMD OFF CACHE BOOL "Use RTNeural with this backend" FORCE)
    set(RTNEURAL_STL OFF CACHE BOOL "Use RTNeural with this backend" FORCE)
    add_subdirectory(RTNeural)
    # rtneural does not need to explicitly add includes with:
    # target_include_directories()
    # because its CMakelists.txt file propagates them to any target linking against the lib with:
    target_link_libraries(dependencies INTERFACE RTNeural)
endif()