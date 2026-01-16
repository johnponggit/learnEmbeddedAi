cmake_minimum_required(VERSION 3.1)

# 添加平台选项
if(NOT PLAT_FORM)
    set(PLAT_FORM "rv1126")
endif()
message(STATUS "Selected platform: ${PLAT_FORM}")

# 设置交叉编译工具链文件路径
set(CMAKE_TOOLCHAIN_FILE "${CMAKE_SOURCE_DIR}/toolchains/${PLAT_FORM}.cmake")
message(STATUS "Loading toolchain file: ${CMAKE_TOOLCHAIN_FILE}")

project(learn_embedded_ai)

# 项目设置
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS} -g -O0 -DDEBUG -fPIC")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS} -O3 -fPIC -s")

# build lib
add_subdirectory(src)

# build test


