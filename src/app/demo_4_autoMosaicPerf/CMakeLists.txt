cmake_minimum_required(VERSION 3.1)

project(demo_4_autoMosaicPerf)

file(GLOB_RECURSE SRC_FILES "*.cpp")
file(GLOB_RECURSE INC_FILES "*.h")

message(STATUS "RPOJ:${PROJECT_NAME}-SRC_FILES: ${SRC_FILES}")
message(STATUS "RPOJ:${PROJECT_NAME}-INC_FILES: ${INC_FILES}")

add_executable(${PROJECT_NAME} ${SRC_FILES} ${INC_FILES})


# 设置依赖库
# 先定义基础库，再添加平台特定库
set(FFMPEG_BASE_LIBS avcodec avformat avutil swscale swresample)
set(OPENSSL_LIBS ssl crypto)
set(RKNN_LIBS rknn_api)
set(RGA_LIBS rga)
set(OTHER_LIBS dl pthread)

# 将 3RDPARTY_LIB 拆分成多个部分，便于管理
set(3RDPARTY_LIB 
    ${FFMPEG_BASE_LIBS}
    ${OPENSSL_LIBS}
    ${RKNN_LIBS}
    ${RGA_LIBS}
    ${OTHER_LIBS}
)

# RV1126 平台特定库
if (FFMPEG_LIB_TYPE STREQUAL "rv1126_4.1.3_rkmpp")  
    # 添加 Rockchip MPP 相关库
    list(APPEND 3RDPARTY_LIB rockchip_mpp)
    # 添加其他可能的依赖
    list(APPEND 3RDPARTY_LIB theoraenc theoradec vorbis vorbisenc rtmp ogg z bz2 drm)
elseif (FFMPEG_LIB_TYPE STREQUAL "rv1126_5.1.2_cpu")
    # 添加软件编码库
    list(APPEND 3RDPARTY_LIB x264 x265)
endif() 

message(STATUS "RPOJ:${PROJECT_NAME}-3RDPARTY_LIB: ${3RDPARTY_LIB}")

# 包含目录
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}   
    ${THIRD_DIR}/../src/common/util       
    ${THIRD_DIR}/cpp-httplib/include/
    ${THIRD_DIR}/ffmpeg/include/${FFMPEG_LIB_TYPE}
    ${THIRD_DIR}/nlohmann/include/
    ${THIRD_DIR}/rockchip/
    ${THIRD_DIR}/rockchip/rga/include/
    ${THIRD_DIR}/rockchip/librknn_api/include/
    ${COMMON_HEADER_DIR}/media/
    ${COMMON_HEADER_DIR}/infer/rknn/yolov5/
)

# 链接目录
target_link_directories(${PROJECT_NAME} PRIVATE 
    ${THIRD_DIR}/OpenSSL/lib/${PLAT_FORM} 
    ${THIRD_DIR}/ffmpeg/lib/${FFMPEG_LIB_TYPE}
    ${THIRD_DIR}/rockchip/rk/lib/${PLAT_FORM}/
    ${THIRD_DIR}/rockchip/librknn_api/lib/
)

# 链接库 - 注意链接顺序很重要！
target_link_libraries(${PROJECT_NAME}
    # 1. 首先链接自定义静态库
    emai_common
    
    # 2. 链接第三方库（按依赖顺序）
    ${3RDPARTY_LIB}    
  
)