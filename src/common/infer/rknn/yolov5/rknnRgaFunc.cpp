// Copyright (c) 2021 by Rockchip Electronics Co., Ltd. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "util.h"
#include "rknnRgaFunc.h"
#include "RgaManager.h"

namespace emai {

int RGA_init(rga_context *rga_ctx)
{
    #if 0
    rga_ctx->rga_handle = dlopen("/usr/lib/librga.so", RTLD_LAZY);
    if (!rga_ctx->rga_handle)
    {
        LOG_DEBUG("dlopen /usr/lib/librga.so failed");
        return -1;
    }
    rga_ctx->init_func = (FUNC_RGA_INIT)dlsym(rga_ctx->rga_handle, "c_RkRgaInit");
    rga_ctx->deinit_func = (FUNC_RGA_DEINIT)dlsym(rga_ctx->rga_handle, "c_RkRgaDeInit");
    rga_ctx->blit_func = (FUNC_RGA_BLIT)dlsym(rga_ctx->rga_handle, "c_RkRgaBlit");
    rga_ctx->init_func();
    #else
    RgaManager::GetInstance();
    #endif 
    return 0;
}


void RGA_resize(rga_context *rga_ctx, int src_fd, void *src_virt, int src_w, int src_h, int  dst_fd, void *dst_virt, int dst_w, int dst_h)
{
    // LOG_DEBUG("rga use fd, src(" << src_w << "x" << src_h << ") -> dst(" << dst_w << "x" << dst_h << ")");
    #if 0
    if (rga_ctx->rga_handle)
    {
        int ret = 0;
        rga_info_t src, dst;

        memset(&src, 0, sizeof(rga_info_t));
        src.fd = src_fd;
        src.mmuFlag = 1;
        src.virAddr = (void *)src_virt;

        memset(&dst, 0, sizeof(rga_info_t));
        dst.fd = dst_fd;
        dst.mmuFlag = 1;
        dst.virAddr = dst_virt;
        dst.nn.nn_flag = 0;

        rga_set_rect(&src.rect, 0, 0, src_w, src_h, src_w, src_h, RK_FORMAT_RGB_888);
        rga_set_rect(&dst.rect, 0, 0, dst_w, dst_h, dst_w, dst_h, RK_FORMAT_RGB_888);

        ret = rga_ctx->blit_func(&src, &dst, NULL);
        if (ret)
        {
            LOG_ERROR("c_RkRgaBlit error :" << strerror(errno));
        }

        return;
    }
    #else
    rga_info_t src, dst;
    memset(&src, 0, sizeof(rga_info_t));
    memset(&dst, 0, sizeof(rga_info_t));

    src.fd = src_fd;
    src.virAddr = src_virt;
    src.mmuFlag = 1;
    rga_set_rect(&src.rect, 0, 0, src_w, src_h, src_w, src_h, RK_FORMAT_RGB_888);

    dst.fd = dst_fd;
    dst.virAddr = dst_virt;
    dst.mmuFlag = 1;
    rga_set_rect(&dst.rect, 0, 0, dst_w, dst_h, dst_w, dst_h, RK_FORMAT_RGB_888);

    if (RgaManager::GetInstance().Blit(&src, &dst, NULL) != 0) {
        LOG_ERROR("RGA_resize Blit failed");
    }

    #endif
    return;
}


 


int RGA_deinit(rga_context *rga_ctx)
{
    #if 0
    if(rga_ctx->rga_handle)
    {
        dlclose(rga_ctx->rga_handle);
        rga_ctx->rga_handle = NULL;
    }
    #endif

    return 0;
}

}