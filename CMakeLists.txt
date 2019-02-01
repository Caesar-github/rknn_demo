# vim: syntax=cmake
cmake_minimum_required(VERSION 2.8.11)
add_compile_options(-g -rdynamic -ldl -funwind-tables -fPIC -O0)

include_directories(.)
include_directories(./include)
include_directories(./common)
include_directories(./ui)

set(RKNN_DEMO_SRC
      rknn_camera.c
      common/rknn_msg.c
      common/v4l2camera.c
      common/yuv.c
      common/buffer.c
      ui/ui_res.c
    )

if(DEFINED ENABLE_SSD)
  add_definitions(-DENABLE_SSD=${ENABLE_SSD})
  include_directories(./rknn/ssd)
  include_directories(./ui/ssd)
  set(RKNN_MODEL_SRC
      rknn/ssd/ssd.c
      rknn/ssd/ssd_post.c
      rknn/ssd/tracker/objects_tracker.cpp
      rknn/ssd/tracker/objects_update.cpp
      rknn/ssd/tracker/track_c_link_c++.cpp
      ui/ssd/ssd_ui.c
    )
endif(DEFINED ENABLE_SSD)

if(DEFINED ENABLE_JOINT)
  add_definitions(-DENABLE_JOINT=${ENABLE_JOINT})
  include_directories(./rknn/joint)
  include_directories(./ui/joint)
  set(RKNN_MODEL_SRC
    rknn/joint/joint.c
    rknn/joint/joint_post.c
    ui/joint/joint_ui.c
  )
endif(DEFINED ENABLE_JOINT)

# import rknn
set (RKNPU_PATH ${CMAKE_SOURCE_DIR}/../../../../../external/rknpu)
include_directories(${RKNPU_PATH}/rknn/)
LINK_DIRECTORIES(${RKNPU_PATH}/rknn/)

link_libraries("librknn_runtime.so")
link_libraries("libv4l2.so")
link_libraries("libjpeg.so")
link_libraries("libm.so")
link_libraries("libdrm.so")
link_libraries("libminigui_ths.so")
link_libraries("libpng12.so")
link_libraries("libpthread.so")
link_libraries("librga.so")
link_libraries("libfreetype.so")

add_executable(rknn_demo ${RKNN_DEMO_SRC} ${RKNN_MODEL_SRC})
