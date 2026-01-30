#pragma once

#include <vector>
#include <mutex>
#include <atomic>
#include <string>

extern "C" {
#include <libavutil/frame.h>
#include <libswscale/swscale.h>
#include <libavutil/pixdesc.h>
}

namespace emai {

#define RK_ALIGN(x, a) (((x) + (a) - 1) & ~((a) - 1))
   
struct MppEncoderConfig {
    int src_width;
    int src_height;
    int dst_width;
    int dst_height;
    int fps;
    int bitrate_bps;
    int gop_size;
    int buffer_count;
    bool low_memory_mode;
    std::string pixel_format;
};

struct MppEncoderStats {
    int frames_encoded;
    size_t total_bytes;
    double avg_encode_time_ms;
    int memory_buffers_total;
    int memory_buffers_used;
};

class MppH264Encoder {
public:
    MppH264Encoder();
    ~MppH264Encoder();
    
    // 初始化
    bool Init(const MppEncoderConfig& config);
    bool Init(int src_width, int src_height, int dst_width, int dst_height,
              int fps, int bitrate_bps, bool low_memory_mode = true);
    
    // 编码接口
    bool EncodeFrame(AVFrame* frame, std::vector<uint8_t>& encoded_data);
    bool Flush(std::vector<uint8_t>& encoded_data);
    bool Reset();
    
    // 状态查询
    MppEncoderStats GetStats() const;
    MppEncoderConfig GetConfig() const;
    
    // 工具函数
    static bool IsMppAvailable();
  
    // 获取SPS和PPS数据
    std::vector<uint8_t> GetSpsData() const { return sps_data_; }
    std::vector<uint8_t> GetPpsData() const { return pps_data_; }
    
    // 获取SPS和PPS作为单个NAL单元
    std::vector<uint8_t> GetSpsPpsNalUnits();
    
    // 生成AVCC格式的extradata（用于H.264流）
    std::vector<uint8_t> GetExtradata();

private:
    void* mpp_ctx_;            // MppCtx
    void* mpp_api_;            // MppApi*
    void* buffer_group_;       // MppBufferGroup
    
    // 缩放相关
    SwsContext* sws_ctx_;
    AVFrame* scaled_frame_;
    AVPixelFormat last_input_format_;
    int last_input_width_;
    int last_input_height_;
    
    // 配置和状态
    MppEncoderConfig config_;
    bool init_ok_;
    bool low_memory_mode_;
    
    // 统计信息
    mutable std::mutex stats_mutex_;
    int frame_count_;
    size_t total_bytes_;
    uint64_t total_encode_time_us_;
    
    // 时间戳
    int64_t pts_counter_;
    
    // 存储SPS/PPS数据
    std::vector<uint8_t> sps_data_;
    std::vector<uint8_t> pps_data_;
    std::atomic<bool> sps_pps_extracted_{false};

    // 私有方法
    bool InitMppEncoder();
    bool InitSoftwareScaler();
    bool AllocateBuffers();
    void Cleanup();
    
    size_t CalculateBufferSize() const;
    void ConvertYUV420PToNV12(const AVFrame* src, uint8_t* dst) const;
    void ReleaseBuffers();
    
    bool EncodeWithScaling(AVFrame* frame, AVPixelFormat input_format,
                          std::vector<uint8_t>& out_data);
    bool EncodeNV12Frame(AVFrame* frame, std::vector<uint8_t>& out_data);
    bool EncodeYUV420PFrame(AVFrame* frame, std::vector<uint8_t>& out_data);
    
    bool SendFrameToMpp(void* yuv_data, int width, int height, int64_t pts);
    bool ReceivePacketFromMpp(std::vector<uint8_t>& out_data);
    bool ExtractSpsPpsFromPacket(void*  packet);
    uint8_t GetNalUnitType(const uint8_t* data, size_t size);

};

} // namespace emai