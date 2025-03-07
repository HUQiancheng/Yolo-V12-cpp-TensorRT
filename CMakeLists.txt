cmake_minimum_required(VERSION 3.18)

# Project declaration with C++ and CUDA support
project(YOLOv12TRT LANGUAGES CXX CUDA)

# Set C++ standard and compiler options
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Define paths (更新TensorRT路径为系统安装位置)
set(TENSORRT_PATH "/usr" CACHE PATH "Path to TensorRT installation")
# 如果OpenCV安装在系统默认路径，可以不显式设定OpenCV_DIR
# set(OpenCV_DIR "/usr/include/opencv4" CACHE PATH "Path to OpenCV installation")

# Find required packages
find_package(OpenCV REQUIRED)
find_package(CUDA REQUIRED)

if(NOT CUDA_FOUND)
    message(FATAL_ERROR "CUDA not found. Please install CUDA Toolkit.")
endif()

# Define source files
set(SOURCES
    main.cpp
    src/YOLOv12.cpp
    src/preprocess.cu
)

# Create executable
add_executable(${PROJECT_NAME} ${SOURCES})

# Define compile definitions
target_compile_definitions(${PROJECT_NAME} PRIVATE API_EXPORTS)

# Set include directories
target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${TENSORRT_PATH}/include
    ${CUDA_INCLUDE_DIRS}
    ${OpenCV_INCLUDE_DIRS}
)

# Set TensorRT libraries (按Orin NX平台的目录)
set(TENSORRT_LIBS
    "${TENSORRT_PATH}/lib/aarch64-linux-gnu/libnvinfer.so"
    "${TENSORRT_PATH}/lib/aarch64-linux-gnu/libnvonnxparser.so"
    "${TENSORRT_PATH}/lib/aarch64-linux-gnu/libnvinfer_plugin.so"
    # 如果不需要特定版本链接，这里可以注释掉：
    # "${TENSORRT_PATH}/lib/aarch64-linux-gnu/libnvinfer_builder_resource.so"
)

# Link libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    ${OpenCV_LIBS}
    ${CUDA_LIBRARIES}
    ${TENSORRT_LIBS}
)

# CUDA specific settings: 开启分离编译并设置正确的CUDA计算能力
set_target_properties(${PROJECT_NAME} PROPERTIES
    CUDA_SEPARABLE_COMPILATION ON
    CMAKE_CUDA_ARCHITECTURES 87   # 修改为Orin NX的计算能力，比如8.7
)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Add TensorRT runtime path to RPATH
set_target_properties(${PROJECT_NAME} PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "${TENSORRT_PATH}/lib/aarch64-linux-gnu"
)
