
# 交叉编译工具链配置文件

# 设置系统名和处理器架构
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# 指定交叉编译工具链路径
set(TOOLCHAIN_PATH "/work/onvif/crosscompilation/toolchain/gcc-arm-8.3-2019.03-x86_64-arm-linux-gnueabihf/bin")

# 设置编译器
set(CMAKE_C_COMPILER ${TOOLCHAIN_PATH}/arm-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PATH}/arm-linux-gnueabihf-g++)

# 设置其他工具
set(CMAKE_AR ${TOOLCHAIN_PATH}/arm-linux-gnueabihf-ar)
set(CMAKE_LINKER ${TOOLCHAIN_PATH}/arm-linux-gnueabihf-ld)
set(CMAKE_NM ${TOOLCHAIN_PATH}/arm-linux-gnueabihf-nm)
set(CMAKE_OBJCOPY ${TOOLCHAIN_PATH}/arm-linux-gnueabihf-objcopy)
set(CMAKE_OBJDUMP ${TOOLCHAIN_PATH}/arm-linux-gnueabihf-objdump)
set(CMAKE_RANLIB ${TOOLCHAIN_PATH}/arm-linux-gnueabihf-ranlib)
set(CMAKE_STRIP ${TOOLCHAIN_PATH}/arm-linux-gnueabihf-strip)

# 设置编译器和链接器标志
#set(CMAKE_C_FLAGS "-march=armv7-a -mfpu=neon -mfloat-abi=hard" CACHE STRING "C flags")
#set(CMAKE_CXX_FLAGS "-march=armv7-a -mfpu=neon -mfloat-abi=hard" CACHE STRING "C++ flags")
#set(CMAKE_EXE_LINKER_FLAGS "-Wl,--gc-sections" CACHE STRING "Executable linker flags")
#set(CMAKE_SHARED_LINKER_FLAGS "-Wl,--gc-sections" CACHE STRING "Shared library linker flags")

# 设置 sysroot（如果需要）
# set(CMAKE_SYSROOT "/work/onvif/crosscompilation/sysroot/arm-linux-gnueabihf")

# 设置查找规则
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)    # 不在交叉环境中查找程序
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)     # 只在交叉环境中查找库
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)     # 只在交叉环境中查找头文件
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)     # 只在交叉环境中查找包

# 设置默认的构建类型（可选）
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# 输出信息
message(STATUS "Cross-compiling for ARM Linux (gnueabihf)")
message(STATUS "Toolchain path: ${TOOLCHAIN_PATH}")
message(STATUS "C compiler: ${CMAKE_C_COMPILER}")
message(STATUS "C++ compiler: ${CMAKE_CXX_COMPILER}")
