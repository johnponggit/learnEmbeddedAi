#pragma once

#include "util.h"
#include "IVideoDecoder.h"
#include "IJpegEncoder.h"
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_drm.h>

namespace emai {


class VideoDecoderFfmpegRkmpp : public IVideoDecoder {
public:
    VideoDecoderFfmpegRkmpp(IDecodeEventHandle::wPtr handle, bool decodeDebugEn = false);
    ~VideoDecoderFfmpegRkmpp();

    virtual bool Init(const VideoInfo& info) override; 
    virtual bool FeedPacket(const AVPacket* pkt, const int64_t frmIndex) override;
    virtual void FeedEos() override;
    virtual int  unInit() override;

private:
    bool        InitHardwareDecoder();
    bool        ProcessFrame(AVFrame* frame, const int64_t frmIndex);
    AVFrame*    TransferHwFrameToSw(AVFrame* hw_frame);
    void        printStreamInfo(); 
    void        printHardwareInfo();
    std::string generateFilename(const std::string& output_dir); 
    uint64_t    getSavedPicNum() const; 
    void        addSavedPicNum(int num); 
    void        ensureDirectoryExists(const std::string& filePath); 
    std::string makeFilenameHead(const std::string& input); 
    void        clrTmpPics();
    
    // 硬件加速相关函数
    bool        SetupHardwareContext();
    void        CleanupHardwareContext();
    const char* GetPixelFormatName(enum AVPixelFormat pix_fmt);

private:
    const std::string kSavedTmpPicDir = "./tmpPic/";

    AVCodecContext*          decode_{nullptr};
    AVBufferRef*             hw_device_ctx_{nullptr};  // 硬件设备上下文
    AVFrame*                 hw_frame_{nullptr};       // 硬件帧
    AVFrame*                 sw_frame_{nullptr};       // 软件帧（用于转换）
    
    IJpegEncoder::Ptr        jpegEncoder_{nullptr};
    
    IDecodeEventHandle::wPtr handle_;

    std::string              url_;
    int                      fps_{-1};
    std::string              picNameHead_;
    std::atomic<int>         eos_got_{0};
    std::atomic<int>         eos_sent_{0};
    std::atomic<bool>        initOk{false};
    uint16_t                 tmpPicMaxNum_{3};
    std::atomic<uint64_t>    savedPicNum_{0};
    
    bool                     decodeDebugEn_{false};
    bool                     use_hardware_{true};      // 是否使用硬件解码
    enum AVPixelFormat       hw_pix_fmt_{AV_PIX_FMT_NONE}; // 硬件支持的像素格式
    enum AVHWDeviceType      hw_device_type_{AV_HWDEVICE_TYPE_NONE}; // 硬件设备类型
    
    // 统计信息
    std::atomic<uint64_t>    hw_frame_count_{0};
    std::atomic<uint64_t>    sw_frame_count_{0};
};

} // namespace emai