#include "MppH264Encoder.h"

#include <chrono>
#include <cstring>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <atomic>
#include <dlfcn.h>

// Rockchip MPP头文件
#ifdef __cplusplus
extern "C" {
#endif

#include <rockchip/rk_mpi.h>

#ifdef __cplusplus
}
#endif

#include "util.h"

namespace emai {

// 检查MPP编码器是否可用
bool MppH264Encoder::IsMppAvailable() {
    // 尝试加载MPP库
    void* handle = dlopen("librockchip_mpp.so", RTLD_LAZY);
    if (!handle) {
        // 尝试其他可能的库名
        handle = dlopen("librk_mpp.so", RTLD_LAZY);
        if (!handle) {
            handle = dlopen("libmpp.so", RTLD_LAZY);
            if (!handle) {
                LOG_ERROR("MPP library not found: " << dlerror());
                return false;
            }
        }
    }
    
    dlclose(handle);
    return true;
}

// 构造函数
MppH264Encoder::MppH264Encoder() :
    mpp_ctx_(nullptr),
    mpp_api_(nullptr),
    buffer_group_(nullptr),
    sws_ctx_(nullptr),
    scaled_frame_(nullptr),
    init_ok_(false),
    low_memory_mode_(true),
    frame_count_(0),
    total_bytes_(0),
    total_encode_time_us_(0),
    last_input_format_(AV_PIX_FMT_NONE),
    last_input_width_(0),
    last_input_height_(0),
    pts_counter_(0) {
    
    LOG_INFO("MppH264Encoder created");
}

// 析构函数
MppH264Encoder::~MppH264Encoder() {
    Cleanup();
    LOG_INFO("MppH264Encoder destroyed");
}

// 完整初始化接口
bool MppH264Encoder::Init(const MppEncoderConfig& config) {
    if (init_ok_) {
        LOG_WARN("Encoder already initialized, cleaning up first");
        Cleanup();
    }
    
    config_ = config;
    low_memory_mode_ = config.low_memory_mode;
    
    LOG_INFO("Initializing MPP H.264 Encoder with config:");
    LOG_INFO("  Input: " << config.src_width << "x" << config.src_height);
    LOG_INFO("  Output: " << config.dst_width << "x" << config.dst_height);
    LOG_INFO("  FPS: " << config.fps << ", Bitrate: " << config.bitrate_bps << " bps");
    LOG_INFO("  GOP: " << config.gop_size);
    LOG_INFO("  Pixel format: " << config.pixel_format);
    LOG_INFO("  Low memory mode: " << (low_memory_mode_ ? "yes" : "no"));
    
    // 检查配置合理性
    if (config.src_width <= 0 || config.src_height <= 0 ||
        config.dst_width <= 0 || config.dst_height <= 0) {
        LOG_ERROR("Invalid resolution configuration");
        return false;
    }
    
    if (config.fps <= 0 || config.fps > 60) {
        LOG_ERROR("Invalid FPS: " << config.fps);
        return false;
    }
    
    // 检查是否支持像素格式
    if (config.pixel_format != "nv12" && config.pixel_format != "yuv420p") {
        LOG_ERROR("Unsupported pixel format: " << config.pixel_format);
        return false;
    }
    
    // 检查MPP是否可用
    if (!IsMppAvailable()) {
        LOG_ERROR("MPP library not available");
        return false;
    }
    
    // 初始化MPP编码器
    if (!InitMppEncoder()) {
        LOG_ERROR("Failed to initialize MPP encoder");
        Cleanup();
        return false;
    }
    
    // 如果需要缩放，初始化软件缩放器
    if (config.src_width != config.dst_width || 
        config.src_height != config.dst_height) {
        if (!InitSoftwareScaler()) {
            LOG_ERROR("Failed to initialize software scaler");
            Cleanup();
            return false;
        }
    }
    
    // 分配内存缓冲区
    if (!AllocateBuffers()) {
        LOG_ERROR("Failed to allocate buffers");
        Cleanup();
        return false;
    }
    
    init_ok_ = true;
    pts_counter_ = 0;
    
     // 强制编码一帧以获取SPS/PPS
    if (init_ok_) {
        // 创建一个空帧来获取SPS/PPS
        AVFrame* dummy_frame = av_frame_alloc();
        if (dummy_frame) {
            dummy_frame->format = AV_PIX_FMT_NV12;
            dummy_frame->width = config_.dst_width;
            dummy_frame->height = config_.dst_height;
            dummy_frame->pts = 0;
            
            if (av_frame_get_buffer(dummy_frame, 32) >= 0) {
                // 填充黑色背景
                memset(dummy_frame->data[0], 0, config_.dst_width * config_.dst_height);
                memset(dummy_frame->data[1], 128, config_.dst_width * config_.dst_height / 2);
                
                std::vector<uint8_t> dummy_data;
                if (EncodeFrame(dummy_frame, dummy_data)) {
                    LOG_INFO("Encoded dummy frame to extract SPS/PPS");
                }
            }
            av_frame_free(&dummy_frame);
        }
    }

    LOG_INFO("MPP H.264 encoder initialized successfully");
    return true;
}

// 简化初始化接口
bool MppH264Encoder::Init(int src_width, int src_height, int dst_width, int dst_height,
                         int fps, int bitrate_bps, bool low_memory_mode) {
    MppEncoderConfig config;
    config.src_width = src_width;
    config.src_height = src_height;
    config.dst_width = dst_width;
    config.dst_height = dst_height;
    config.fps = fps;
    config.bitrate_bps = bitrate_bps;
    config.gop_size = fps * 2; // 默认2秒一个关键帧
    config.buffer_count = low_memory_mode ? 4 : 8;
    config.low_memory_mode = low_memory_mode;
    config.pixel_format = "nv12"; // MPP默认使用NV12
    
    return Init(config);
}

// 计算缓冲区大小
size_t MppH264Encoder::CalculateBufferSize() const {
    int hor_stride = RK_ALIGN(config_.dst_width, 16);
    int ver_stride = RK_ALIGN(config_.dst_height, 16);
    return hor_stride * ver_stride * 3 / 2; // NV12格式
}

// 初始化MPP编码器
bool MppH264Encoder::InitMppEncoder() {
    MPP_RET ret;
    
    // 创建MPP上下文
    MppCtx ctx = nullptr;
    MppApi* api = nullptr;
    ret = mpp_create(&ctx, &api);
    if (ret != MPP_OK) {
        LOG_ERROR("Failed to create MPP context: " << ret);
        return false;
    }
    
    // 存储上下文指针
    mpp_ctx_ = reinterpret_cast<void*>(ctx);
    mpp_api_ = reinterpret_cast<void*>(api);
    
    // 初始化编码器
    MppCodingType codec_type = MPP_VIDEO_CodingAVC; // H.264
    ret = mpp_init(ctx, MPP_CTX_ENC, codec_type);
    if (ret != MPP_OK) {
        LOG_ERROR("Failed to init MPP encoder: " << ret);
        mpp_destroy(ctx);
        mpp_ctx_ = nullptr;
        mpp_api_ = nullptr;
        return false;
    }
    
    // 创建编码器配置
    MppEncCfg enc_cfg = nullptr;
    ret = mpp_enc_cfg_init(&enc_cfg);
    if (ret != MPP_OK) {
        LOG_ERROR("Failed to init encoder config: " << ret);
        mpp_destroy(ctx);
        mpp_ctx_ = nullptr;
        mpp_api_ = nullptr;
        return false;
    }
    
    // 设置编码参数
    int hor_stride = RK_ALIGN(config_.dst_width, 16);
    int ver_stride = RK_ALIGN(config_.dst_height, 16);
    
    // 基础配置
    mpp_enc_cfg_set_s32(enc_cfg, "prep:width", config_.dst_width);
    mpp_enc_cfg_set_s32(enc_cfg, "prep:height", config_.dst_height);
    mpp_enc_cfg_set_s32(enc_cfg, "prep:hor_stride", hor_stride);
    mpp_enc_cfg_set_s32(enc_cfg, "prep:ver_stride", ver_stride);
    mpp_enc_cfg_set_s32(enc_cfg, "prep:format", MPP_FMT_YUV420SP); // NV12格式
    
    // 码率控制
    if (low_memory_mode_) {
        mpp_enc_cfg_set_s32(enc_cfg, "rc:mode", MPP_ENC_RC_MODE_VBR);
    } else {
        mpp_enc_cfg_set_s32(enc_cfg, "rc:mode", MPP_ENC_RC_MODE_CBR);
    }
    
    mpp_enc_cfg_set_s32(enc_cfg, "rc:bps_target", config_.bitrate_bps);
    mpp_enc_cfg_set_s32(enc_cfg, "rc:bps_max", config_.bitrate_bps * 3 / 2);
    mpp_enc_cfg_set_s32(enc_cfg, "rc:bps_min", config_.bitrate_bps / 2);
    
    // 帧率配置
    mpp_enc_cfg_set_s32(enc_cfg, "rc:fps_in_flex", 0);
    mpp_enc_cfg_set_s32(enc_cfg, "rc:fps_in_num", config_.fps);
    mpp_enc_cfg_set_s32(enc_cfg, "rc:fps_in_denorm", 1);
    mpp_enc_cfg_set_s32(enc_cfg, "rc:fps_out_flex", 0);
    mpp_enc_cfg_set_s32(enc_cfg, "rc:fps_out_num", config_.fps);
    mpp_enc_cfg_set_s32(enc_cfg, "rc:fps_out_denorm", 1);
    
    // GOP配置
    mpp_enc_cfg_set_s32(enc_cfg, "rc:gop", config_.gop_size);
    
    // 质量参数
    mpp_enc_cfg_set_s32(enc_cfg, "codec:qp_init", 26);
    mpp_enc_cfg_set_s32(enc_cfg, "codec:qp_max", 51);
    mpp_enc_cfg_set_s32(enc_cfg, "codec:qp_min", 10);
    mpp_enc_cfg_set_s32(enc_cfg, "codec:qp_step", 4);
    
    // 低内存模式优化
    if (low_memory_mode_) {
        LOG_INFO("Applying low memory mode optimizations");
        mpp_enc_cfg_set_s32(enc_cfg, "codec:ref_frm_cfg", 1);  // 减少参考帧
        mpp_enc_cfg_set_s32(enc_cfg, "prep:buf_cnt", 2);       // 减少缓冲区数量
        mpp_enc_cfg_set_s32(enc_cfg, "codec:max_i_interval", config_.gop_size);
    } else {
        mpp_enc_cfg_set_s32(enc_cfg, "codec:ref_frm_cfg", 2);
        mpp_enc_cfg_set_s32(enc_cfg, "prep:buf_cnt", config_.buffer_count);
    }
    
    // 应用配置
    ret = api->control(ctx, MPP_ENC_SET_CFG, enc_cfg);
    mpp_enc_cfg_deinit(enc_cfg);
    
    if (ret != MPP_OK) {
        LOG_ERROR("Failed to set encoder config: " << ret);
        mpp_destroy(ctx);
        mpp_ctx_ = nullptr;
        mpp_api_ = nullptr;
        return false;
    }
    
    LOG_INFO("MPP encoder initialized successfully");
    return true;
}

// 初始化软件缩放器
bool MppH264Encoder::InitSoftwareScaler() {
    LOG_INFO("Initializing software scaler: " << config_.src_width << "x" << config_.src_height << " -> " << config_.dst_width << "x" << config_.dst_height);
    
    // 分配缩放后的帧
    scaled_frame_ = av_frame_alloc();
    if (!scaled_frame_) {
        LOG_ERROR("Failed to allocate scaled frame");
        return false;
    }
    
    scaled_frame_->format = AV_PIX_FMT_NV12; // MPP需要NV12格式
    scaled_frame_->width = config_.dst_width;
    scaled_frame_->height = config_.dst_height;
    
    if (av_frame_get_buffer(scaled_frame_, 32) < 0) {
        LOG_ERROR("Failed to allocate scaled frame buffer");
        av_frame_free(&scaled_frame_);
        return false;
    }
    
    LOG_INFO("Software scaler initialized successfully");
    return true;
}

// 分配内存缓冲区
bool MppH264Encoder::AllocateBuffers() {
    MPP_RET ret;
    
    // 计算缓冲区大小
    size_t buffer_size = CalculateBufferSize();
    
    // 创建缓冲区组
    MppBufferGroup group = nullptr;
    ret = mpp_buffer_group_get_internal(&group, MPP_BUFFER_TYPE_ION);
    if (ret != MPP_OK) {
        LOG_ERROR("Failed to create buffer group: " << ret);
        return false;
    }
    
    // 存储缓冲区组指针
    buffer_group_ = reinterpret_cast<void*>(group);
    
    // 预分配缓冲区
    int buffer_count = low_memory_mode_ ? 2 : config_.buffer_count;
    
    for (int i = 0; i < buffer_count; i++) {
        MppBuffer buffer = nullptr;
        ret = mpp_buffer_get(group, &buffer, buffer_size);
        if (ret != MPP_OK) {
            LOG_WARN("Failed to allocate buffer " << i << ", continuing with " << i << " buffers");
            break;
        }
        LOG_DEBUG("Allocated buffer " << i << " size: " << buffer_size << " bytes");
    }
    
    LOG_INFO("Allocated " << buffer_count << " buffers, " << buffer_size << " bytes each");
    return true;
}

// 编码帧
bool MppH264Encoder::EncodeFrame(AVFrame* frame, std::vector<uint8_t>& encoded_data) {
    if (!init_ok_) {
        LOG_ERROR("Encoder not initialized");
        return false;
    }
    
    if (!frame) {
        LOG_ERROR("Input frame is null");
        return false;
    }
    
    auto encode_start = std::chrono::high_resolution_clock::now();
    
    // 验证输入帧
    if (frame->width <= 0 || frame->height <= 0) {
        LOG_ERROR("Invalid frame dimensions: " << frame->width << "x" << frame->height);
        return false;
    }
    
    AVPixelFormat input_format = (AVPixelFormat)frame->format;
    bool need_scaling = (scaled_frame_ != nullptr);
    
    LOG_DEBUG("Encoding frame: " << frame->width << "x" << frame->height << " (" << av_get_pix_fmt_name(input_format) << "), need scaling: " << (need_scaling ? "yes" : "no"));
    
    // 处理时间戳
    if (frame->pts == AV_NOPTS_VALUE) {
        frame->pts = pts_counter_++;
    }
    
    bool encode_success = false;
    
    try {
        if (need_scaling) {
            encode_success = EncodeWithScaling(frame, input_format, encoded_data);
        } else {
            if (input_format == AV_PIX_FMT_NV12 || input_format == AV_PIX_FMT_NV21) {
                encode_success = EncodeNV12Frame(frame, encoded_data);
            } else if (input_format == AV_PIX_FMT_YUV420P) {
                encode_success = EncodeYUV420PFrame(frame, encoded_data);
            } else {
                LOG_ERROR("Unsupported pixel format: " << av_get_pix_fmt_name(input_format));
                return false;
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Exception during encoding: " << e.what());
        return false;
    }
    
    auto encode_end = std::chrono::high_resolution_clock::now();
    auto encode_time = std::chrono::duration_cast<std::chrono::microseconds>(
        encode_end - encode_start).count();
    
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        total_encode_time_us_ += encode_time;
    }
    
    if (encode_success) {
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            frame_count_++;
        }
        LOG_DEBUG("Encoded frame " << frame_count_ << ", size: " << encoded_data.size() << " bytes, time: " << encode_time << "ms");
    }
    
    return encode_success;
}

// 带缩放的编码
bool MppH264Encoder::EncodeWithScaling(AVFrame* frame, AVPixelFormat input_format,
                                      std::vector<uint8_t>& out_data) {
    // 创建或更新SWS上下文
    if (!sws_ctx_ || 
        input_format != last_input_format_ ||
        frame->width != last_input_width_ ||
        frame->height != last_input_height_) {
        
        if (sws_ctx_) {
            sws_freeContext(sws_ctx_);
            sws_ctx_ = nullptr;
        }
        
        // 创建新的SWS上下文
        sws_ctx_ = sws_getContext(
            frame->width, frame->height, input_format,
            config_.dst_width, config_.dst_height, AV_PIX_FMT_NV12,
            SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
        
        if (!sws_ctx_) {
            LOG_ERROR("Failed to create SWS context for scaling");
            return false;
        }
        
        last_input_format_ = input_format;
        last_input_width_ = frame->width;
        last_input_height_ = frame->height;
        
        LOG_DEBUG("Created new SWS context");
    }
    
    // 执行缩放和格式转换
    sws_scale(sws_ctx_,
             frame->data, frame->linesize,
             0, frame->height,
             scaled_frame_->data, scaled_frame_->linesize);
    
    // 设置时间戳
    scaled_frame_->pts = frame->pts;
    
    // 编码缩放后的帧
    return EncodeNV12Frame(scaled_frame_, out_data);
}

// YUV420P转NV12
void MppH264Encoder::ConvertYUV420PToNV12(const AVFrame* src, uint8_t* dst) const {
    int width = config_.dst_width;
    int height = config_.dst_height;
    
    // 复制Y平面
    if (src->linesize[0] == width) {
        memcpy(dst, src->data[0], width * height);
    } else {
        for (int i = 0; i < height; i++) {
            memcpy(dst + i * width, src->data[0] + i * src->linesize[0], width);
        }
    }
    
    dst += width * height;
    
    // 合并U和V平面为NV12格式
    int uv_width = width;
    int uv_height = height / 2;
    
    for (int i = 0; i < uv_height; i++) {
        for (int j = 0; j < uv_width; j += 2) {
            // 从U平面取数据
            uint8_t u = src->data[1][i * src->linesize[1] + (j / 2)];
            // 从V平面取数据
            uint8_t v = src->data[2][i * src->linesize[2] + (j / 2)];
            
            // 写入NV12格式：UV交错
            dst[i * uv_width + j] = u;
            dst[i * uv_width + j + 1] = v;
        }
    }
}

// 发送帧到MPP编码器
bool MppH264Encoder::SendFrameToMpp(void* yuv_data, int width, int height, int64_t pts) {
    if (!mpp_ctx_ || !mpp_api_) {
        LOG_ERROR("MPP context not initialized");
        return false;
    }
    
    MppCtx ctx = reinterpret_cast<MppCtx>(mpp_ctx_);
    MppApi* api = reinterpret_cast<MppApi*>(mpp_api_);
    MppFrame mpp_frame = nullptr;
    MPP_RET ret;
    
    // 创建MPP帧
    ret = mpp_frame_init(&mpp_frame);
    if (ret != MPP_OK) {
        LOG_ERROR("Failed to init MPP frame: " << ret);
        return false;
    }
    
    // 设置帧参数
    mpp_frame_set_width(mpp_frame, width);
    mpp_frame_set_height(mpp_frame, height);
    mpp_frame_set_hor_stride(mpp_frame, RK_ALIGN(width, 16));
    mpp_frame_set_ver_stride(mpp_frame, RK_ALIGN(height, 16));
    mpp_frame_set_fmt(mpp_frame, MPP_FMT_YUV420SP);
    mpp_frame_set_pts(mpp_frame, pts);
    mpp_frame_set_eos(mpp_frame, 0);
    
    // 获取缓冲区
    size_t buffer_size = CalculateBufferSize();
    MppBuffer mpp_buffer = nullptr;
    MppBufferGroup group = reinterpret_cast<MppBufferGroup>(buffer_group_);
    
    ret = mpp_buffer_get(group, &mpp_buffer, buffer_size);
    if (ret != MPP_OK) {
        LOG_ERROR("Failed to get MPP buffer: " << ret);
        mpp_frame_deinit(&mpp_frame);
        return false;
    }
    
    // 复制数据
    void* buffer_ptr = mpp_buffer_get_ptr(mpp_buffer);
    if (!buffer_ptr) {
        LOG_ERROR("Failed to get buffer pointer");
        mpp_buffer_put(mpp_buffer);
        mpp_frame_deinit(&mpp_frame);
        return false;
    }
    
    memcpy(buffer_ptr, yuv_data, buffer_size);
    mpp_frame_set_buffer(mpp_frame, mpp_buffer);
    mpp_buffer_put(mpp_buffer); // 减少引用计数，MPP帧现在持有引用
    
    // 发送到编码器
    ret = api->encode_put_frame(ctx, mpp_frame);
    mpp_frame_deinit(&mpp_frame); // 释放帧，编码器内部会复制数据
    
    if (ret != MPP_OK) {
        LOG_ERROR("Failed to put frame to encoder: " << ret);
        return false;
    }
    
    return true;
}

// 从MPP编码器接收包并提取SPS/PPS
bool MppH264Encoder::ReceivePacketFromMpp(std::vector<uint8_t>& out_data) {
    if (!mpp_ctx_ || !mpp_api_) {
        LOG_ERROR("MPP context not initialized");
        return false;
    }
    
    MppCtx ctx = reinterpret_cast<MppCtx>(mpp_ctx_);
    MppApi* api = reinterpret_cast<MppApi*>(mpp_api_);
    MppPacket mpp_packet = nullptr;
    MPP_RET ret;
    bool got_packet = false;
    
    out_data.clear();
    
    while (true) {
        ret = api->encode_get_packet(ctx, &mpp_packet);
        if (ret == MPP_ERR_TIMEOUT) {
            // 编码器需要更多输入
            break;
        }
        
        if (ret != MPP_OK) {
            LOG_ERROR("Failed to get packet from encoder: " << ret);
            break;
        }
        
        if (!mpp_packet) {
            break;
        }
        
        // 检查是否为SPS/PPS包
        if (!sps_pps_extracted_) {
            ExtractSpsPpsFromPacket(mpp_packet);
        }
        
        // 获取编码数据
        void* packet_data = mpp_packet_get_data(mpp_packet);
        size_t packet_size = mpp_packet_get_length(mpp_packet);
        
        if (packet_data && packet_size > 0) {
            out_data.insert(out_data.end(),
                           static_cast<uint8_t*>(packet_data),
                           static_cast<uint8_t*>(packet_data) + packet_size);
            got_packet = true;
            
            {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                total_bytes_ += packet_size;
            }
        }
        
        mpp_packet_deinit(&mpp_packet);
    }
    
    return got_packet;
}

// 编码NV12帧
bool MppH264Encoder::EncodeNV12Frame(AVFrame* frame, std::vector<uint8_t>& out_data) {
    // 准备数据缓冲区
    size_t buffer_size = CalculateBufferSize();
    std::vector<uint8_t> nv12_data(buffer_size);
    
    uint8_t* dst = nv12_data.data();
    int width = config_.dst_width;
    int height = config_.dst_height;
    
    // 复制Y平面
    if (frame->linesize[0] == width) {
        memcpy(dst, frame->data[0], width * height);
        dst += width * height;
    } else {
        for (int i = 0; i < height; i++) {
            memcpy(dst + i * width, frame->data[0] + i * frame->linesize[0], width);
        }
        dst += width * height;
    }
    
    // 复制UV平面 (NV12: UV交错)
    int uv_height = height / 2;
    if (frame->linesize[1] == width) {
        memcpy(dst, frame->data[1], width * uv_height);
    } else {
        for (int i = 0; i < uv_height; i++) {
            memcpy(dst + i * width, frame->data[1] + i * frame->linesize[1], width);
        }
    }
    
    // 发送到MPP编码器
    if (!SendFrameToMpp(nv12_data.data(), width, height, frame->pts)) {
        return false;
    }
    
    // 接收编码数据
    return ReceivePacketFromMpp(out_data);
}

// 编码YUV420P帧
bool MppH264Encoder::EncodeYUV420PFrame(AVFrame* frame, std::vector<uint8_t>& out_data) {
    // YUV420P需要转换为NV12
    size_t buffer_size = CalculateBufferSize();
    std::vector<uint8_t> nv12_data(buffer_size);
    
    // 转换格式
    ConvertYUV420PToNV12(frame, nv12_data.data());
    
    // 发送到MPP编码器
    if (!SendFrameToMpp(nv12_data.data(), config_.dst_width, config_.dst_height, frame->pts)) {
        return false;
    }
    
    // 接收编码数据
    return ReceivePacketFromMpp(out_data);
}

// 刷新编码器
bool MppH264Encoder::Flush(std::vector<uint8_t>& encoded_data) {
    if (!init_ok_) {
        LOG_ERROR("Encoder not initialized");
        return false;
    }
    
    LOG_INFO("Flushing encoder...");
    
    MppCtx ctx = reinterpret_cast<MppCtx>(mpp_ctx_);
    MppApi* api = reinterpret_cast<MppApi*>(mpp_api_);
    
    // MPP编码器通常不需要显式刷新
    // 只需发送空帧即可
    MPP_RET ret = api->encode_put_frame(ctx, nullptr);
    if (ret != MPP_OK) {
        LOG_ERROR("Failed to send flush frame: " << ret);
        return false;
    }
    
    bool got_data = false;
    encoded_data.clear();
    
    // 获取所有剩余数据
    while (true) {
        MppPacket mpp_packet = nullptr;
        ret = api->encode_get_packet(ctx, &mpp_packet);
        
        if (ret == MPP_ERR_TIMEOUT || ret != MPP_OK) {
            break;
        }
        
        if (ret != MPP_OK || !mpp_packet) {
            break;
        }
        
        void* packet_data = mpp_packet_get_data(mpp_packet);
        size_t packet_size = mpp_packet_get_length(mpp_packet);
        
        if (packet_data && packet_size > 0) {
            encoded_data.insert(encoded_data.end(),
                              static_cast<uint8_t*>(packet_data),
                              static_cast<uint8_t*>(packet_data) + packet_size);
            got_data = true;
            
            {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                total_bytes_ += packet_size;
            }
        }
        
        mpp_packet_deinit(&mpp_packet);
    }
    
    if (got_data) {
        LOG_INFO("Flush completed, got " << encoded_data.size() << " bytes");
    } else {
        LOG_INFO("Flush completed, no data");
    }
    
    return got_data;
}

// 重置编码器
bool MppH264Encoder::Reset() {
    LOG_INFO("Resetting encoder...");
    
    Cleanup();
    
    if (!Init(config_)) {
        LOG_ERROR("Failed to reset encoder");
        return false;
    }
    
    LOG_INFO("Encoder reset successfully");
    return true;
}

// 获取统计信息
MppEncoderStats MppH264Encoder::GetStats() const {
    MppEncoderStats stats;
    
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats.frames_encoded = frame_count_;
        stats.total_bytes = total_bytes_;
        
        if (frame_count_ > 0) {
            stats.avg_encode_time_ms = (total_encode_time_us_ / 1000.0) / frame_count_;
        } else {
            stats.avg_encode_time_ms = 0.0;
        }
    }
    
    // 获取缓冲区使用情况
    stats.memory_buffers_total = low_memory_mode_ ? 2 : config_.buffer_count;
    stats.memory_buffers_used = 0; // 目前无法获取准确的使用数量
    
    return stats;
}

// 获取配置
MppEncoderConfig MppH264Encoder::GetConfig() const {
    return config_;
}

// 释放缓冲区
void MppH264Encoder::ReleaseBuffers() {
    if (buffer_group_) {
        MppBufferGroup group = reinterpret_cast<MppBufferGroup>(buffer_group_);
        mpp_buffer_group_put(group);
        buffer_group_ = nullptr;
        LOG_DEBUG("MPP buffers released");
    }
}

// 清理函数
void MppH264Encoder::Cleanup() {
    if (!init_ok_) {
        return;
    }
    
    LOG_INFO("Cleaning up MPP encoder...");
    
    // 刷新编码器以获取剩余数据
    std::vector<uint8_t> flush_data;
    Flush(flush_data);
    
    // 清理SWS上下文
    if (sws_ctx_) {
        sws_freeContext(sws_ctx_);
        sws_ctx_ = nullptr;
        LOG_DEBUG("SWS context freed");
    }
    
    // 清理AVFrame
    if (scaled_frame_) {
        av_frame_free(&scaled_frame_);
        scaled_frame_ = nullptr;
        LOG_DEBUG("Scaled frame freed");
    }
    
    // 释放缓冲区
    ReleaseBuffers();
    
    // 清理MPP资源
    if (mpp_ctx_) {
        MppCtx ctx = reinterpret_cast<MppCtx>(mpp_ctx_);
        mpp_destroy(ctx);
        mpp_ctx_ = nullptr;
        mpp_api_ = nullptr;
        LOG_DEBUG("MPP context destroyed");
    }
    
    // 重置状态
    init_ok_ = false;
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        frame_count_ = 0;
        total_bytes_ = 0;
        total_encode_time_us_ = 0;
    }
    last_input_format_ = AV_PIX_FMT_NONE;
    last_input_width_ = 0;
    last_input_height_ = 0;
    pts_counter_ = 0;
    
