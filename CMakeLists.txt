cmake_minimum_required(VERSION 2.8)
project(ffmpeg_examples)

#set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../../cmake) # for cuda nppi

set (CMAKE_CXX_FLAGS "-std=c++11 ${CMAKE_CXX_FLAGS}")

# set (CMAKE_BUILD_TYPE Debug)

#find_package(CUDA)
#if (NOT CUDA_npp_LIBRARY)
#  message( FATAL_ERROR "CUDA_npp_LIBRARY not found" )
#endif()

find_package(OpenCV)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET
    libavdevice
    libavfilter
    libavformat
    libavcodec
    libswresample
    libswscale
    libavutil
)


include_directories(
  include
  ${CUDA_INCLUDE_DIRS}
)

link_directories(

)

file(GLOB SRC_CODE
    "src/*.cpp"
    "src/*.cc"
    "src/*.c"
)

set(ALL_LIBS 
  PkgConfig::LIBAV
  cudnn ${CUDA_CUBLAS_LIBRARIES}  
  ${OpenCV_LIBS}
  ${CUDA_npp_LIBRARY}
  ${CUDA_LIBRARIES}
  pthread
)

add_executable(my_encoder src/my_encoder.c)
target_link_libraries(my_encoder PRIVATE
  ${ALL_LIBS}
)

