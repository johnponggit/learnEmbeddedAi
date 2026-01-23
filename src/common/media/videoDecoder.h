
#pragma once

#include "videoDecoderInterface.h"


namespace emai {

enum DecoderType {
    FFMPEG_CPU,
    FFMPEG_RKMPP,
    FFMPEG_CUDA
    // todo: add more decoder types        
};

inline std::ostream& operator<<(std::ostream& os, DecoderType type) {
    switch (type) {
        case FFMPEG_CPU:
            os << "FFMPEG_CPU";
            break;
        case FFMPEG_RKMPP:
            os << "FFMPEG_RKMPP";
            break;
        case FFMPEG_CUDA:
            os << "FFMPEG_CUDA";
            break;
        default:
            os << "UNKNOWN_DECODER_TYPE";
            break;
    }
    return os;
}

class VideoDecoder final : public IDemuxEventHandle {
public:
    using Ptr =  std::shared_ptr<VideoDecoder>;
    using uPtr =  std::unique_ptr<VideoDecoder>;
    
    VideoDecoder(IDecodeEventHandle::wPtr handle, DecoderType type = FFMPEG_CPU, int device_id = -1);
    ~VideoDecoder();

    virtual bool OnParseInfo(const VideoInfo& info) override;
    virtual bool OnPacket(const AVPacket* packet, const int64_t frmIndex) override;
    virtual void OnEos() override;
    virtual bool Running() override;
    virtual void Destroy();

    VideoInfo& GetVideoInfo() { return info_; }

private:
    VideoInfo                 info_;
    VideoDecoderInterface::uPtr    impl_{nullptr};

    int                       device_id_;
    bool                      send_eos_{false};

};
       

}
 
 