    LOG_INFO("MPP encoder cleanup completed");
}

// 解析H.264 NAL单元类型
uint8_t MppH264Encoder::GetNalUnitType(const uint8_t* data, size_t size) {
    if (size < 5) return 0;
    
    // 查找起始码
    size_t offset = 0;
    while (offset + 4 <= size) {
        if (data[offset] == 0x00 && data[offset + 1] == 0x00 && 
            data[offset + 2] == 0x01) {
            offset += 3;
            break;
        } else if (data[offset] == 0x00 && data[offset + 1] == 0x00 && 
                   data[offset + 2] == 0x00 && data[offset + 3] == 0x01) {
            offset += 4;
            break;
        }
        offset++;
    }
    
    if (offset >= size) return 0;
    
    // 获取NAL单元类型 (低5位)
    return data[offset] & 0x1F;
}

// 提取SPS和PPS数据
bool MppH264Encoder::ExtractSpsPpsFromPacket(void* packet) {
    if (!packet || sps_pps_extracted_) {
        return false;
    }
    
    void* packet_data = mpp_packet_get_data(packet);
    size_t packet_size = mpp_packet_get_length(packet);
    
    if (!packet_data || packet_size == 0) {
        return false;
    }
    
    uint8_t* data = static_cast<uint8_t*>(packet_data);
    size_t offset = 0;
    
    while (offset + 4 < packet_size) {
        // 查找起始码
        bool found_start_code = false;
        size_t start_code_len = 0;
        
        if (data[offset] == 0x00 && data[offset + 1] == 0x00 && 
            data[offset + 2] == 0x01) {
            start_code_len = 3;
            found_start_code = true;
        } else if (data[offset] == 0x00 && data[offset + 1] == 0x00 && 
                   data[offset + 2] == 0x00 && data[offset + 3] == 0x01) {
            start_code_len = 4;
            found_start_code = true;
        }
        
        if (!found_start_code) {
            offset++;
            continue;
        }
        
        size_t nal_start = offset + start_code_len;
        if (nal_start >= packet_size) {
            break;
        }
        
        // 获取NAL单元类型
        uint8_t nal_type = data[nal_start] & 0x1F;
        
        // 查找下一个起始码以确定当前NAL单元长度
        size_t nal_end = nal_start;
        while (nal_end + 3 < packet_size) {
            if (data[nal_end] == 0x00 && data[nal_end + 1] == 0x00 && 
                data[nal_end + 2] == 0x01) {
                break;
            } else if (data[nal_end] == 0x00 && data[nal_end + 1] == 0x00 && 
                       data[nal_end + 2] == 0x00 && data[nal_end + 3] == 0x01) {
                break;
            }
            nal_end++;
        }
        
        size_t nal_size = nal_end - nal_start;
        
        // 提取SPS (NAL类型7)
        if (nal_type == 7 && sps_data_.empty()) {
            sps_data_.resize(start_code_len + nal_size);
            memcpy(sps_data_.data(), data + offset, start_code_len + nal_size);
            LOG_INFO("Extracted SPS, size: " << sps_data_.size());
        }
        // 提取PPS (NAL类型8)
        else if (nal_type == 8 && pps_data_.empty()) {
            pps_data_.resize(start_code_len + nal_size);
            memcpy(pps_data_.data(), data + offset, start_code_len + nal_size);
            LOG_INFO("Extracted PPS, size: " << pps_data_.size());
        }
        
        // 如果已经提取到SPS和PPS，设置标志
        if (!sps_data_.empty() && !pps_data_.empty()) {
            sps_pps_extracted_ = true;
            LOG_INFO("SPS and PPS extracted successfully");
        }
        
        // 移动到下一个NAL单元
        offset = nal_end;
    }
    
    return sps_pps_extracted_;
}

