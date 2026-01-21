
#pragma once

#include "jpegEncoderImpl.h"

namespace emai {

class JpegEncoderFfmpegCpu : public JpegEncoderImpl {
public:
    using Ptr =  std::shared_ptr<JpegEncoderFfmpegCpu>;
    using uPtr =  std::unique_ptr<JpegEncoderFfmpegCpu>;

    JpegEncoderFfmpegCpu() = default;
    ~JpegEncoderFfmpegCpu();

    bool Init(const int width, const int height) override;
    int  unInit() override;
    bool saveFrameAsJpeg(AVFrame* frame, const std::string& filename, std::string& outFileData) override;
    std::vector<uint8_t> encode(const YUVFrame& yuv_frame) override;

private:
    AVFrame* convert2yuv(AVFrame* frame);

    AVCodecContext*          jpegCtx_{nullptr};
    struct SwsContext*       swsCtx_{nullptr};
    AVFrame*                 av_frame_{nullptr};
    AVPacket*                pkt_{nullptr};

    std::atomic<bool>        initOk_{false};
};       

}