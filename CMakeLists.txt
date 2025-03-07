cmake_minimum_required(VERSION 3.18)

# 项目声明，支持C++和CUDA
project(YOLOv12TRT LANGUAGES CXX CUDA)

# 设置C++标准和编译器选项
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# 根据调试结果设置正确的路径
set(TENSORRT_PATH "/usr/lib/aarch64-linux-gnu" CACHE PATH "Path to TensorRT libraries")
set(TENSORRT_INCLUDE_PATH "/usr/include/aarch64-linux-gnu" CACHE PATH "Path to TensorRT headers")
set(OpenCV_DIR "/usr/lib/cmake/opencv4" CACHE PATH "Path to OpenCV CMake files")

# 寻找必要的包
find_package(OpenCV REQUIRED)
find_package(CUDA REQUIRED)

# 验证CUDA安装
if(NOT CUDA_FOUND)
    message(FATAL_ERROR "CUDA not found. Please check JetPack installation.")
endif()

# 定义源文件
set(SOURCES
    main.cpp
    src/YOLOv12.cpp
    src/preprocess.cu
)

# 创建可执行文件
add_executable(${PROJECT_NAME} ${SOURCES})

# 定义编译宏
target_compile_definitions(${PROJECT_NAME} PRIVATE
    API_EXPORTS
)

# 设置包含目录
target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${TENSORRT_INCLUDE_PATH}
    ${CUDA_INCLUDE_DIRS}
    ${OpenCV_INCLUDE_DIRS}
)

# 设置TensorRT库（移除不存在的libnvparsers.so）
set(TENSORRT_LIBS
    "${TENSORRT_PATH}/libnvinfer.so"
    "${TENSORRT_PATH}/libnvonnxparser.so"
    "${TENSORRT_PATH}/libnvinfer_plugin.so" 
)

# 检查builder_resource库是否存在
if(EXISTS "${TENSORRT_PATH}/libnvinfer_builder_resource.so.10.3.0")
    list(APPEND TENSORRT_LIBS "${TENSORRT_PATH}/libnvinfer_builder_resource.so.10.3.0")
endif()

# 链接库
target_link_libraries(${PROJECT_NAME} PRIVATE
    ${OpenCV_LIBS}
    ${CUDA_LIBRARIES}
    ${TENSORRT_LIBS}
    cuda
    cudart
)

# CUDA特定设置
set_target_properties(${PROJECT_NAME} PROPERTIES
    CUDA_SEPARABLE_COMPILATION ON
)

# 设置CUDA架构（Jetson Orin NX使用Ampere架构，对应compute capability 8.7）
set(CMAKE_CUDA_ARCHITECTURES 87)

# 输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# 添加TensorRT运行时路径到RPATH
set_target_properties(${PROJECT_NAME} PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "${TENSORRT_PATH}"
)

