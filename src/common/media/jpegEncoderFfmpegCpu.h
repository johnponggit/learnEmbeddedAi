
#pragma once

#include "IJpegEncoder.h"

namespace emai {

class JpegEncoderFfmpegCpu : public IJpegEncoder {
public:
    using Ptr =  std::shared_ptr<JpegEncoderFfmpegCpu>;
    using uPtr =  std::unique_ptr<JpegEncoderFfmpegCpu>;

    JpegEncoderFfmpegCpu() = default;
    ~JpegEncoderFfmpegCpu();

    bool Init(const int width, const int height) override;
    int  unInit() override;

    bool saveFrameAsJpeg(AVFrame* frame, const std::string& filename, std::string& outFileData) override;
    bool saveRgbAsJpeg(const uint8_t* rgb_data, int width, int height, const std::string& filename, std::string& outFileData) override;   
    
    std::vector<uint8_t> encode(const YUVFrame& yuv_frame) override;
    std::vector<uint8_t> encodeRgb(const uint8_t* rgb_data, int width, int height) override;

    AVFrame* convertYUV420PtoRGB24(AVFrame* yuv_frame) override;    

private:
    bool     initRgbToYuvContext(int width, int height);
    AVFrame* convert2yuv(AVFrame* frame);
    
    AVCodecContext*          jpegCtx_{nullptr};
    struct SwsContext*       swsCtx_{nullptr};
    AVFrame*                 av_frame_{nullptr};
    AVPacket*                pkt_{nullptr};

    struct SwsContext*       rgb_to_yuv_ctx_{nullptr};
    int                      last_rgb_width_ = 0;
    int                      last_rgb_height_ = 0;
    
    // YUV420P到RGB24转换的上下文
    struct SwsContext*       yuv_to_rgb_ctx_{nullptr};   
    int                      last_yuv_width_ = 0;
    int                      last_yuv_height_ = 0;

    std::atomic<bool>        initOk_{false};
};       

}