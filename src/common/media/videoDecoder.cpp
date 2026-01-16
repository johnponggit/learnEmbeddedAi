
#include <algorithm>
#include <memory>
#include <string>
 
 
#include "videoDecoder.h"
#include "videoDecoderFfmpegCpu.h"
#include "util.h"

namespace emai{
 
VideoDecoder::VideoDecoder(IDecodeEventHandle::wPtr handle, DecoderType type, int device_id) : device_id_(device_id)
{
    LOG_INFO("in VideoDecoder ctor, type:" << type << ", device_id:" << device_id);

    switch (type) {
        case FFMPEG_CPU:
            impl_ = std::make_unique<VideoDecoderFfmpegCpu>(handle);
            break;

        default:
            LOG_ERROR("unsupported decoder type:" << type);
            break;
    }

    if (!impl_) {
        LOG_ERROR("create VideoDecoderImpl failed");
    }
}
 
VideoDecoder::~VideoDecoder() 
{
    LOG_INFO("in VideoDecoder deCtor");

    if (impl_ == nullptr) {
        return;
    }

    impl_->unInit();
    impl_ = nullptr;
}
 
bool VideoDecoder::OnParseInfo(const VideoInfo& info) {
    LOG_INFO("in VideoDecoder OnParseInfo");
    info_ = info;
    return impl_->Init(info);
}
 
bool VideoDecoder::OnPacket(const AVPacket* packet, const int64_t frmIndex) {
    return impl_->FeedPacket(packet, frmIndex);
}
 
void VideoDecoder::OnEos() {
    if (send_eos_ == false) {
        LOG_INFO("VideoDecoder OnEos(): Feed EOS");
        impl_->FeedEos();
        send_eos_ = true;
    }
}
 
bool VideoDecoder::Running() {
    //return runner_->Running();
    return true;
}
 
void VideoDecoder::Destroy() {
    impl_->unInit();
}
 
}  // namespace emai