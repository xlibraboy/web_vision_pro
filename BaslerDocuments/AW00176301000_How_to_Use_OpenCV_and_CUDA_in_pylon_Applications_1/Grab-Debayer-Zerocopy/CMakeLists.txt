cmake_minimum_required(VERSION 3.1.0)
project(Grab-Debayer-Zerocopy_cmake)

# Specify the pylon root directory and the pylon library directory
set(PYLON_ROOT "/opt/pylon")
set (PYLON_LIB_DIR "${PYLON_ROOT}/lib")

# Set the module path to enable pylon finder
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/modules")

# Search for programs in the build host directories
SET(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)

# Search for libraries and headers in the target directories
SET(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
SET(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
find_package(Pylon REQUIRED)

SET(OPENCV_LIB_DIR "/usr/local/lib/")
SET(OPENCV_INCLUDE_DIR "/usr/local/include/opencv4")
SET(CUDA_LIB_DIR "/usr/local/cuda/lib64")
SET(CUDA_INCLUDE_DIR "/usr/local/cuda/include")

INCLUDE_DIRECTORIES(
  ${OPENCV_INCLUDE_DIR}
  ${CUDA_INCLUDE_DIR}
  ${Pylon_INCLUDE_DIRS}
)

LINK_DIRECTORIES(
  ${OPENCV_LIB_DIR}
  ${CUDA_LIB_DIR}
)

# Populate a CMake variable with the sources
set (SRCS
  Grab-Debayer-Zerocopy.cpp
)

# Add the executable
add_executable(Grab-Debayer-Zerocopy ${SRCS})
target_link_libraries (Grab-Debayer-Zerocopy ${Pylon_LIBRARIES} opencv_core opencv_highgui opencv_imgproc opencv_cudaimgproc opencv_cudaarithm cudart)
