
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
#if LIBAVFORMAT_VERSION_INT == FFMPEG_VERSION_4_2_2
#include <libavutil/hwcontext.h>
#endif
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
    virtual bool needSnap(const int64_t frmIndex) = 0;
    virtual void onSnapDone(const std::string& filename, std::shared_ptr<const std::string> fileData, const int64_t curFrmIndex) = 0;
    virtual int  getMinSnapInterval() = 0;
    virtual void setKeyFrmInfo(const int fps, const int gopSz, bool en) = 0;
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
 
 