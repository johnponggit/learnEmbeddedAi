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
#include <vector>

#include "videoDecoderFfmpegRkmpp.h"
#include "jpegEncoderFfmpegCpu.h"

namespace fs = std::filesystem;

namespace emai{

VideoDecoderFfmpegRkmpp::VideoDecoderFfmpegRkmpp(IDecodeEventHandle::wPtr handle, bool decodeDebugEn) 
    : handle_(handle), decodeDebugEn_(decodeDebugEn)
{
    LOG_INFO("in VideoDecoderFfmpegRkmpp ctor");
}

VideoDecoderFfmpegRkmpp::~VideoDecoderFfmpegRkmpp()
{
    LOG_INFO("in VideoDecoderFfmpegRkmpp deCtor");
    unInit();
}

bool VideoDecoderFfmpegRkmpp::Init(const VideoInfo& info)
{
    LOG_INFO("in VideoDecoderFfmpegRkmpp Init, info:" << info);

    if (initOk.load())
    {
        LOG_WARN("VideoDecoderFfmpegRkmpp already initOk");
        return true;
    }

    url_ = info.url;
    fps_ = info.fps;
    picNameHead_ = makeFilenameHead(url_) + "_";

    // 尝试初始化硬件解码器
    if (!InitHardwareDecoder()) {
        LOG_WARN("Hardware decoder initialization failed, falling back to software");
        use_hardware_ = false;
        
        // 回退到软件解码器
        const AVCodec *dec = avcodec_find_decoder(info.codec_id);
        if (!dec) {
            LOG_ERROR("avcodec_find_decoder failed, codec_id:" << info.codec_id);
            return false;
        }
        
        decode_ = avcodec_alloc_context3(dec);
    }

    if (!decode_) {
        LOG_ERROR("Failed to create decoder context");
        return false;
    }

    auto ret = avcodec_parameters_to_context(decode_, info.codecpar);
    if (ret < 0) {
        LOG_ERROR("avcodec_parameters_to_context failed, ret:" << ret);
        return false;
    }

    if (!info.extra_data.empty()) {
        decode_->extradata_size = info.extra_data.size();
        uint8_t* extradata = reinterpret_cast<uint8_t*>(malloc(decode_->extradata_size));
        memcpy(extradata, info.extra_data.data(), decode_->extradata_size);
        decode_->extradata = extradata;
    }

    // 配置解码器参数
    decode_->flags |= AV_CODEC_FLAG_LOW_DELAY;
    decode_->flags2 |= AV_CODEC_FLAG2_FAST;
    decode_->skip_frame = AVDISCARD_NONREF;
    decode_->thread_count = 1;
    
    // 硬件解码器特定的设置
    if (use_hardware_) {
        decode_->hw_device_ctx = av_buffer_ref(hw_device_ctx_);
        
        // 设置硬件像素格式
        if (hw_pix_fmt_ != AV_PIX_FMT_NONE) {
            decode_->get_format = [](AVCodecContext* ctx, const enum AVPixelFormat* pix_fmts) -> enum AVPixelFormat {
                const enum AVPixelFormat* p;
                VideoDecoderFfmpegRkmpp* decoder = static_cast<VideoDecoderFfmpegRkmpp*>(ctx->opaque);
                
                for (p = pix_fmts; *p != AV_PIX_FMT_NONE; p++) {
                    if (*p == decoder->hw_pix_fmt_) {
                        LOG_DEBUG("Selected hardware pixel format: " << decoder->GetPixelFormatName(*p));
                        return *p;
                    }
                }
                
                LOG_WARN("Hardware pixel format not available, using software");
                return pix_fmts[0];
            };
            
            decode_->opaque = this;
        }
    }

    // 打开解码器
    const AVCodec* decoder = use_hardware_ ? 
        avcodec_find_decoder_by_name("h264_rkmpp") : 
        avcodec_find_decoder(info.codec_id);
        
    if (!decoder) {
        LOG_ERROR("Failed to find decoder");
        return false;
    }

    if (avcodec_open2(decode_, decoder, NULL) < 0) {
        LOG_ERROR("avcodec_open2 failed");
        return false;
    }

    // 分配帧
    hw_frame_ = av_frame_alloc();
    if (!hw_frame_) {
        LOG_ERROR("av_frame_alloc for hw_frame failed");
        return false;
    }
    
    sw_frame_ = av_frame_alloc();
    if (!sw_frame_) {
        LOG_ERROR("av_frame_alloc for sw_frame failed");
        return false;
    }

    eos_got_.store(0);
    eos_sent_.store(0);

    initOk.store(true);
    
    LOG_INFO("VideoDecoderFfmpegRkmpp Init success, url:" << url_ 
             << ", using hardware: " << (use_hardware_ ? "YES" : "NO"));

    printStreamInfo();
    if (use_hardware_) {
        printHardwareInfo();
    }
    
    return true;
}

bool VideoDecoderFfmpegRkmpp::InitHardwareDecoder()
{
    LOG_INFO("Initializing hardware decoder...");
    
    // 设置硬件设备类型为rkmpp
    hw_device_type_ = av_hwdevice_find_type_by_name("rkmpp");
    if (hw_device_type_ == AV_HWDEVICE_TYPE_NONE) {
        LOG_WARN("rkmpp hardware device type not found");
        return false;
    }
    
    LOG_INFO("Found hardware device type: " << av_hwdevice_get_type_name(hw_device_type_));
    
    // 创建硬件设备上下文
    int ret = av_hwdevice_ctx_create(&hw_device_ctx_, hw_device_type_, 
                                      nullptr, nullptr, 0);
    if (ret < 0) {
        LOG_ERROR("Failed to create hardware device context, ret: " << ret);
        return false;
    }
    
    // 查找支持硬件解码的解码器
    const AVCodec* decoder = avcodec_find_decoder_by_name("h264_rkmpp");
    if (!decoder) {
        LOG_WARN("h264_rkmpp decoder not found");
        CleanupHardwareContext();
        return false;
    }
    
    // 获取硬件像素格式
    for (int i = 0;; i++) {
        const AVCodecHWConfig* config = avcodec_get_hw_config(decoder, i);
        if (!config) {
            LOG_WARN("No hardware configuration found for decoder");
            break;
        }
        
        if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
            config->device_type == hw_device_type_) {
            hw_pix_fmt_ = config->pix_fmt;
            LOG_INFO("Found hardware pixel format: " << GetPixelFormatName(hw_pix_fmt_));
            break;
        }
    }
    
