cmake_minimum_required(VERSION 3.1)

project(demo_blur)

file(GLOB_RECURSE SRC_FILES "*.cpp")
file(GLOB_RECURSE INC_FILES "*.h")

message(STATUS "RPOJ:${PROJECT_NAME}-SRC_FILES: ${SRC_FILES}")
message(STATUS "RPOJ:${PROJECT_NAME}-INC_FILES: ${INC_FILES}")

add_executable(${PROJECT_NAME} ${SRC_FILES} ${INC_FILES})


#设置依赖库
set(3RDPARTY_LIB "emai_common" "ssl" "crypto" "avcodec" "avformat" 
                 "avutil" "swscale" "swresample")

# RV1126 平台特定库
if (FFMPEG_LIB_TYPE STREQUAL "rv1126_4.1.3_rkmpp")  
    list(APPEND 3RDPARTY_LIB "theoraenc" "theoradec" "vorbis" "vorbisenc" "rtmp" "ogg" "rockchip_mpp" "z" "bz2" "rga" "drm")
elseif (FFMPEG_LIB_TYPE STREQUAL "rv1126_5.1.2_cpu")
    list(APPEND 3RDPARTY_LIB "x264" "x265")
endif() 

message(STATUS "RPOJ:${PROJECT_NAME}-3RDPARTY_LIB: ${3RDPARTY_LIB}")

target_include_directories( ${PROJECT_NAME} PRIVATE
                            ${CMAKE_CURRENT_SOURCE_DIR}   
                            ${THIRD_DIR}/../src/common/util       
                            ${THIRD_DIR}/cpp-httplib/include/
                            ${THIRD_DIR}/ffmpeg/include/${FFMPEG_LIB_TYPE}
                            ${THIRD_DIR}/nlohmann/include/
                            ${COMMON_HEADER_DIR}/media/
                            )

target_link_directories(${PROJECT_NAME} PRIVATE 
                        ${THIRD_DIR}/OpenSSL/lib/${PLAT_FORM} 
                        ${THIRD_DIR}/ffmpeg/lib/${FFMPEG_LIB_TYPE}
                        ${THIRD_DIR}/rk/lib/${PLAT_FORM}/
                        )

target_link_libraries(${PROJECT_NAME}     
    ${3RDPARTY_LIB}
    -lpthread
)