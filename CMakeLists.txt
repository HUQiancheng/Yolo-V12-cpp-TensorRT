cmake_minimum_required(VERSION 3.18)

# Project declaration with C++ and CUDA support
project(YOLOv12TRT LANGUAGES CXX CUDA)

# Set C++ standard and compiler options
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Define paths with options for override
set(TENSORRT_PATH "/home/lukas/dev/TensorRT-10.8.0.43" CACHE PATH "Path to TensorRT installation")
set(OpenCV_DIR "/usr/include/opencv4" CACHE PATH "Path to OpenCV installation")

# Find required packages
find_package(OpenCV REQUIRED)
find_package(CUDA REQUIRED)

# Verify CUDA installation
if(NOT CUDA_FOUND)
    message(FATAL_ERROR "CUDA not found. Please install CUDA Toolkit.")
endif()

# Define source files
set(SOURCES
    main.cpp
    src/YOLOv12.cpp
    src/preprocess.cu
    src/videocap.cpp
)

# Create executable
add_executable(${PROJECT_NAME} ${SOURCES})

# Define compile definitions
target_compile_definitions(${PROJECT_NAME} PRIVATE
    API_EXPORTS
)

# Set include directories
target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${TENSORRT_PATH}/include
    ${CUDA_INCLUDE_DIRS}
    ${OpenCV_INCLUDE_DIRS}
)

# Set TensorRT libraries (Linux .so files)
set(TENSORRT_LIBS
    "${TENSORRT_PATH}/lib/libnvinfer.so"
    "${TENSORRT_PATH}/lib/libnvonnxparser.so"
    "${TENSORRT_PATH}/lib/libnvinfer_plugin.so"
    "${TENSORRT_PATH}/lib/libnvinfer_builder_resource.so.10.8.0"
)

# Link libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    ${OpenCV_LIBS}
    ${CUDA_LIBRARIES}
    ${TENSORRT_LIBS}
)

# CUDA specific settings
set_target_properties(${PROJECT_NAME} PROPERTIES
    CUDA_SEPARABLE_COMPILATION ON
)

# Set CUDA architectures based on your GPU
# Uncomment and modify based on your GPU architecture
# set(CMAKE_CUDA_ARCHITECTURES 75)  # For RTX 20xx series
set(CMAKE_CUDA_ARCHITECTURES 86)  # For RTX 30xx series
# set(CMAKE_CUDA_ARCHITECTURES 89)  # For RTX 40xx series

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Add TensorRT runtime path to RPATH
set_target_properties(${PROJECT_NAME} PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "${TENSORRT_PATH}/lib"
)