    if (hw_pix_fmt_ == AV_PIX_FMT_NONE) {
        LOG_WARN("No suitable hardware pixel format found");
        CleanupHardwareContext();
        return false;
    }
    
    // 创建解码器上下文
    decode_ = avcodec_alloc_context3(decoder);
    if (!decode_) {
        LOG_ERROR("Failed to allocate hardware decoder context");
        CleanupHardwareContext();
        return false;
    }
    
    return true;
}

bool VideoDecoderFfmpegRkmpp::FeedPacket(const AVPacket* pkt, const int64_t frmIndex) 
{
    if (!initOk.load())
    {
        LOG_ERROR("VideoDecoderFfmpegRkmpp not initOk");
        return false;
    }

    if (!pkt || (pkt->size == 0 && pkt->data == NULL)) {
        LOG_ERROR("invalid pkt, data ptr, size:"
                << (pkt ? pkt->data : nullptr) << ", " << (pkt ? pkt->size : -1));
        return false;
    }

    auto start = std::chrono::high_resolution_clock::now();

    int err = avcodec_send_packet(decode_, pkt);
    if (err != AVERROR(EAGAIN) && err != AVERROR_EOF && err < 0) {
        LOG_ERROR("avcodec_send_packet, err:" << err 
                 << ", EAGAIN:" << AVERROR(EAGAIN) 
                 << ", AVERROR_EOF:" << AVERROR_EOF);
        return false;
    }

    while (err >= 0){
        AVFrame* frame_to_process = hw_frame_;
        
        err = avcodec_receive_frame(decode_, hw_frame_);
        if (err == AVERROR(EAGAIN)) {
            break;
        } else if (err == AVERROR_EOF) {
            LOG_INFO("avcodec_receive_frame hit EOF, err:" << err);
            break;
        } else if (err < 0) {
            LOG_ERROR("avcodec_receive_frame, err:" << err);
            break;
        }

        if (!hw_frame_->pts) {
            LOG_WARN("frame pts is 0");
        } else {
            if (!hw_frame_->width || !hw_frame_->height)
            {
                LOG_ERROR("avframe err, width:" << hw_frame_->width 
                         << ",height:" << hw_frame_->height);
                continue;
            }

            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            LOG_DEBUG("VideoDecoderFfmpegRkmpp FeedPacket: Got decoded frame, pts=" << hw_frame_->pts
                         << ", format=" << hw_frame_->format
                         << ", width=" << hw_frame_->width
                         << ", height=" << hw_frame_->height
                         << ", duration=" << duration << " ms");

            // 统计
            if (hw_frame_->format == hw_pix_fmt_) {
                hw_frame_count_++;
                LOG_DEBUG("Got hardware frame, pts=" << hw_frame_->pts
                         << ", format=" << GetPixelFormatName((AVPixelFormat)hw_frame_->format)
                         << ", width=" << hw_frame_->width
                         << ", height=" << hw_frame_->height);
                
                // 如果是硬件帧，需要转换为软件帧进行处理
                AVFrame* sw_frame = TransferHwFrameToSw(hw_frame_);
                if (sw_frame) {
                    frame_to_process = sw_frame;
                    sw_frame_count_++;
                } else {
                    LOG_ERROR("Failed to transfer hardware frame to software");
                    continue;
                }
            } else {
                sw_frame_count_++;
                LOG_DEBUG("Got software frame, pts=" << hw_frame_->pts
                         << ", format=" << GetPixelFormatName((AVPixelFormat)hw_frame_->format));
            }

            auto end1 = std::chrono::high_resolution_clock::now();
            auto duration1 = std::chrono::duration_cast<std::chrono::milliseconds>(end1 - end).count();
            LOG_DEBUG("VideoDecoderFfmpegRkmpp FeedPacket: to ProcessFrame, duration1 =" << duration1 << " ms");
            
            ProcessFrame(frame_to_process, frmIndex);
            
            // 清理转换的帧
            if (frame_to_process != hw_frame_) {
                av_frame_unref(sw_frame_);
            }
        }

        av_frame_unref(hw_frame_);
    }

    return true;
}

