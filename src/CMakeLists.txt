cmake_minimum_required(VERSION 3.1)

set(THIRD_DIR         ${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/)
set(COMMON_HEADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/common/)

add_subdirectory(common)
add_subdirectory(app)