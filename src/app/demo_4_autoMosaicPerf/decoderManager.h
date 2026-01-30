#pragma once    

#include "videoParser.h"
#include "videoDecoder.h"
#include "rknnYolov5Detector.h"
#include "mosaicProcessor.h"
#include "httpFlvStreamer.h"
#include "MppH264Encoder.h"
#include <unordered_map>

class DecoderManager : public std::enable_shared_from_this<DecoderManager>, public emai::IDecodeEventHandle, public IHttFlvStreamHandler
{    
public:
    using Ptr  = std::shared_ptr<DecoderManager>;

    struct PerfStats {
        double decode_ms = 0;
        double detect_ms = 0;
        double mosaic_ms = 0;
        double encode_ms = 0;
        double total_ms = 0;
        int fps = 0;
        int64_t stream_clients = 0;
    };

    DecoderManager();    
    ~DecoderManager();
    
    // 启动流
    int start_stream(const std::string& rtsp_url);
    
    // 停止所有流
    void stop_all();
    
    // 获取处理后的JPEG帧（兼容旧接口）
    bool get_processed_frame(std::vector<uint8_t>& frame);    

    // 获取性能统计
    std::string get_perf_stats_json();

    // 获取FLV流
    virtual bool get_flv_stream_data(std::vector<uint8_t>& data, int client_id) override;
    virtual int  register_flv_client() override;
    virtual void unregister_flv_client(int client_id) override;
    virtual bool get_encoder_config(int& width, int& height, int& fps) override;

    // 获取SPS/PPS数据用于FLV流
    bool get_sps_pps_data(std::vector<uint8_t>& sps, std::vector<uint8_t>& pps);
   
    int  updateMosaicSettingsByDetect(const emai::YUVFrame& frame, double& detect_time_ms);

    bool update_mosaic_settings(int x, int y, int width, int height, int block_size = 16, int border_size = 2, bool enabled = true);
    std::string get_mosaic_settings_json();
    MosaicProcessor::MosaicSettings getMosaicSettings();

    // 获取当前设置
    std::string get_current_url();    
    bool        is_streaming();

    // IDecodeEventHandle interface
    virtual void onDecodeEos() override;
    virtual void onDecodeFrame(AVFrame* frame, const int64_t frmIndex) override;

private:
    void        processing_loop();
    void        stream_broadcast_loop();
    int         updateBlurSettingsByDetect(const emai::YUVFrame& frame);
    bool        init_h264_encoder(int width, int height);
    void        broadcast_h264_data(const std::vector<uint8_t>& data);

private:
    /* 解封装 */
    emai::VideoParser::Ptr                            parser_{nullptr};
    std::atomic<bool>                                 parserStartOk_{false};
    std::thread                                       parserThread_;

    /* 解码 */
    emai::VideoDecoder::Ptr                           decoder_{nullptr};
    std::unique_ptr<emai::YUVFrameBuffer>             decodedFrameBuffer_{nullptr};
    SwsContext*                                       decodedYuvSwsCtx_{nullptr};

    std::mutex                                        managerMutex_;
    std::string                                       currentUrl_;

    // 处理线程
    std::thread                                       processThread_;
    std::atomic<bool>                                 processing_{false};
    
    // H.264编码器
    // std::unique_ptr<emai::H264RkmppEncoder>           h264_encoder_{nullptr}; asdftest
    std::unique_ptr<emai::MppH264Encoder>             h264_encoder_{nullptr};
    std::queue<std::vector<uint8_t>>                  h264_frame_queue_;
    std::mutex                                        h264_queue_mutex_;
    std::condition_variable                           h264_queue_cv_;
    
    // 流广播线程
    std::thread                                       broadcast_thread_;
    std::atomic<bool>                                 broadcasting_{false};
    
    // 客户端管理
    struct StreamClient {
        int64_t last_active_time;
        int64_t sequence;
        std::queue<std::vector<uint8_t>> data_queue;  // 每个客户端的数据队列
    };
    std::unordered_map<int, StreamClient>             stream_clients_;
    std::mutex                                        clients_mutex_;
    std::atomic<int>                                  next_client_id_{1};
    
    // 处理后的JPEG帧缓冲区（兼容旧接口）
    std::unique_ptr<std::queue<std::vector<uint8_t>>> jpegBuffer_;
    std::mutex                                        jpegBufferMutex_;
    
    MosaicProcessor::Ptr                              mosaicProcessor_{nullptr};

    std::unique_ptr<emai::RknnYolov5Detector>         detector_{nullptr};
    
    // 性能统计
    std::atomic<int>                                  processedFrameCnt_{0};
    std::atomic<int64_t>                              lastStatTime_{0};
    
    PerfStats                                         perfStats_;
    std::mutex                                        perfMutex_;
    
    int64_t                                           detectFrameCnt_{0};
    int                                               detectFrameSkipNum_{3};
    int                                               dst_width_{800};
    int                                               dst_height_{600};
    std::string                                       enLabel_ = "person";
    bool                                              enable_auto_detect_{true};
    std::chrono::high_resolution_clock::time_point    onDecodeFrameTime_;
};