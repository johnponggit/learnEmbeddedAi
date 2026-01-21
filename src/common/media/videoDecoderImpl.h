
#pragma once

#include <atomic>
#include <chrono>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#ifdef __cplusplus
extern "C" {
#endif
#include <libswscale/swscale.h>
#include <libavutil/opt.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/hwcontext.h>
#ifdef __cplusplus
}
#endif

#include "videoParser.h"

namespace emai {

class IDecodeEventHandle {
public:
    using Ptr =  std::shared_ptr<IDecodeEventHandle>;
    using uPtr =  std::unique_ptr<IDecodeEventHandle>;
    using wPtr =  std::weak_ptr<IDecodeEventHandle>;

    virtual ~IDecodeEventHandle() = default;

    virtual void onDecodeEos() = 0;
    virtual void onDecodeFrame(AVFrame* frame, const int64_t frmIndex) = 0;
};

class VideoDecoderImpl {
public:
    using Ptr =  std::shared_ptr<VideoDecoderImpl>;
    using uPtr =  std::unique_ptr<VideoDecoderImpl>;

    virtual ~VideoDecoderImpl() = default;

    virtual bool Init(const VideoInfo& info) = 0;
    virtual int unInit() = 0;
 
    virtual bool FeedPacket(const AVPacket* pkt, const int64_t frmIndex) = 0;
    virtual void FeedEos() = 0;
};       

}
 
 