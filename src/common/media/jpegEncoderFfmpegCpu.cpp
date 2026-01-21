#include <filesystem>

#include "jpegEncoderFfmpegCpu.h"

namespace fs = std::filesystem;

namespace emai {

static std::string avErrorToString(int errnum) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    if (av_strerror(errnum, errbuf, sizeof(errbuf)) == 0) {
        return std::string(errbuf);
    }
    return "unknown error";
}

static std::string pixelFormatToString(AVPixelFormat pix_fmt) {
    const char* fmt_name = av_get_pix_fmt_name(pix_fmt);
    if (fmt_name) {
        return fmt_name;
    } else {
        return "Unknown pixel format: " + std::to_string(static_cast<int>(pix_fmt));
    }
}

JpegEncoderFfmpegCpu::~JpegEncoderFfmpegCpu()
{
    LOG_INFO("in JpegEncoderFfmpegCpu destructor");
    unInit();
}

bool JpegEncoderFfmpegCpu::Init(const int width, const int height)
{
    LOG_INFO("in JpegEncoderFfmpegCpu Init, width:" << width << ", height:" << height);

    if (initOk_) 
    {
        LOG_WARN("in JpegEncoderFfmpegCpu Init already init");
        return false;
    }

    const AVCodec* jpeg_codec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
    if (!jpeg_codec) 
    {
        LOG_ERROR("in JpegEncoderFfmpegCpu Init avcodec_find_encoder failed");
        return false;
    }
    
    jpegCtx_ = avcodec_alloc_context3(jpeg_codec);
    if (!jpegCtx_) 
    {
        LOG_ERROR("in JpegEncoderFfmpegCpu Init avcodec_alloc_context3 failed");
        return false;
    }
    
    jpegCtx_->width     = width;    
    jpegCtx_->height    = height;
    jpegCtx_->pix_fmt   = AV_PIX_FMT_YUVJ420P;
    jpegCtx_->time_base = (AVRational){1, 25};
    jpegCtx_->time_base = {1, 25};
    jpegCtx_->framerate = {25, 1};
    jpegCtx_->qmin      = 2;
    jpegCtx_->qmax      = 31;

    int ret = -1;
    if ((ret = avcodec_open2(jpegCtx_, jpeg_codec, nullptr)) < 0) {
        LOG_ERROR("in JpegEncoderFfmpegCpu Init avcodec_open2 failed, ret:" << ret << ",errMsg:" << avErrorToString(ret));
        avcodec_free_context(&jpegCtx_);
        return false;
    }
    
    av_frame_ = av_frame_alloc();
    pkt_ = av_packet_alloc();

    initOk_ = true;
    return true;
}


int  JpegEncoderFfmpegCpu::unInit() 
{
    if (jpegCtx_) {
        avcodec_free_context(&jpegCtx_);
        jpegCtx_ = nullptr;
    }

    if (swsCtx_) 
    {
        sws_freeContext(swsCtx_);
        swsCtx_ = nullptr;
    }

    if (av_frame_) 
    {
        av_frame_free(&av_frame_);
        av_frame_ = nullptr;
    }
    
    if (pkt_) 
    {
        av_packet_free(&pkt_);
        pkt_ = nullptr;
    }
        
    initOk_ = false;
    LOG_INFO("in JpegEncoderFfmpegCpu unInit");
    return 0;
}

