
#pragma once

#include "util.h"
#include "videoDecoderImpl.h"
#include "jpegEncoderImpl.h"

namespace emai {



const std::string kSavedTmpPicDir = "./tmpPic/";

class VideoDecoderFfmpegCpu : public VideoDecoderImpl {
public:
    VideoDecoderFfmpegCpu(IDecodeEventHandle::wPtr handle, bool decodeDebugEn = false);
    ~VideoDecoderFfmpegCpu();

    virtual bool Init(const VideoInfo& info) override; 
    virtual bool FeedPacket(const AVPacket* pkt, const int64_t frmIndex) override;
    virtual void FeedEos() override;
    virtual int  unInit() override;

private:
    bool        ProcessFrame(AVFrame* frame, const int64_t frmIndex);
    void        printStreamInfo(); 
    std::string generateFilename(const std::string& output_dir); 
    uint64_t    getSavedPicNum() const; 
    void        addSavedPicNum(int num); 
    void        ensureDirectoryExists(const std::string& filePath); 
    std::string makeFilenameHead(const std::string& input); 
    void        clrTmpPics();
    

private:
    AVCodecContext*          decode_{nullptr};
    AVFrame                 *av_frame_ = nullptr;

    JpegEncoderImpl::Ptr     jpegEncoder_{nullptr};
    
    IDecodeEventHandle::wPtr handle_;

    std::string              url_;
    int                      fps_{-1};
    std::string              picNameHead_;
    std::atomic<int>         eos_got_{0};
    std::atomic<int>         eos_sent_{0};
    std::atomic<bool>        initOk{false};
    uint16_t                 tmpPicMaxNum_{3};
    std::atomic<uint64_t>    savedPicNum_{0};

    bool                      decodeDebugEn_{false};

};
       

}
 
 