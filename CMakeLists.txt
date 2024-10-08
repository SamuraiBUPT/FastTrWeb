# CMakeLists.txt

cmake_minimum_required(VERSION 3.10)

# Set the project name
project(TrWebOCRcpp)

find_package(CUDA REQUIRED)
set(CUDA_NVCC_FLAGS "${CUDA_NVCC_FLAGS} -gencode arch=compute_70,code=sm_70")

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

if(CMAKE_BUILD_TYPE MATCHES Debug)
    # Debug 模式编译选项（无优化）
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")
elseif(CMAKE_BUILD_TYPE MATCHES Release)
    # Release 模式编译选项（优化级别为 2 或 3）
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")
endif()

option(USE_GPU "Enable GPU support" OFF)

# Include the current directory for header files
include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_SOURCE_DIR}/3rd_party)
include_directories(${CUDA_INCLUDE_DIRS})

set(CTPN_PATH \"${CMAKE_SOURCE_DIR}/tr/tr/ctpn.bin\")
set(CRNN_PATH \"${CMAKE_SOURCE_DIR}/tr/tr/crnn.bin\")
set(CUDA_LIBRARIES ${CUDA_CUDART_LIBRARY})

find_package(OpenCV REQUIRED)
include_directories(${OpenCV_INCLUDE_DIRS})

# Add the executable target (main.cc)
add_executable(main 
               csrc/tr.h
               csrc/main.cc
               csrc/tr_wrapper.h
               csrc/tr_wrapper.cc
               csrc/tr_worker.h
               csrc/tr_worker.cc
               csrc/tr_utils.h

               3rd_party/base64.cpp
               3rd_party/base64.h
               3rd_party/json.hpp
               3rd_party/json.hpp
               )

target_compile_definitions(main PRIVATE CTPN_PATH=${CTPN_PATH})
target_compile_definitions(main PRIVATE CRNN_PATH=${CRNN_PATH})

# Link the shared library (libtr.so)
if(USE_GPU)
    add_definitions(-DUSE_GPU)
    target_link_libraries(main ${CMAKE_SOURCE_DIR}/tr/tr_gpu/libtr.so)
    target_link_libraries(main ${CMAKE_SOURCE_DIR}/tr/tr_gpu/libonnxruntime.so.1.3.0)
else()
    target_link_libraries(main ${CMAKE_SOURCE_DIR}/tr/tr/libtr.so)
    target_link_libraries(main ${CMAKE_SOURCE_DIR}/tr/tr/libonnxruntime.so.1.3.0)
endif()

target_link_libraries(main pthread)
target_link_libraries(main ${OpenCV_LIBS})
target_link_libraries(main ${CUDA_LIBRARIES})