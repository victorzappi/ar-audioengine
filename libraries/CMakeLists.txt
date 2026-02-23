# CMakeLists.txt in the libraries directory

add_library(libraries INTERFACE)

if(ADD_LIBSNDFILE)
    add_library(AudioFile STATIC AudioFile/AudioFileUtilities.cpp)
    target_link_libraries(AudioFile PRIVATE dependencies)
    target_include_directories(AudioFile PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/AudioFile)
    target_link_libraries(libraries INTERFACE AudioFile)
endif()