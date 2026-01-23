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

    if (rgb_to_yuv_ctx_) 
    {
        sws_freeContext(rgb_to_yuv_ctx_);
        rgb_to_yuv_ctx_ = nullptr;
        last_rgb_width_ = 0;
        last_rgb_height_ = 0;
    }

    if (yuv_to_rgb_ctx_) {
        sws_freeContext(yuv_to_rgb_ctx_);
        yuv_to_rgb_ctx_ = nullptr;
        last_yuv_width_ = 0;
        last_yuv_height_ = 0;
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

bool JpegEncoderFfmpegCpu::initRgbToYuvContext(int width, int height) {
    if (rgb_to_yuv_ctx_) {
        // 如果已经初始化且尺寸相同，则重用
        if (width == last_rgb_width_ && height == last_rgb_height_) {
            return true;
        }
        // 尺寸变化，需要重新初始化
        sws_freeContext(rgb_to_yuv_ctx_);
        rgb_to_yuv_ctx_ = nullptr;
    }
    
    // 创建RGB到YUV的转换上下文
    // 假设RGB数据格式为RGB24（每个像素3字节，顺序为R,G,B）
    rgb_to_yuv_ctx_ = sws_getContext(
        width, height,                    // 源图像尺寸
        AV_PIX_FMT_RGB24,                 // 源像素格式：RGB24
        width, height,                    // 目标图像尺寸（保持相同）
        AV_PIX_FMT_YUVJ420P,              // 目标像素格式：YUVJ420P（JPEG编码常用）
        SWS_BILINEAR,                     // 缩放算法
        nullptr, nullptr, nullptr         // 其他参数
    );
    
    if (!rgb_to_yuv_ctx_) {
        LOG_ERROR("Failed to create RGB to YUV conversion context");
        return false;
    }
    
    // 记录最后一次初始化的尺寸
    last_rgb_width_ = width;
    last_rgb_height_ = height;
    
    LOG_DEBUG("RGB to YUV conversion context initialized: " 
              << width << "x" << height);
    
    return true;
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

// 将RGB数据保存为JPEG文件
bool JpegEncoderFfmpegCpu::saveRgbAsJpeg(const uint8_t* rgb_data, int width, int height, 
                                        const std::string& filename, std::string& outFileData) 
{
    if (!jpegCtx_) 
    {
        LOG_ERROR("in saveRgbAsJpeg jpegCtx_ is null");
        return false;
    }
    
    if (!rgb_data || width <= 0 || height <= 0) 
    {
        LOG_ERROR("in saveRgbAsJpeg invalid input parameters");
        return false;
    }
    
    LOG_INFO("in saveRgbAsJpeg, filename:" << filename 
                << ", width:" << width << ", height:" << height);
    
    // 确保JPEG编码器尺寸匹配
    if (jpegCtx_->width != width || jpegCtx_->height != height) 
    {
        LOG_WARN("in saveRgbAsJpeg resizing jpegCtx_ from "
                    << jpegCtx_->width << "x" << jpegCtx_->height << " to "
                    << width << "x" << height);
        
        // 重新初始化编码器
        unInit();
        if (!Init(width, height)) 
        {
            LOG_ERROR("in saveRgbAsJpeg re-init jpegCtx_ failed");
            return false;
        }
    }
    
    // 初始化RGB到YUV转换上下文
    if (!initRgbToYuvContext(width, height)) 
    {
        LOG_ERROR("in saveRgbAsJpeg initRgbToYuvContext failed");
        return false;
    }
    
    // 创建RGB输入帧
    AVFrame* rgb_frame = av_frame_alloc();
    if (!rgb_frame) 
    {
        LOG_ERROR("in saveRgbAsJpeg av_frame_alloc for rgb_frame failed");
        return false;
    }
    
    rgb_frame->width = width;
    rgb_frame->height = height;
    rgb_frame->format = AV_PIX_FMT_RGB24;
    
    // 分配RGB帧缓冲区
    if (av_frame_get_buffer(rgb_frame, 32) < 0) 
    {
        LOG_ERROR("in saveRgbAsJpeg av_frame_get_buffer for rgb_frame failed");
        av_frame_free(&rgb_frame);
        return false;
    }
    
    // 复制RGB数据到AVFrame
    // 假设输入数据是连续存储的RGB24格式
    int rgb_stride = width * 3; // RGB24，每个像素3字节
    for (int y = 0; y < height; y++) 
    {
        memcpy(rgb_frame->data[0] + y * rgb_frame->linesize[0],
               rgb_data + y * rgb_stride,
               rgb_stride);
    }
    
    // 创建YUV输出帧
    AVFrame* yuv_frame = av_frame_alloc();
    if (!yuv_frame) 
    {
        LOG_ERROR("in saveRgbAsJpeg av_frame_alloc for yuv_frame failed");
        av_frame_free(&rgb_frame);
        return false;
    }
    
    yuv_frame->width = width;
    yuv_frame->height = height;
    yuv_frame->format = AV_PIX_FMT_YUVJ420P;
    
    if (av_frame_get_buffer(yuv_frame, 32) < 0) 
    {
        LOG_ERROR("in saveRgbAsJpeg av_frame_get_buffer for yuv_frame failed");
        av_frame_free(&rgb_frame);
        av_frame_free(&yuv_frame);
        return false;
    }
    
    // 转换RGB到YUV
    sws_scale(rgb_to_yuv_ctx_, 
              rgb_frame->data, rgb_frame->linesize, 
              0, height,
              yuv_frame->data, yuv_frame->linesize);
    
    // 编码为JPEG
    AVPacket* pkt = av_packet_alloc();
    bool success = false;
    
    int ret = avcodec_send_frame(jpegCtx_, yuv_frame);
    if (ret < 0) 
    {
        LOG_ERROR("in saveRgbAsJpeg avcodec_send_frame failed, ret:" 
                  << ret << ", errMsg:" << avErrorToString(ret));
    } 
    else 
    {
        ret = avcodec_receive_packet(jpegCtx_, pkt);
        if (ret < 0) 
        {
            LOG_ERROR("in saveRgbAsJpeg avcodec_receive_packet failed, ret:" 
                      << ret << ", errMsg:" << avErrorToString(ret));
        } 
        else 
        {
            // 保存JPEG数据到字符串
            outFileData = std::string(reinterpret_cast<char*>(pkt->data), pkt->size);
            
            // 保存到文件
            FILE* file = fopen(filename.c_str(), "wb");
            if (file) 
            {
                fwrite(pkt->data, 1, pkt->size, file);
                fclose(file);
                success = true;
            }
            else
            {
                LOG_ERROR("in saveRgbAsJpeg failed to open file: " << filename);
            }
        }
    }
    
    // 清理资源
    av_packet_free(&pkt);
    av_frame_free(&rgb_frame);
    av_frame_free(&yuv_frame);
    
    LOG_INFO("in saveRgbAsJpeg, filename:" << filename 
                << (success ? " saved successfully." : " failed to save.")
                << ", file data size:" << outFileData.size());
    
    return success;
}

std::vector<uint8_t> JpegEncoderFfmpegCpu::encodeRgb(const uint8_t* rgb_data, int width, int height) 
{
    std::vector<uint8_t> jpeg_data;
    
    if (!jpegCtx_ || !rgb_data || width <= 0 || height <= 0) 
    {
        return jpeg_data;
    }
    
    LOG_DEBUG("Encoding RGB to JPEG: " << width << "x" << height);
    
    // 确保编码器尺寸匹配
    if (jpegCtx_->width != width || jpegCtx_->height != height) 
    {
        LOG_WARN("Encoder size mismatch, reinitializing: " 
                 << jpegCtx_->width << "x" << jpegCtx_->height << " -> " 
                 << width << "x" << height);
        
        unInit();
        if (!Init(width, height)) 
        {
            LOG_ERROR("Failed to reinitialize encoder");
            return jpeg_data;
        }
    }
    
    // 初始化RGB到YUV转换上下文
    if (!initRgbToYuvContext(width, height)) 
    {
        LOG_ERROR("Failed to initialize RGB to YUV context");
        return jpeg_data;
    }
    
    // 分配并填充RGB帧
    AVFrame* rgb_frame = av_frame_alloc();
    if (!rgb_frame) 
    {
        return jpeg_data;
    }
    
    rgb_frame->width = width;
    rgb_frame->height = height;
    rgb_frame->format = AV_PIX_FMT_RGB24;
    
    if (av_frame_get_buffer(rgb_frame, 32) < 0) 
    {
        av_frame_free(&rgb_frame);
        return jpeg_data;
    }
    
    // 复制RGB数据
    int rgb_stride = width * 3;
    for (int y = 0; y < height; y++) 
    {
        memcpy(rgb_frame->data[0] + y * rgb_frame->linesize[0],
               rgb_data + y * rgb_stride,
               rgb_stride);
    }
    
    // 分配YUV帧
    AVFrame* yuv_frame = av_frame_alloc();
    if (!yuv_frame) 
    {
        av_frame_free(&rgb_frame);
        return jpeg_data;
    }
    
    yuv_frame->width = width;
    yuv_frame->height = height;
    yuv_frame->format = AV_PIX_FMT_YUVJ420P;
    
    if (av_frame_get_buffer(yuv_frame, 32) < 0) 
    {
        av_frame_free(&rgb_frame);
        av_frame_free(&yuv_frame);
        return jpeg_data;
    }
    
    // 转换RGB到YUV
    sws_scale(rgb_to_yuv_ctx_, 
              rgb_frame->data, rgb_frame->linesize, 
              0, height,
              yuv_frame->data, yuv_frame->linesize);
    
    // 编码
    int ret = avcodec_send_frame(jpegCtx_, yuv_frame);
    if (ret < 0) 
    {
        LOG_ERROR("avcodec_send_frame failed: " << avErrorToString(ret));
    }
    else 
    {
        // 分配数据包
        AVPacket* pkt = av_packet_alloc();
        if (!pkt) 
        {
            av_frame_free(&rgb_frame);
            av_frame_free(&yuv_frame);
            return jpeg_data;
        }
        
        ret = avcodec_receive_packet(jpegCtx_, pkt);
        if (ret == 0) 
        {
            jpeg_data.assign(pkt->data, pkt->data + pkt->size);
        }
        
        av_packet_free(&pkt);
    }
    
    // 清理
    av_frame_free(&rgb_frame);
    av_frame_free(&yuv_frame);
    
    LOG_DEBUG("RGB to JPEG encoding complete, size: " << jpeg_data.size() << " bytes");
    
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

AVFrame* JpegEncoderFfmpegCpu::convertYUV420PtoRGB24(AVFrame* yuv_frame) {
    if (!yuv_frame) {
        LOG_ERROR("convertYUV420PtoRGB24: input frame is null");
        return nullptr;
    }
    
    if (yuv_frame->format != AV_PIX_FMT_YUV420P) {
        LOG_ERROR("convertYUV420PtoRGB24: input frame format is not YUV420P, actual format: " 
                  << av_get_pix_fmt_name((AVPixelFormat)yuv_frame->format));
        return nullptr;
    }
    
    LOG_DEBUG("Converting YUV420P to RGB24: " 
              << yuv_frame->width << "x" << yuv_frame->height);
    
    // 检查是否需要重新创建转换上下文
    if (!yuv_to_rgb_ctx_ || 
        last_yuv_width_ != yuv_frame->width || 
        last_yuv_height_ != yuv_frame->height) {
        
        if (yuv_to_rgb_ctx_) {
            sws_freeContext(yuv_to_rgb_ctx_);
            yuv_to_rgb_ctx_ = nullptr;
        }
        
        // 创建转换上下文
        yuv_to_rgb_ctx_ = sws_getContext(
            yuv_frame->width, yuv_frame->height,
            AV_PIX_FMT_YUV420P,                    // 源格式
            yuv_frame->width, yuv_frame->height,
            AV_PIX_FMT_RGB24,                      // 目标格式
            SWS_BILINEAR,                          // 缩放算法
            nullptr, nullptr, nullptr
        );
        
        if (!yuv_to_rgb_ctx_) {
            LOG_ERROR("convertYUV420PtoRGB24: failed to create SwsContext");
            return nullptr;
        }
        
        last_yuv_width_ = yuv_frame->width;
        last_yuv_height_ = yuv_frame->height;
        
        LOG_DEBUG("Created YUV420P to RGB24 conversion context: " 
                  << yuv_frame->width << "x" << yuv_frame->height);
    }
    
    // 创建输出RGB帧
    AVFrame* rgb_frame = av_frame_alloc();
    if (!rgb_frame) {
        LOG_ERROR("convertYUV420PtoRGB24: failed to allocate RGB frame");
        return nullptr;
    }
    
    rgb_frame->width = yuv_frame->width;
    rgb_frame->height = yuv_frame->height;
    rgb_frame->format = AV_PIX_FMT_RGB24;
    rgb_frame->pts = yuv_frame->pts;
    
    // 分配缓冲区
    if (av_frame_get_buffer(rgb_frame, 32) < 0) {
        LOG_ERROR("convertYUV420PtoRGB24: failed to allocate buffer for RGB frame");
        av_frame_free(&rgb_frame);
        return nullptr;
    }
    
    // 进行转换
    sws_scale(yuv_to_rgb_ctx_,
              yuv_frame->data, yuv_frame->linesize,
              0, yuv_frame->height,
              rgb_frame->data, rgb_frame->linesize);
    
    return rgb_frame;
}


}