bool JpegEncoderFfmpegCpu::saveFrameAsJpeg(AVFrame* frame, const std::string& filename, std::string& outFileData) 
{
    if (!jpegCtx_) 
    {
        LOG_ERROR("in saveFrameAsJpeg jpegCtx_ is null");
        return false;
    }

    if (!frame)
    {
        LOG_ERROR("in saveFrameAsJpeg frame is null");
        return false;
    }
    
    LOG_INFO("in saveFrameAsJpeg, filename:" << filename 
                << ", frame format:" << pixelFormatToString((AVPixelFormat)frame->format)
                << ", width:" << frame->width << ", height:" << frame->height);
            
    // 确保JPEG编码器尺寸匹配
    if (jpegCtx_->width != frame->width || jpegCtx_->height != frame->height) {
        LOG_WARN("in saveFrameAsJpeg resizing jpegCtx_ from "
                    << jpegCtx_->width << "x" << jpegCtx_->height << " to "
                    << frame->width << "x" << frame->height);
        
        avcodec_free_context(&jpegCtx_);
        jpegCtx_ = nullptr;

        unInit();
        if (!Init(frame->width, frame->height)) 
        {
            LOG_ERROR("in saveFrameAsJpeg re-init jpegCtx_ failed");
            return false;
        }
    }
    
    AVFrame* yuv_frame = convert2yuv(frame);
    if (!yuv_frame) 
    {
        LOG_ERROR("in saveFrameAsJpeg convert2yuv failed");
        return false;
    }

    AVPacket* pkt = av_packet_alloc();
    bool success = false;
    
    if (avcodec_send_frame(jpegCtx_, yuv_frame) >= 0 &&
        avcodec_receive_packet(jpegCtx_, pkt) >= 0) {
        
        outFileData = std::string(reinterpret_cast<char*>(pkt->data), pkt->size);

        FILE* file = fopen(filename.c_str(), "wb");
        if (file) {
            fwrite(pkt->data, 1, pkt->size, file);
            fclose(file);
            success = true;
        }
    }
    
    av_packet_free(&pkt);
    av_frame_free(&yuv_frame);

    LOG_INFO("in saveFrameAsJpeg, filename:" << filename 
                << (success ? " saved successfully." : " failed to save.") << 
                ", file data size:" << outFileData.size());    

    return success;
}

// 编码YUVFrame为JPEG
std::vector<uint8_t> JpegEncoderFfmpegCpu::encode(const YUVFrame& yuv_frame) 
{
    std::vector<uint8_t> jpeg_data;
    
    if (!jpegCtx_ || yuv_frame.empty()) {
        return jpeg_data;
    }
    
    // 转换像素格式
    if (!swsCtx_) {
        swsCtx_ = sws_getContext(
            yuv_frame.width, yuv_frame.height,
            AV_PIX_FMT_YUV420P,
            jpegCtx_->width, jpegCtx_->height,
            jpegCtx_->pix_fmt,
            SWS_BILINEAR, nullptr, nullptr, nullptr
        );
    }
    
    // 准备输入帧
    AVFrame* input_frame = yuv_frame.to_avframe();
    if (!input_frame) {
        return jpeg_data;
    }
    
    // 准备输出帧
    av_frame_->width = jpegCtx_->width;
    av_frame_->height = jpegCtx_->height;
    av_frame_->format = jpegCtx_->pix_fmt;
    av_frame_->pts = input_frame->pts;
    
    if (av_frame_get_buffer(av_frame_, 0) < 0) {
        av_frame_free(&input_frame);
        return jpeg_data;
    }
    
    // 转换像素格式
    sws_scale(swsCtx_, input_frame->data, input_frame->linesize,
                0, input_frame->height,
                av_frame_->data, av_frame_->linesize);
    
    // 编码为JPEG
    int ret = avcodec_send_frame(jpegCtx_, av_frame_);
    if (ret < 0) {
        av_frame_free(&input_frame);
        return jpeg_data;
    }
    
    ret = avcodec_receive_packet(jpegCtx_, pkt_);
    if (ret == 0) {
        jpeg_data.assign(pkt_->data, pkt_->data + pkt_->size);
    }
    
    av_frame_unref(av_frame_);
    av_packet_unref(pkt_);
    av_frame_free(&input_frame);
    
    return jpeg_data;
}

AVFrame* JpegEncoderFfmpegCpu::convert2yuv(AVFrame* frame) 
{
    if (!swsCtx_) {
        LOG_INFO("in convert2yuv initializing swsCtx_");
        swsCtx_ = sws_getContext(frame->width, frame->height, (AVPixelFormat)frame->format,
                                 frame->width, frame->height, AV_PIX_FMT_YUVJ420P,
                                 SWS_BILINEAR, nullptr, nullptr, nullptr);
    }
    
    if (!swsCtx_) 
    {
        LOG_ERROR("in convert2yuv sws_getContext failed");
        return nullptr;
    }

    AVFrame* yuv_frame = av_frame_alloc();
    yuv_frame->format = AV_PIX_FMT_YUVJ420P;
    yuv_frame->width = frame->width;
    yuv_frame->height = frame->height;
    
    if (av_frame_get_buffer(yuv_frame, 0) < 0) {
        LOG_ERROR("in convert2yuv av_frame_get_buffer failed");
        av_frame_free(&yuv_frame);
        return nullptr;
    }
    
    sws_scale(swsCtx_, frame->data, frame->linesize, 0,
              frame->height, yuv_frame->data, yuv_frame->linesize);
    
    return yuv_frame;
}

}
