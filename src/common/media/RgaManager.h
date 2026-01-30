#pragma once
#include <mutex>
#include <dlfcn.h>
#include "RgaApi.h"

namespace emai {

class RgaManager {
public:
    static RgaManager& GetInstance() {
        static RgaManager instance;
        return instance;
    }

    // 提供全局的 Blit 接口
    int Blit(rga_info_t* src, rga_info_t* dst, rga_info_t* src_rect) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (blit_func_) {
            return blit_func_(src, dst, src_rect);
        }
        return -1;
    }

private:
    RgaManager();
    ~RgaManager();

    void* rga_handle_ = nullptr;
    typedef int (*FUNC_RGA_INIT)();
    typedef void (*FUNC_RGA_DEINIT)();
    typedef int (*FUNC_RGA_BLIT)(rga_info_t*, rga_info_t*, rga_info_t*);

    FUNC_RGA_INIT init_func_ = nullptr;
    FUNC_RGA_DEINIT deinit_func_ = nullptr;
    FUNC_RGA_BLIT blit_func_ = nullptr;
    std::mutex mutex_;
};

} // namespace emai