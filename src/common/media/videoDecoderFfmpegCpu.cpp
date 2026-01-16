#include <algorithm>
#include <memory>
#include <iostream>
#include <chrono>
#include <thread>
#include <string>
#include <sstream>
#include <iomanip>
#include <queue>
#include <atomic>
#include <filesystem>

#include "videoDecoderFfmpegCpu.h"
 
namespace fs = std::filesystem;

namespace emai{

static std::string pixelFormatToString(AVPixelFormat pix_fmt) {
    const char* fmt_name = av_get_pix_fmt_name(pix_fmt);
    if (fmt_name) {
        return fmt_name;
    } else {
        return "Unknown pixel format: " + std::to_string(static_cast<int>(pix_fmt));
    }
}

VideoDecoderFfmpegCpu::VideoDecoderFfmpegCpu(IDecodeEventHandle::wPtr handle) : handle_(handle)
{
    LOG_INFO("in VideoDecoderFfmpegCpu ctor");
}

VideoDecoderFfmpegCpu::~VideoDecoderFfmpegCpu()
{
    LOG_INFO("in VideoDecoderFfmpegCpu deCtor");
    unInit();
}


bool VideoDecoderFfmpegCpu::Init(const VideoInfo& info)
{
    LOG_INFO("in VideoDecoderFfmpegCpu Init, info:" << info);

    if (initOk.load())
    {
        LOG_WARN("VideoDecoderFfmpegCpu already initOk");
        return true;
    }

    if (!initJpegEncoder(info.width, info.height)) {
        LOG_ERROR("in VideoDecoderFfmpegCpu Init init_jpeg_encoder failed");
        return false;
    }

    const AVCodec *dec = avcodec_find_decoder(info.codec_id);
    if (!dec) {
        LOG_ERROR("in VideoDecoderFfmpegCpu Init avcodec_find_decoder failed, codec_id:" << info.codec_id);
        return false;
    }

    decode_ = avcodec_alloc_context3(dec);
    if (!decode_) {
        LOG_ERROR("in VideoDecoderFfmpegCpu Init avcodec_alloc_context3 failed");
        return false;
    }
    // av_codec_set_pkt_timebase(instance_, st->time_base);

    auto ret = avcodec_parameters_to_context(decode_, info.codecpar);
    if (ret < 0) {
        LOG_ERROR("in VideoDecoderFfmpegCpu Init avcodec_parameters_to_context failed, ret:" << ret);
        return false;
    }

    if (!info.extra_data.empty()) {
        decode_->extradata_size = info.extra_data.size();
        uint8_t* extradata = reinterpret_cast<uint8_t*>(malloc(decode_->extradata_size));
        memcpy(extradata, info.extra_data.data(), decode_->extradata_size);
        decode_->extradata = extradata;
    }

    decode_->flags |= AV_CODEC_FLAG_LOW_DELAY;
    decode_->flags2 |= AV_CODEC_FLAG2_FAST;
    decode_->skip_frame = AVDISCARD_NONREF;
    decode_->thread_count = 1; 

    if (avcodec_open2(decode_, dec, NULL) < 0) {
        LOG_ERROR("in VideoDecoderFfmpegCpu Init avcodec_open2 failed");
        return false;
    }

    av_frame_ = av_frame_alloc();
    if (!av_frame_) {
        LOG_ERROR("in VideoDecoderFfmpegCpu Init av_frame_alloc failed");
        return false;
    }
    eos_got_.store(0);
    eos_sent_.store(0);

    initOk.store(true);
    url_ = info.url;
    fps_ = info.fps;
    picNameHead_ = makeFilenameHead(url_) + "_";
    LOG_INFO("VideoDecoderFfmpegCpu Init success, url:" << url_);

    printStreamInfo();
    return true;
}

bool VideoDecoderFfmpegCpu::FeedPacket(const AVPacket* pkt, const int64_t frmIndex) 
{
    if (!initOk.load())
    {
        LOG_ERROR("VideoDecoderFfmpegCpu not initOk");
        return false;
    }

    if (!pkt || (pkt->size == 0 && pkt->data == NULL)) {
        LOG_ERROR("in VideoDecoderFfmpegCpu FeedPacket invalid pkt, data ptr, size:"
                << (pkt ? pkt->data : nullptr) << ", " << (pkt ? pkt->size : -1));
        return false;
    }

    int err = avcodec_send_packet(decode_, pkt);
    if (err != AVERROR(EAGAIN) && err != AVERROR_EOF && err < 0) {
        LOG_ERROR("avcodec_send_packet, err:" << err << ", EAGAIN:" << AVERROR(EAGAIN) << ", AVERROR_EOF:" << AVERROR_EOF);
        return false;
    }

    while (err >= 0){
        err = avcodec_receive_frame(decode_, av_frame_);
        if (err == AVERROR(EAGAIN)) {
            // char errbuf[101]{};
            // av_strerror(err,errbuf,100);
            // LOGW("avcodec_receive_frame, err:" << err << ",err msg:" << string(errbuf));
            break;
        } else if (err == AVERROR_EOF) {
            LOG_ERROR("avcodec_receive_frame hit EOF, err:" << err);
            //eof = true;
            break;
        } else if (err < 0) {
            LOG_ERROR("avcodec_receive_frame, err:" << err);
            break;
        }

        if (!av_frame_->pts) {
            LOG_WARN("frame pts is 0");
        } else {

            if (!av_frame_->width || !av_frame_->height)
            {
                LOG_ERROR("avframe err, width:" << av_frame_->width << ",height:" << av_frame_->height);
                continue;
            }

            LOG_DEBUG("VideoDecoderFfmpegCpu FeedPacket: Got decoded frame, pts=" << av_frame_->pts
                         << ", format=" << av_frame_->format
                         << ", width=" << av_frame_->width
                         << ", height=" << av_frame_->height);
            
            ProcessFrame(av_frame_, frmIndex);
        }

        av_frame_unref(av_frame_);
    }

    return true;
}

void VideoDecoderFfmpegCpu::FeedEos() 
{
    AVPacket packet;
    av_init_packet(&packet);
    packet.size = 0;
    packet.data = NULL;

    LOG_INFO("in VideoDecoderFfmpegCpu FeedEos Sent EOS packet to decoder");
    eos_sent_.store(1);
    // flush all frames ...
    int got_frame = 0;
    do {
    // avcodec_decode_video2(decode_, av_frame_, &got_frame, &packet);
    // if (got_frame) ProcessFrame(av_frame_);
    } while (got_frame);

    if (auto handle = handle_.lock()) {
        handle->onDecodeEos();
    }
    eos_got_.store(1);
}

int VideoDecoderFfmpegCpu::unInit() 
{
    LOG_INFO("in VideoDecoderFfmpegCpu unInit");

    if (!initOk.load())
    {
        LOG_WARN("VideoDecoderFfmpegCpu not initOk");
        return 0;
    }

    if (swsCtx_) {
        sws_freeContext(swsCtx_);
        swsCtx_ = nullptr;
    }

    if (jpegCtx_) {
        avcodec_free_context(&jpegCtx_);
        jpegCtx_ = nullptr;
    }

    if (av_frame_) {
        av_frame_free(&av_frame_);
        av_frame_ = nullptr;
    }
    if (decode_) {
        avcodec_close(decode_);
        avcodec_free_context(&decode_);
        decode_ = nullptr;
    }

    initOk.store(false);

    clrTmpPics();
    
    return 0;
}

bool VideoDecoderFfmpegCpu::ProcessFrame(AVFrame* frame, const int64_t frmIndex) 
{
    if (auto handle = handle_.lock())
    {
        if (!handle->needSnap(frmIndex))
        {
            return true;
        }

        std::string filename = generateFilename(kSavedTmpPicDir);
        std::string fileData;
        if (saveFrameAsJpeg(frame, filename, fileData)) 
        {
            addSavedPicNum(1);

            auto pData = std::make_shared<std::string>(std::move(fileData));
            handle->onSnapDone(filename, pData, frmIndex);

            keyFrmDecCtr_.haveFirstKeyFrm = false;  // 重置关键帧等待状态
        }        
    }

    return true;
}

void VideoDecoderFfmpegCpu::printStreamInfo() 
{
    if (!initOk.load())
    {
        LOG_ERROR("VideoDecoderFfmpegCpu not initOk");
        return;
    }
  
    // 获取GOP信息
    if (decode_->gop_size > 0) {
        LOG_INFO("url:" << url_ << ", GOP size: " << decode_->gop_size);
    } else {
        LOG_INFO("url:" << url_ << ", GOP size not specified (may be variable GOP)");
    }
    
    if (decode_->has_b_frames > 0) {
        LOG_INFO("url:" << url_ << ", B frame num: " << decode_->has_b_frames);
    }
   
}

bool VideoDecoderFfmpegCpu::initJpegEncoder(const int width, const int height) 
{
    LOG_INFO("in VideoDecoderFfmpegCpu initJpegEncoder, width:" << width << ", height:" << height);

    const AVCodec* jpeg_codec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
    if (!jpeg_codec) 
    {
        LOG_ERROR("in VideoDecoderFfmpegCpu initJpegEncoder avcodec_find_encoder failed");
        return false;
    }
    
    jpegCtx_ = avcodec_alloc_context3(jpeg_codec);
    if (!jpegCtx_) 
    {
        LOG_ERROR("in VideoDecoderFfmpegCpu initJpegEncoder avcodec_alloc_context3 failed");
        return false;
    }
    
    jpegCtx_->width     = width;    
    jpegCtx_->height    = height;
    jpegCtx_->pix_fmt   = AV_PIX_FMT_YUVJ420P;
    jpegCtx_->time_base = (AVRational){1, 25};
    
    int ret = -1;
    if ((ret = avcodec_open2(jpegCtx_, jpeg_codec, nullptr)) < 0) {
        LOG_ERROR("in VideoDecoderFfmpegCpu initJpegEncoder avcodec_open2 failed, ret:" << ret << ",errMsg:" << detail::avErrorToString(ret));
        avcodec_free_context(&jpegCtx_);
        return false;
    }
    
    return true;
}

bool VideoDecoderFfmpegCpu::saveFrameAsJpeg(AVFrame* frame, const std::string& filename, std::string& outFileData) 
{
    if (!jpegCtx_) 
    {
        LOG_ERROR("in VideoDecoderFfmpegCpu saveFrameAsJpeg jpegCtx_ is null");
        return false;
    }

    if (!frame)
    {
        LOG_ERROR("in VideoDecoderFfmpegCpu saveFrameAsJpeg frame is null");
        return false;
    }
    
    LOG_INFO("in VideoDecoderFfmpegCpu saveFrameAsJpeg, filename:" << filename 
                << ", frame format:" << pixelFormatToString((AVPixelFormat)frame->format)
                << ", width:" << frame->width << ", height:" << frame->height);
            
    // 确保JPEG编码器尺寸匹配
    if (jpegCtx_->width != frame->width || jpegCtx_->height != frame->height) {
        LOG_WARN("in VideoDecoderFfmpegCpu saveFrameAsJpeg resizing jpegCtx_ from "
                    << jpegCtx_->width << "x" << jpegCtx_->height << " to "
                    << frame->width << "x" << frame->height);
        
        avcodec_free_context(&jpegCtx_);
        jpegCtx_ = nullptr;

        if (!initJpegEncoder(frame->width, frame->height)) 
        {
            LOG_ERROR("in VideoDecoderFfmpegCpu saveFrameAsJpeg re-init jpegCtx_ failed");
            return false;
        }

        jpegCtx_->width = frame->width;
        jpegCtx_->height = frame->height;
    }
    
    AVFrame* yuv_frame = convert2yuv(frame);
    if (!yuv_frame) 
    {
        LOG_ERROR("in VideoDecoderFfmpegCpu saveFrameAsJpeg convert2yuv failed");
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

    LOG_INFO("in VideoDecoderFfmpegCpu saveFrameAsJpeg, url_:" << url_ << ", filename:" << filename 
                << (success ? " saved successfully." : " failed to save.") << 
                ", file data size:" << outFileData.size());    

    return success;
}

AVFrame* VideoDecoderFfmpegCpu::convert2yuv(AVFrame* frame) 
{
    if (!swsCtx_) {
        LOG_INFO("in VideoDecoderFfmpegCpu convert2yuv initializing swsCtx_");
        swsCtx_ = sws_getContext(
            frame->width, frame->height, (AVPixelFormat)frame->format,
            frame->width, frame->height, AV_PIX_FMT_YUVJ420P,
            SWS_BILINEAR, nullptr, nullptr, nullptr);
    }
    
    if (!swsCtx_) 
    {
        LOG_ERROR("in VideoDecoderFfmpegCpu convert2yuv sws_getContext failed");
        return nullptr;
    }

    AVFrame* yuv_frame = av_frame_alloc();
    yuv_frame->format = AV_PIX_FMT_YUVJ420P;
    yuv_frame->width = frame->width;
    yuv_frame->height = frame->height;
    
    if (av_frame_get_buffer(yuv_frame, 0) < 0) {
        LOG_ERROR("in VideoDecoderFfmpegCpu convert2yuv av_frame_get_buffer failed");
        av_frame_free(&yuv_frame);
        return nullptr;
    }
    
    sws_scale(swsCtx_, frame->data, frame->linesize, 0,
              frame->height, yuv_frame->data, yuv_frame->linesize);
    
    return yuv_frame;
}

std::string VideoDecoderFfmpegCpu::generateFilename(const std::string& output_dir) 
{
    std::ostringstream filename;
    #if 0
    std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm* tm = std::localtime(&now);
        
    filename << output_dir << "/capture_"
             << std::put_time(tm, "%Y%m%d_%H%M%S") << ".jpg";
    #else
    uint64_t nextSavedPicIdx = getSavedPicNum() + 1;
    int index = nextSavedPicIdx % tmpPicMaxNum_;
    filename << output_dir << "/" << picNameHead_ << std::setw(2) << std::setfill('0') << index << ".jpg";
    #endif

    ensureDirectoryExists(filename.str());

    return filename.str();
}

uint64_t VideoDecoderFfmpegCpu::getSavedPicNum() const 
{
    return savedPicNum_.load();
}

void VideoDecoderFfmpegCpu::addSavedPicNum(int num) 
{
    savedPicNum_ += num;
}


void VideoDecoderFfmpegCpu::ensureDirectoryExists(const std::string& filePath) 
{
    fs::path dir_path = fs::path(filePath).parent_path();
    if (!fs::exists(dir_path)) {
        fs::create_directories(dir_path);
    }
}

std::string VideoDecoderFfmpegCpu::makeFilenameHead(const std::string& input) 
{
    std::string result = input;
    
    // 替换不允许在文件名中使用的字符
    std::replace(result.begin(), result.end(), '/', '_');
    std::replace(result.begin(), result.end(), ':', '-');
    std::replace(result.begin(), result.end(), '.', '_');
    std::replace(result.begin(), result.end(), '?', '_');
    std::replace(result.begin(), result.end(), '&', '_');
    std::replace(result.begin(), result.end(), '=', '_');
    
    // 去除连续的两个下划线
    std::string final_result;
    bool last_was_underscore = false;
    
    for (char c : result) {
        if (c == '_') {
            if (!last_was_underscore) {
                final_result += c;
                last_was_underscore = true;
            }
            // 如果已经是下划线，跳过这个下划线
        } else {
            final_result += c;
            last_was_underscore = false;
        }
    }
    
    return final_result;
}

void VideoDecoderFfmpegCpu::clrTmpPics()
{
    std::vector<std::string> filesToDelete;

    for (int index = 0; index < tmpPicMaxNum_; ++index)
    {
        std::ostringstream filename;
        filename << kSavedTmpPicDir << "/" << picNameHead_ << std::setw(2) << std::setfill('0') << index << ".jpg";
        
        filesToDelete.push_back(filename.str());
    }

    for (const auto& filePath : filesToDelete)
    {
        fs::path file_path = fs::path(filePath);
        if (fs::exists(file_path) && fs::is_regular_file(file_path))
        {
            fs::remove(file_path);
            LOG_INFO("in VideoDecoderFfmpegCpu clrTmpPics, removed tmp pic file:" << filePath);
        }
    }
}




} // namespace emai