

#pragma once

#include <atomic>
#include <chrono>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "util.h"

#ifdef __cplusplus
extern "C" {
#endif
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/error.h>
#ifdef __cplusplus
}
#endif
 
#define FFMPEG_VERSION_3_1 AV_VERSION_INT(57, 40, 100)
#define FFMPEG_VERSION_4_2_2 AV_VERSION_INT(58, 29, 100)

namespace emai {

namespace detail {
struct BeginWith {
explicit BeginWith(const std::string& str) noexcept : s(str) {}
inline bool operator()(const std::string& prefix) noexcept {
    if (s.size() < prefix.size()) return false;
    return prefix == s.substr(0, prefix.size());
}
std::string s;
};  // struct BeginWith
 
class FileSaver {
public:
    explicit FileSaver(const char* file_name) {
        of_.open(file_name);
        if (!of_.is_open()) {
            throw std::runtime_error("open file failed");
        }
    }

    ~FileSaver() { of_.close(); }

    void Write(char* buf, size_t len) { of_.write(buf, len); }

private:
    std::ofstream of_;
};

std::string avErrorToString(int errnum);

}  // namespace detail
 
inline bool IsRtsp(const std::string& url) { return detail::BeginWith(url)("rtsp://"); }
 
struct VideoInfo {
    AVCodecID codec_id = AV_CODEC_ID_NONE;
    #if LIBAVFORMAT_VERSION_INT >= FFMPEG_VERSION_3_1
    AVCodecParameters* codecpar = nullptr;
    #endif
    AVCodecContext* codec_ctx = nullptr;
    std::vector<uint8_t> extra_data{};
    int width = 0;
    int height = 0;
    int progressive = 0;
    int fps = 0;
    std::string url;
};
 
std::ostream& operator<<(std::ostream& os, const VideoInfo& info);

class IDemuxEventHandle {
public:
    using Ptr =  std::shared_ptr<IDemuxEventHandle>;

    virtual bool OnParseInfo(const VideoInfo& info) = 0;
    virtual bool OnPacket(const AVPacket* frame, const int64_t frmIndex) = 0;
    virtual void OnEos() = 0;
    virtual bool Running() = 0;
    virtual void Destroy() = 0;
};
 
class VideoParser {
public:
    using Ptr  = std::shared_ptr<VideoParser>;

    explicit VideoParser(IDemuxEventHandle::Ptr handle) : handler_(handle) {}
    ~VideoParser() { Close(); }
    bool Open(const char* url, bool save_file = false);
    // -1 for error, 1 for eos
    int ParseLoop(uint32_t frame_interval);
    void Close();
    bool CheckTimeout();
    bool IsRtsp() { return is_rtsp_; }

    const VideoInfo& GetVideoInfo() const { return info_; }
    void setParseLoopRunning(bool running) { parseLoopRunning_.store(running); }

private:
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
};

}
 
 