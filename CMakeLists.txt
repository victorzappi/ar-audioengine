# CMakeLists for ar_audioengine

# Build on board
# From audio engine root:
#
# _Configure in new 'build' dir:
# cmake -B build -DPROJECT_PATH=projects/my_synth
# 
# __Optional configurations (board specific):
#   -DMIXER_PATHS=/path/to/on_board/mixer_path.xml
#   -DMIXER_PATHS=/path/to/on_board/backend_config.xml
#
# __for debug build (-O0 -g):
#   -DCMAKE_BUILD_TYPE=Debug
#
#_Build [bin in build]
# cmake --buld build
#
# _Clean 
# cmake --build build --target clean
# or more drastically
# rm -rf ./build
#
#_To switch to default project after config, unset var
# cmake -B build -UPROJECT_PATH

# Cross-compilation via SDK/toolchain
#
#_source SDK environment, for example:
# source /opt/qcom-wayland/1.6/environment-setup-armv8-2a-qcom-linux
#
#_pass the cmake environment exported by the SDK to the usual config:
# cmake -B build -DCMAKE_TOOLCHAIN_FILE="$OE_CMAKE_TOOLCHAIN_FILE" -DPROJECT_PATH=projects/my_synth
#
# rest is the same as on board build


cmake_minimum_required(VERSION 3.10)


message(STATUS "=== Configuring ar_audioengine ===")


#-------------------------------------------------------------------------
# build configuration/type
#-------------------------------------------------------------------------
# Default to Release if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
#-------------------------------------------------------------------------


project(ar_audioengine LANGUAGES CXX)


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
# definitions (can be overridden at configure time)
#-------------------------------------------------------------------------
# project to build
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

# board-specific xml file paths 
if(NOT MIXER_PATHS)
    set(MIXER_PATHS "/etc/mixer_paths_qcs6490_rb3gen2.xml")
endif()
if(NOT BACKEND_CONF_FILE)
    set(BACKEND_CONF_FILE "/etc/backend_conf.xml")
endif()
target_compile_definitions(ar_audioengine PRIVATE
    MIXER_PATHS="${MIXER_PATHS}"
    BACKEND_CONF_FILE="${BACKEND_CONF_FILE}"
)
message(STATUS "Mixer paths: ${MIXER_PATHS}")
message(STATUS "Backend conf: ${BACKEND_CONF_FILE}")
#-------------------------------------------------------------------------


#-------------------------------------------------------------------------
# options and libs
#-------------------------------------------------------------------------
target_compile_options(ar_audioengine PRIVATE -Wall -fdiagnostics-color=always)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(ar_audioengine PRIVATE -O0 -g)
else()
    target_compile_options(ar_audioengine PRIVATE -O2)
endif()

target_link_libraries(ar_audioengine PRIVATE tinyalsa expat pthread)
#-------------------------------------------------------------------------
