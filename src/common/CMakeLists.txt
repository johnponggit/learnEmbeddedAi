cmake_minimum_required(VERSION 3.1)

project(emai_common)


# 添加包含目录 - 当前目录（src）及其子目录
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}       # common目录
    ${CMAKE_CURRENT_SOURCE_DIR}/media # media子目录
    ${CMAKE_CURRENT_SOURCE_DIR}/util  # util子目录
    ${THIRD_DIR}/cpp-httplib/include/
    ${THIRD_DIR}/ffmpeg/include/${FFMPEG_LIB_TYPE}
    ${THIRD_DIR}/nlohmann/include/
)



file(GLOB_RECURSE SRC_FILES "*.cpp")
file(GLOB_RECURSE INC_FILES "*.h")

message(STATUS "SRC_FILES: ${SRC_FILES}")
message(STATUS "INC_FILES: ${INC_FILES}")

add_library(${PROJECT_NAME} STATIC ${SRC_FILES} ${INC_FILES})

target_link_libraries(${PROJECT_NAME} 
    PRIVATE 
    stdc++fs
)