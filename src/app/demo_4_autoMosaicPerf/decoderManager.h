
#pragma once    

#include "videoParser.h"
#include "videoDecoder.h"
#include "rknnYolov5Detector.h"
#include "mosaicProcessor.h"

class DecoderManager : public std::enable_shared_from_this<DecoderManager>, public emai::IDecodeEventHandle
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
    };

    DecoderManager();    
    ~DecoderManager();
    
    // 启动流
    int start_stream(const std::string& rtsp_url);
    
    // 停止所有流
    void stop_all();
    
    // 获取处理后的JPEG帧
    bool get_processed_frame(std::vector<uint8_t>& frame);    

    // 获取性能统计
    std::string get_perf_stats_json();

   
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
    int         updateBlurSettingsByDetect(const emai::YUVFrame& frame);

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

    
    // 处理后的JPEG帧缓冲区
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
};

