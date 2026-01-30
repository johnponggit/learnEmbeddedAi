

#pragma once

#include "videoParser.h"

namespace emai {


 
class VideoParserAsync {
public:
    using Ptr  = std::shared_ptr<VideoParserAsync>;

    explicit VideoParserAsync(IDemuxEventHandle::Ptr handle) : handler_(handle) {}
    ~VideoParserAsync() { Close(); }
    bool Open(const char* url, bool save_file = false);
    // -1 for error, 1 for eos
    int ParseLoop(uint32_t frame_interval);
    void Close();
    bool CheckTimeout();
    bool IsRtsp() { return is_rtsp_; }

    const VideoInfo& GetVideoInfo() const { return info_; }
    void setParseLoopRunning(bool running) { parseLoopRunning_.store(running); }

private:
    // 异步数据包结构
    struct AsyncPacket {
        AVPacket packet;
        int64_t frame_index;
        std::chrono::steady_clock::time_point enqueue_time;
        
        AsyncPacket(const AVPacket* src, int64_t idx) : frame_index(idx) {
            av_init_packet(&packet);
            if (src) {
                av_packet_ref(&packet, src);
            }
            enqueue_time = std::chrono::steady_clock::now();
        }
        
        ~AsyncPacket() {
            av_packet_unref(&packet);
        }
        
        // 移动构造函数
        AsyncPacket(AsyncPacket&& other) noexcept {
            packet = other.packet;
            frame_index = other.frame_index;
            enqueue_time = other.enqueue_time;
            av_init_packet(&other.packet); // 防止双重释放
        }
    };

    bool AsyncProcessPacket(const AVPacket* packet, int64_t frame_index);
    void AsyncProcessingThread();
    void StopAsyncProcessing();
    void ClearAsyncQueue();
    void SendEosToAsyncQueue();


    static constexpr uint32_t max_receive_timeout_{3000};
    bool        isRecoverableError(int error_code);
  
    AVFormatContext* p_format_ctx_ = nullptr;
    AVPacket packet_;
    AVDictionary* options_{nullptr};

    VideoInfo info_;
    IDemuxEventHandle::Ptr handler_;
    std::unique_ptr<detail::FileSaver> saver_{nullptr};
    std::chrono::time_point<std::chrono::steady_clock> last_receive_frame_time_{};

    uint64_t frame_index_{0};   /* 每次重连清零 */
    int32_t video_index_{0};
    std::atomic<bool> have_video_source_{false};
    bool first_frame_{true};
    bool is_rtsp_{false};

    std::atomic<bool>   parseLoopRunning_{false};

    // 异步处理相关
    std::queue<std::unique_ptr<AsyncPacket>> async_packet_queue_;
    std::mutex async_queue_mutex_;
    std::condition_variable async_queue_cv_;
    std::thread async_thread_;
    std::atomic<bool> async_running_{false};
    std::atomic<bool> async_eos_sent_{false};
    size_t max_async_queue_size_{30};  // 最大队列大小，防止内存溢出
    std::atomic<int64_t> processed_packets_{0};
    std::atomic<int64_t> dropped_packets_{0};

    std::chrono::high_resolution_clock::time_point    onParseFrameTime_;


    
};

}
 
 