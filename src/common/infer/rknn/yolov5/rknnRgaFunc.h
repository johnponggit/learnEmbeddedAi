#pragma once


#include <dlfcn.h> 
#include "RgaApi.h"

namespace emai {

typedef int(* FUNC_RGA_INIT)();
typedef void(* FUNC_RGA_DEINIT)();
typedef int(* FUNC_RGA_BLIT)(rga_info_t *, rga_info_t *, rga_info_t *);

typedef struct _rga_context{
    void *rga_handle;
    FUNC_RGA_INIT init_func;
    FUNC_RGA_DEINIT deinit_func;
    FUNC_RGA_BLIT blit_func;
} rga_context;

int  RGA_init(rga_context* rga_ctx);
int  RGA_deinit(rga_context* rga_ctx);
void RGA_resize(rga_context *rga_ctx, int src_fd, void *src_virt, int src_w, int src_h, int  dst_fd, void *dst_virt, int dst_w, int dst_h);

}

