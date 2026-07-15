# CMakeLists.txt in dependencies directory
add_library(dependencies INTERFACE)

# all dependencies' includes are exposed as an INTERFACE target, so that they can be used by core and libraries targets too
message("")
message(STATUS "Configuring dependencies:")


set(VARIABLE_NAMES 
    "ADD_LIBSNDFILE"
    "ADD_RTNEURAL"
    "ADD_ONNXRUNTIME"
    "ADD_NAM"
    "ADD_QNN"
)

set(SEARCH_STRINGS
    "sndfile.h\;AudioFile.h"
    "RTNeural/RTNeural.h\;rtneural"
    "OrtModel.h\;OrtModel"
    "NAM/get_dsp.h\;NeuralAmpModelerCore"
    "QnnModel.h\;QnnTypeMacros.hpp\;Qnn"
)

foreach(VAR_NAME IN LISTS VARIABLE_NAMES)
    set(${VAR_NAME} FALSE CACHE BOOL "Initially, ${VAR_NAME} is set to FALSE" FORCE)
endforeach()

# the optional dependencies are built and linked only if the user project uses thme
if(PROJECT_PATH)
    include("${CMAKE_CURRENT_SOURCE_DIR}/check_dependencies_inclusion.cmake")
endif()


#-------------------------------------------------------------------------
# LIBSNDFILE
#-------------------------------------------------------------------------
if (ADD_LIBSNDFILE)
    # Configuration for libsndfile's provided CMakeLists.txt
    message("")
    message(STATUS "Configuring Libsndfile...")
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
#-------------------------------------------------------------------------


#-------------------------------------------------------------------------
# RTNEURAL
#-------------------------------------------------------------------------
if (ADD_RTNEURAL)
    # Configuration for libsndfile's provided CMakeLists.txt
    message("")
    message(STATUS "Configuring RTNeural...")
    # Set the CPM source cache to a custom directory within the build directory and create the dir
    # needed because rtneural uses the CMake Package Manager which needs to be explicitly 
    # re-directed to the custom build dir
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
#-------------------------------------------------------------------------

#-------------------------------------------------------------------------
# ONNXRUNTIME
#-------------------------------------------------------------------------
if (ADD_ONNXRUNTIME)
    # Pre-built shared library
    message("")
    message(STATUS "Configuring ONNX Runtime...")
    set(ORT_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/onnxruntime/ort_output/lib")
    target_include_directories(dependencies INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/onnxruntime/ort_output/include
    )
    target_link_libraries(dependencies INTERFACE
        -L${ORT_LIB_DIR} -l:libonnxruntime.so.1.22.0
    )
endif()
#-------------------------------------------------------------------------

#-------------------------------------------------------------------------
# NAM
#-------------------------------------------------------------------------
if(ADD_NAM)
    # NeuralAmpModelerCore does not provide its own library CMake target,
    # so we build it as a static library from source here
    message(STATUS "\tConfiguring NeuralAmpModelerCore...")
    set(NAM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/NeuralAmpModelerCore)
    file(GLOB_RECURSE NAM_SOURCES ${NAM_DIR}/NAM/*.cpp)
    file(GLOB_RECURSE NAM_HEADERS ${NAM_DIR}/NAM/*.h)
    add_library(NAM STATIC ${NAM_SOURCES} ${NAM_HEADERS})
    target_include_directories(NAM PUBLIC
        ${NAM_DIR}
        ${NAM_DIR}/Dependencies/eigen
        ${NAM_DIR}/Dependencies/nlohmann
    )
    target_include_directories(dependencies INTERFACE
        ${NAM_DIR}
        ${NAM_DIR}/Dependencies/eigen
        ${NAM_DIR}/Dependencies/nlohmann
    )
    # NeuralAmpModelerCore uses static self-registration to register model parsers
    # (WaveNet, LSTM, ConvNet) at startup. Without --whole-archive, the linker
    # strips these registration objects because nothing directly references them,
    # causing "No config parser registered for architecture" at runtime.
    # --no-whole-archive restores default linking for all libraries that follow,
    # so only NeuralAmpModelerCore is affected.
    target_link_libraries(dependencies INTERFACE
            -Wl,--whole-archive NAM -Wl,--no-whole-archive)
endif()
#-------------------------------------------------------------------------


#-------------------------------------------------------------------------
# QNN
#-------------------------------------------------------------------------
if (ADD_QNN)
    message("")
    message(STATUS "Configuring QNN...")

    set(QNN_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/QNN/include")

    if(NOT EXISTS "${QNN_INCLUDE_DIR}/QnnInterface.h")
        message(FATAL_ERROR "QNN headers not found at ${QNN_INCLUDE_DIR}")
    endif()

    # Build QNN static library from source files
    file(GLOB_RECURSE QNN_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/QNN/tools/*.cpp")
    list(FILTER QNN_SOURCES EXCLUDE REGEX "/DspLog/")
    list(FILTER QNN_SOURCES EXCLUDE REGEX "/PAL/src/windows")
    add_library(QNN STATIC ${QNN_SOURCES})

    target_link_libraries(QNN PRIVATE dl)

    # Add both root QNN headers and one-level backend folders (GPU, HTP, etc.)
    # so consumers can include headers with or without folder prefixes.
    set(QNN_INCLUDE_DIRS "${QNN_INCLUDE_DIR}")
    file(GLOB QNN_INCLUDE_CHILD_PATHS LIST_DIRECTORIES TRUE "${QNN_INCLUDE_DIR}/*")
    foreach(QNN_INCLUDE_CHILD_PATH IN LISTS QNN_INCLUDE_CHILD_PATHS)
        if(IS_DIRECTORY "${QNN_INCLUDE_CHILD_PATH}")
            list(APPEND QNN_INCLUDE_DIRS "${QNN_INCLUDE_CHILD_PATH}")
        endif()
    endforeach()

    # Dynamically add all include directories from libraries
    set(QNN_LIBRARIES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/QNN/tools")
    list(APPEND QNN_INCLUDE_DIRS "${QNN_LIBRARIES_DIR}")
    file(GLOB QNN_LIB_PATHS LIST_DIRECTORIES TRUE "${QNN_LIBRARIES_DIR}/*")
    foreach(QNN_LIB_PATH IN LISTS QNN_LIB_PATHS)
        if(IS_DIRECTORY "${QNN_LIB_PATH}")
            list(APPEND QNN_INCLUDE_DIRS "${QNN_LIB_PATH}")
            # Also add include subdirectories if they exist
            if(EXISTS "${QNN_LIB_PATH}/include")
                list(APPEND QNN_INCLUDE_DIRS "${QNN_LIB_PATH}/include")
            endif()
        endif()
    endforeach()

    target_include_directories(QNN
        PUBLIC
            ${QNN_INCLUDE_DIRS}
    )

    target_link_libraries(dependencies INTERFACE QNN)
endif()
#-------------------------------------------------------------------------