// 获取SPS和PPS作为单独的NAL单元
std::vector<uint8_t> MppH264Encoder::GetSpsPpsNalUnits() {
    std::vector<uint8_t> result;
    
    if (!sps_data_.empty()) {
        result.insert(result.end(), sps_data_.begin(), sps_data_.end());
    }
    
    if (!pps_data_.empty()) {
        result.insert(result.end(), pps_data_.begin(), pps_data_.end());
    }
    
    return result;
}

// 生成AVCC格式的extradata
std::vector<uint8_t> MppH264Encoder::GetExtradata() {
    // AVCC格式: [版本][profile][compat][level][保留位][sps个数][sps长度][sps数据][pps个数][pps长度][pps数据]
    std::vector<uint8_t> extradata;
    
    if (sps_data_.empty() || pps_data_.empty()) {
        return extradata;
    }
    
    // 跳过起始码提取实际的SPS/PPS数据
    std::vector<uint8_t> sps_no_startcode;
    std::vector<uint8_t> pps_no_startcode;
    
    // 从SPS中提取数据（跳过起始码）
    size_t sps_offset = 0;
    if (sps_data_.size() > 3 && sps_data_[0] == 0x00 && 
        sps_data_[1] == 0x00 && sps_data_[2] == 0x01) {
        sps_offset = 3;
    } else if (sps_data_.size() > 4 && sps_data_[0] == 0x00 && 
               sps_data_[1] == 0x00 && sps_data_[2] == 0x00 && 
               sps_data_[3] == 0x01) {
        sps_offset = 4;
    }
    sps_no_startcode.assign(sps_data_.begin() + sps_offset, sps_data_.end());
    
    // 从PPS中提取数据（跳过起始码）
    size_t pps_offset = 0;
    if (pps_data_.size() > 3 && pps_data_[0] == 0x00 && 
        pps_data_[1] == 0x00 && pps_data_[2] == 0x01) {
        pps_offset = 3;
    } else if (pps_data_.size() > 4 && pps_data_[0] == 0x00 && 
               pps_data_[1] == 0x00 && pps_data_[2] == 0x00 && 
               pps_data_[3] == 0x01) {
        pps_offset = 4;
    }
    pps_no_startcode.assign(pps_data_.begin() + pps_offset, pps_data_.end());
    
    // 构建AVCC格式
    extradata.resize(6 + 2 + sps_no_startcode.size() + 1 + 2 + pps_no_startcode.size());
    size_t offset = 0;
    
    // 版本 (1 = AVCC)
    extradata[offset++] = 0x01;
    
    // profile, compat, level (从SPS中获取)
    if (sps_no_startcode.size() >= 4) {
        extradata[offset++] = sps_no_startcode[1]; // profile
        extradata[offset++] = sps_no_startcode[2]; // compat
        extradata[offset++] = sps_no_startcode[3]; // level
    } else {
        extradata[offset++] = 0x64; // High profile
        extradata[offset++] = 0x00; // compat
        extradata[offset++] = 0x1F; // level 3.1
    }
    
    // 保留位 + NAL长度字节数减1 (通常为3，表示4字节长度)
    extradata[offset++] = 0xFF;
    extradata[offset++] = 0xE1; // SPS个数为1
    
    // SPS长度
    uint16_t sps_len = static_cast<uint16_t>(sps_no_startcode.size());
    extradata[offset++] = (sps_len >> 8) & 0xFF;
    extradata[offset++] = sps_len & 0xFF;
    
    // SPS数据
    memcpy(&extradata[offset], sps_no_startcode.data(), sps_no_startcode.size());
    offset += sps_no_startcode.size();
    
    // PPS个数
    extradata[offset++] = 0x01; // PPS个数为1
    
    // PPS长度
    uint16_t pps_len = static_cast<uint16_t>(pps_no_startcode.size());
    extradata[offset++] = (pps_len >> 8) & 0xFF;
    extradata[offset++] = pps_len & 0xFF;
    
    // PPS数据
    memcpy(&extradata[offset], pps_no_startcode.data(), pps_no_startcode.size());
    
    return extradata;
}

} // namespace emai