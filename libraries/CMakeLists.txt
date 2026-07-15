# CMakeLists.txt in the libraries directory

add_library(libraries INTERFACE)

if(ADD_LIBSNDFILE)
    add_library(AudioFile STATIC AudioFile/AudioFileUtilities.cpp)
    target_link_libraries(AudioFile PRIVATE dependencies)
    target_include_directories(AudioFile PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/AudioFile)
    target_link_libraries(libraries INTERFACE AudioFile)
endif()

if(ADD_ONNXRUNTIME)
    add_library(OrtModel STATIC OrtModel/OrtModel.cpp)
    target_link_libraries(OrtModel PRIVATE dependencies)
    target_include_directories(OrtModel PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/OrtModel)
    target_link_libraries(libraries INTERFACE OrtModel)
endif()

if(ADD_QNN)
    add_library(QnnModel STATIC
        QNN/QnnModel.cpp
        QNN/TensorTools.cpp
    )
    target_include_directories(QnnModel PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/QNN)
    target_link_libraries(QnnModel PUBLIC dependencies)
    target_link_libraries(libraries INTERFACE QnnModel)
endif()