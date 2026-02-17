# CMakeLists for ar_audioengine
# build for Yocto RB3 Gen2

# From audio engine root:

# _Configure in new 'build' dir:
# cmake -B build -DPROJECT_PATH=projects/my_synth

#_BUild [bin in build]
# cmake --buld build

# _Clean 
# cmake --build build --target clean
# or more drastically
# rm -rf ./build

#_To switch to default project after config, unset var
# cmake -B build -UPROJECT_PATH


cmake_minimum_required(VERSION 3.10)
project(ar_audioengine LANGUAGES CXX)

message(STATUS "=== Configuring ar_audioengine ===")

#-------------------------------------------------------------------------
# sources
#-------------------------------------------------------------------------
set(CORE_SRCS
    core/main.cpp
    core/pcm_utils.cpp
    core/hw_mixer.cpp
    core/agm_mixer.cpp
)
# pass project path as either relative to source dir or absolute
if(PROJECT_PATH)
    # Resolve relative paths against the source directory
    if(NOT IS_ABSOLUTE "${PROJECT_PATH}")
        set(PROJECT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/${PROJECT_PATH}")
    endif()

    if(EXISTS "${PROJECT_PATH}/render.cpp")
        file(GLOB PROJECT_SRCS "${PROJECT_PATH}/*.cpp")
        list(APPEND CORE_SRCS ${PROJECT_SRCS})
        message(STATUS "Project path: ${PROJECT_PATH}")
    else()
        message(FATAL_ERROR "No render.cpp found in ${PROJECT_PATH}")
    endif()
else()
    # if no project path is passed, we default to simpler sine renderer
    list(APPEND CORE_SRCS core/default_render.cpp)
    message(STATUS "Project: default (no PROJECT_PATH set)")
endif()
#-------------------------------------------------------------------------


add_executable(ar_audioengine ${CORE_SRCS})


#-------------------------------------------------------------------------
# includes
#-------------------------------------------------------------------------
target_include_directories(ar_audioengine PRIVATE
    ${CMAKE_SYSROOT}/usr/include/acdbdata
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
if(PROJECT_PATH)
    target_include_directories(ar_audioengine PRIVATE ${PROJECT_PATH})
endif()
#-------------------------------------------------------------------------


#-------------------------------------------------------------------------
# definitions
#-------------------------------------------------------------------------
if(PROJECT_PATH)
    # Strip trailing slash to avoid empty result from get_filename_component
    string(REGEX REPLACE "/$" "" PROJECT_PATH_CLEAN "${PROJECT_PATH}")
    # Extract just the folder name (e.g., "sine" from "projects/sine")
    get_filename_component(PROJECT_NAME_STR "${PROJECT_PATH_CLEAN}" NAME)
    # Pass project name as compile definition so main.cpp can print it at run-time
    target_compile_definitions(ar_audioengine PRIVATE PROJECT_NAME="${PROJECT_NAME_STR}")
else()
    target_compile_definitions(ar_audioengine PRIVATE PROJECT_NAME="default")
endif()
#-------------------------------------------------------------------------


#-------------------------------------------------------------------------
# options and libs
#-------------------------------------------------------------------------
target_compile_options(ar_audioengine PRIVATE -Wall -fdiagnostics-color=always)
target_link_libraries(ar_audioengine PRIVATE tinyalsa expat pthread)
#-------------------------------------------------------------------------
