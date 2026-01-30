#include "RgaManager.h"
#include <iostream>

#include "util.h"

namespace emai {

RgaManager::RgaManager() {
    rga_handle_ = dlopen("/usr/lib/librga.so", RTLD_LAZY);
    if (!rga_handle_) {
        LOG_ERROR("Failed to dlopen librga.so");
        return;
    }

    init_func_ = (FUNC_RGA_INIT)dlsym(rga_handle_, "c_RkRgaInit");
    deinit_func_ = (FUNC_RGA_DEINIT)dlsym(rga_handle_, "c_RkRgaDeInit");
    blit_func_ = (FUNC_RGA_BLIT)dlsym(rga_handle_, "c_RkRgaBlit");

    if (init_func_) init_func_();
}

RgaManager::~RgaManager() {
    if (deinit_func_) deinit_func_();
    if (rga_handle_) dlclose(rga_handle_);
}

}