AVFrame* VideoDecoderFfmpegRkmpp::TransferHwFrameToSw(AVFrame* hw_frame)
{
    if (!hw_frame || hw_frame->format != hw_pix_fmt_) {
        return hw_frame; // 如果不是硬件帧，直接返回
    }
    
    int ret = av_hwframe_transfer_data(sw_frame_, hw_frame, 0);
    if (ret < 0) {
        LOG_ERROR("Failed to transfer data from hardware to software, ret: " << ret);
        return nullptr;
    }
    
    // 复制其他字段
    sw_frame_->pts = hw_frame->pts;
    sw_frame_->pkt_dts = hw_frame->pkt_dts;
    sw_frame_->best_effort_timestamp = hw_frame->best_effort_timestamp;
    
    return sw_frame_;
}

void VideoDecoderFfmpegRkmpp::FeedEos() 
{
    AVPacket packet;
    av_init_packet(&packet);
    packet.size = 0;
    packet.data = NULL;

    LOG_INFO("in VideoDecoderFfmpegRkmpp FeedEos Sent EOS packet to decoder");
    eos_sent_.store(1);
    
    // 发送空包刷新解码器
    int ret = avcodec_send_packet(decode_, &packet);
    if (ret < 0 && ret != AVERROR_EOF) {
        LOG_ERROR("Failed to send flush packet, ret: " << ret);
    }
    
    // 接收所有剩余的帧
    while (true) {
        ret = avcodec_receive_frame(decode_, hw_frame_);
        if (ret == AVERROR_EOF) {
            LOG_INFO("Decoder fully flushed");
            break;
        } else if (ret < 0) {
            LOG_ERROR("Error receiving frame during flush, ret: " << ret);
            break;
        }
        
        // 处理剩余的帧
        AVFrame* frame_to_process = hw_frame_;
        if (hw_frame_->format == hw_pix_fmt_) {
            AVFrame* sw_frame = TransferHwFrameToSw(hw_frame_);
            if (sw_frame) {
                frame_to_process = sw_frame;
            }
        }
        
        if (frame_to_process) {
            ProcessFrame(frame_to_process, -1);
            
            if (frame_to_process != hw_frame_) {
                av_frame_unref(sw_frame_);
            }
        }
        
        av_frame_unref(hw_frame_);
    }

    if (auto handle = handle_.lock()) {
        handle->onDecodeEos();
    }
    eos_got_.store(1);
    
    LOG_INFO("Hardware decoder statistics - HW frames: " << hw_frame_count_.load()
             << ", SW frames: " << sw_frame_count_.load());
}

int VideoDecoderFfmpegRkmpp::unInit() 
{
    LOG_INFO("in VideoDecoderFfmpegRkmpp unInit");

    if (!initOk.load())
    {
        LOG_WARN("VideoDecoderFfmpegRkmpp not initOk");
        return 0;
    }

    if (hw_frame_) {
        av_frame_free(&hw_frame_);
        hw_frame_ = nullptr;
    }
    
    if (sw_frame_) {
        av_frame_free(&sw_frame_);
        sw_frame_ = nullptr;
    }
    
    if (decode_) {
        avcodec_close(decode_);
        avcodec_free_context(&decode_);
        decode_ = nullptr;
    }
    
    CleanupHardwareContext();

    initOk.store(false);
    
    clrTmpPics();
    
    return 0;
}

void VideoDecoderFfmpegRkmpp::CleanupHardwareContext()
{
    if (hw_device_ctx_) {
        av_buffer_unref(&hw_device_ctx_);
        hw_device_ctx_ = nullptr;
    }
    
    hw_device_type_ = AV_HWDEVICE_TYPE_NONE;
    hw_pix_fmt_ = AV_PIX_FMT_NONE;
    use_hardware_ = false;
}

