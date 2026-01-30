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
#include "jpegEncoderFfmpegCpu.h"

namespace fs = std::filesystem;

namespace emai{


VideoDecoderFfmpegCpu::VideoDecoderFfmpegCpu(IDecodeEventHandle::wPtr handle, bool decodeDebugEn) : handle_(handle), decodeDebugEn_(decodeDebugEn)
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

    auto end = std::chrono::high_resolution_clock::now();
    LOG_DEBUG("FeedPacket duration total: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - feedPacketFrameTime_).count() << " ms");
    feedPacketFrameTime_ = std::chrono::high_resolution_clock::now();

    if (!pkt || (pkt->size == 0 && pkt->data == NULL)) {
        LOG_ERROR("in VideoDecoderFfmpegCpu FeedPacket invalid pkt, data ptr, size:"
                << (pkt ? pkt->data : nullptr) << ", " << (pkt ? pkt->size : -1));
        return false;
    }

    auto start = std::chrono::high_resolution_clock::now();

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

            auto end = std::chrono::high_resolution_clock::now();
            LOG_DEBUG("VideoDecoderFfmpegCpu FeedPacket: Got decoded frame, pts=" << av_frame_->pts
                         << ", format=" << av_frame_->format
                         << ", width=" << av_frame_->width
                         << ", height=" << av_frame_->height);
            LOG_DEBUG("FeedPacket duration decode: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " ms");
 
                     
            auto start1 = std::chrono::high_resolution_clock::now();
            ProcessFrame(av_frame_, frmIndex); 
            auto end1 = std::chrono::high_resolution_clock::now();
            LOG_DEBUG("FeedPacket duration ProcessFrame: " << std::chrono::duration_cast<std::chrono::milliseconds>(end1 - start1).count() << " ms");
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
        /* for debug */
        if (decodeDebugEn_) 
        {
            std::string filename = generateFilename(kSavedTmpPicDir);
            std::string fileData;

            if (!jpegEncoder_) 
            {
                jpegEncoder_ = std::make_shared<emai::JpegEncoderFfmpegCpu>();

                if (!jpegEncoder_ || !jpegEncoder_->Init(frame->width, frame->height))
                {
                    jpegEncoder_.reset();
                    LOG_ERROR("in VideoDecoderFfmpegCpu ProcessFrame jpegEncoder_ init failed");
                }
            }                 

            if (jpegEncoder_->saveFrameAsJpeg(frame, filename, fileData)) 
            {
                addSavedPicNum(1);
            }  
        }              

        handle->onDecodeFrame(frame, frmIndex);
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