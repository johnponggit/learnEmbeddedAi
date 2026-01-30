
#pragma once

#include <vector>
#include <atomic>
#include <mutex>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_drm.h>
}

#include "util.h"


namespace emai {

class IVideoEncoder {
public:
    using Ptr =  std::shared_ptr<IVideoEncoder>;
    using uPtr =  std::unique_ptr<IVideoEncoder>;

    virtual ~IVideoEncoder() = default;

    virtual bool Init(int input_width, int input_height, int output_width, int output_height, int fps, int bitrate) = 0;
    virtual int  unInit() = 0;
    virtual bool EncodeFrame(AVFrame* frame, std::vector<uint8_t>& encoded_data) = 0;
};       

}
 
 