bool VideoDecoderFfmpegRkmpp::ProcessFrame(AVFrame* frame, const int64_t frmIndex) 
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
                    LOG_ERROR("in VideoDecoderFfmpegRkmpp ProcessFrame jpegEncoder_ init failed");
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

void VideoDecoderFfmpegRkmpp::printStreamInfo() 
{
    if (!initOk.load())
    {
        LOG_ERROR("VideoDecoderFfmpegRkmpp not initOk");
        return;
    }
  
    if (decode_->gop_size > 0) {
        LOG_INFO("url:" << url_ << ", GOP size: " << decode_->gop_size);
    } else {
        LOG_INFO("url:" << url_ << ", GOP size not specified (may be variable GOP)");
    }
    
    if (decode_->has_b_frames > 0) {
        LOG_INFO("url:" << url_ << ", B frame num: " << decode_->has_b_frames);
    }
}

void VideoDecoderFfmpegRkmpp::printHardwareInfo()
{
    if (!use_hardware_) {
        return;
    }
    
    LOG_INFO("=== Hardware Decoder Information ===");
    LOG_INFO("Device type: " << av_hwdevice_get_type_name(hw_device_type_));
    LOG_INFO("Hardware pixel format: " << GetPixelFormatName(hw_pix_fmt_));
    
    if (hw_device_ctx_) {
        AVHWDeviceContext* device_ctx = (AVHWDeviceContext*)hw_device_ctx_->data;
        if (device_ctx && device_ctx->hwctx) {
            LOG_INFO("Hardware context type: " << device_ctx->type);
        }
    }
    
    // 查询支持的硬件配置
    const AVCodec* decoder = decode_->codec;
    if (decoder) {
        LOG_INFO("Decoder name: " << decoder->name);
        LOG_INFO("Decoder long name: " << decoder->long_name);
        
        for (int i = 0;; i++) {
            const AVCodecHWConfig* config = avcodec_get_hw_config(decoder, i);
            if (!config) break;
            
            LOG_INFO("  Config " << i << ": device_type=" 
                     << av_hwdevice_get_type_name(config->device_type)
                     << ", pix_fmt=" << GetPixelFormatName(config->pix_fmt)
                     << ", methods=" << config->methods);
        }
    }
    
    LOG_INFO("===================================");
}

const char* VideoDecoderFfmpegRkmpp::GetPixelFormatName(enum AVPixelFormat pix_fmt)
{
    const char* name = av_get_pix_fmt_name(pix_fmt);
    return name ? name : "unknown";
}

std::string VideoDecoderFfmpegRkmpp::generateFilename(const std::string& output_dir) 
{
    std::ostringstream filename;
    uint64_t nextSavedPicIdx = getSavedPicNum() + 1;
    int index = nextSavedPicIdx % tmpPicMaxNum_;
    filename << output_dir << "/" << picNameHead_ << std::setw(2) << std::setfill('0') << index << ".jpg";

    ensureDirectoryExists(filename.str());

    return filename.str();
}

uint64_t VideoDecoderFfmpegRkmpp::getSavedPicNum() const 
{
    return savedPicNum_.load();
}

void VideoDecoderFfmpegRkmpp::addSavedPicNum(int num) 
{
    savedPicNum_ += num;
}

void VideoDecoderFfmpegRkmpp::ensureDirectoryExists(const std::string& filePath) 
{
    fs::path dir_path = fs::path(filePath).parent_path();
    if (!fs::exists(dir_path)) {
        fs::create_directories(dir_path);
    }
}

std::string VideoDecoderFfmpegRkmpp::makeFilenameHead(const std::string& input) 
{
    std::string result = input;
    
    std::replace(result.begin(), result.end(), '/', '_');
    std::replace(result.begin(), result.end(), ':', '-');
    std::replace(result.begin(), result.end(), '.', '_');
    std::replace(result.begin(), result.end(), '?', '_');
    std::replace(result.begin(), result.end(), '&', '_');
    std::replace(result.begin(), result.end(), '=', '_');
    
    std::string final_result;
    bool last_was_underscore = false;
    
    for (char c : result) {
        if (c == '_') {
            if (!last_was_underscore) {
                final_result += c;
                last_was_underscore = true;
            }
        } else {
            final_result += c;
            last_was_underscore = false;
        }
    }
    
    return final_result;
}

void VideoDecoderFfmpegRkmpp::clrTmpPics()
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
            LOG_INFO("in VideoDecoderFfmpegRkmpp clrTmpPics, removed tmp pic file:" << filePath);
        }
    }
}

} // namespace emai