
 #include "videoParser.h"

 #include <atomic>
 #include <chrono>
 #include <thread>
 
 
 #ifdef __cplusplus
 extern "C" {
 #endif
 #include <libavcodec/avcodec.h>
 #include <libavformat/avformat.h>
 #include <libavutil/avutil.h>
 #include <libavutil/imgutils.h>
 #ifdef __cplusplus
 }
 #endif
 #ifdef __GNUC__
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
 #endif
 
 namespace emai{

const uint32_t kDefaultFps = 25;

static void printVersionInfo() {
    unsigned int version = avformat_version();
    
    LOG_INFO("libavformat 版本信息: " << AV_VERSION_MAJOR(version) << "." << AV_VERSION_MINOR(version) << "." << AV_VERSION_MICRO(version));   
    LOG_INFO("配置信息: " << avformat_configuration());
    LOG_INFO("许可证: " << avformat_license());
    
    
    // avformat_version_info();
}

std::ostream& operator<<(std::ostream& os, const VideoInfo& info)
{
    return os << "VideoInfo{ width:" << info.width
              << ", height:" << info.height
              << ", codec_id:" << info.codec_id
              << ", progressive:" << info.progressive
              << ", url:" << info.url
              << " }";
}

 namespace detail {
 static int InterruptCallBack(void* ctx) {
    VideoParser* parser = reinterpret_cast<VideoParser*>(ctx);
    if (parser->CheckTimeout()) {
        LOG_INFO("Rtsp: Get interrupt and timeout");
        return 1;
    }
   return 0;
 }

std::string avErrorToString(int errnum) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    if (av_strerror(errnum, errbuf, sizeof(errbuf)) == 0) {
        return std::string(errbuf);
    }
    return "unknown error";
}

 }  // namespace detail
 
bool VideoParser::CheckTimeout() {
    std::chrono::duration<float, std::milli> dura = std::chrono::steady_clock::now() - last_receive_frame_time_;
    if (dura.count() > max_receive_timeout_) {
        return true;
    }
    return false;
}

bool VideoParser::Open(const char *url, bool save_file) {
    static struct _InitFFmpeg {
        _InitFFmpeg() {
            // init ffmpeg            
            avformat_network_init();

            #if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 9, 100)
            // FFmpeg 4.0 及更早版本需要手动注册
            avcodec_register_all();
            av_register_all();
            #endif

            printVersionInfo();
        }
    } _init_ffmpeg;

    is_rtsp_ = emai::IsRtsp(url);
    if (have_video_source_.load()) return false;
    // format context
    p_format_ctx_ = avformat_alloc_context();
    if (!p_format_ctx_) return false;
    if (is_rtsp_) {
        AVIOInterruptCB intrpt_callback = {detail::InterruptCallBack, this};
        p_format_ctx_->interrupt_callback = intrpt_callback;
        last_receive_frame_time_ = std::chrono::steady_clock::now();
        // options
        av_dict_set(&options_, "rtsp_transport", "tcp", 0);
        av_dict_set(&options_, "stimeout", "3000000", 0); 
        av_dict_set(&options_, "buffer_size", "1024000", 0);
        av_dict_set(&options_, "max_delay", "500000", 0);
    } else {
        av_dict_set(&options_, "buffer_size", "1024000", 0);
        av_dict_set(&options_, "stimeout", "200000", 0);
    }
    // open input
    int ret_code = avformat_open_input(&p_format_ctx_, url, NULL, &options_);
    if (0 != ret_code) {        
        LOG_ERROR("Can not open input stream: " << url << ", avformat_open_input failed, ret_code:" << ret_code << ", errMsg:" << detail::avErrorToString(ret_code));
        return false;
    }
    // find video stream information
    ret_code = avformat_find_stream_info(p_format_ctx_, NULL);
    if (ret_code < 0) {
        LOG_ERROR("url:" << url << " Can not find stream information, ret_code:" << ret_code << ", errMsg:" << detail::avErrorToString(ret_code));
        return false;
    }
    video_index_ = -1;
    AVStream *vstream = nullptr;
    LOG_INFO("url:" << url << " Find stream information, nb_streams:" << p_format_ctx_->nb_streams);

    for (uint32_t iloop = 0; iloop < p_format_ctx_->nb_streams; iloop++) {
        vstream = p_format_ctx_->streams[iloop];

        LOG_INFO("url:" << url << " stream index:" << iloop << ", vstream is valid: " << (vstream ? "true" : "false") <<
                 ", codecpar is valid: " << (vstream->codecpar ? "true" : "false"));

        if (!vstream)
        {
            LOG_WARN("url:" << url << " stream index:" << iloop << " vstream is invalid");
            continue;
        }

        if (!vstream->codecpar) {
            LOG_WARN("url:" << url << " stream index:" << iloop << " codecpar is invalid");
            continue;
        }      

        //LOG_INFO("url:" << url << " stream index:" << iloop << " codecpar codec type: " << static_cast<int>(vstream->codecpar->codec_type));
        if (vstream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) { 
            video_index_ = iloop;
            break;
        }
    }

    if (video_index_ == -1) {
        LOG_ERROR("url:" << url << " Can not find a video stream.");
        return false;
    }

    info_.width = vstream->codecpar->width;
    info_.height = vstream->codecpar->height;

    // Get codec id, check progressive
    auto codec_id = vstream->codecpar->codec_id;
    int field_order = vstream->codecpar->field_order;

    info_.codec_id = codec_id;
    info_.codecpar = p_format_ctx_->streams[video_index_]->codecpar;

    /*
    * At this moment, if the demuxer does not set this value (avctx->field_order == UNKNOWN),
    * the input stream will be assumed as progressive one.
    */
    switch (field_order) {
        case AV_FIELD_TT:
        case AV_FIELD_BB:
        case AV_FIELD_TB:
        case AV_FIELD_BT:
            info_.progressive = 0;
        break;
        case AV_FIELD_PROGRESSIVE:  // fall through
        default:
            info_.progressive = 1;
        break;
    }

    // get extra data
    uint8_t* extradata = vstream->codecpar->extradata;
    int extradata_size = vstream->codecpar->extradata_size;

    info_.extra_data = std::vector<uint8_t>(extradata, extradata + extradata_size);

    if (vstream->avg_frame_rate.den) {
        info_.fps = vstream->avg_frame_rate.num / vstream->avg_frame_rate.den;
    }
    else {
        info_.fps = kDefaultFps;
    }

    LOG_INFO("url:" << url << " Video info: " << "width=" << info_.width << ", height=" << info_.height <<
               ",fps=" << info_.fps << ", codec_id=" << codec_id << ", progressive=" << info_.progressive << 
                "Format name is " << p_format_ctx_->iformat->name);

    if (strstr(p_format_ctx_->iformat->name, "mp4") || strstr(p_format_ctx_->iformat->name, "flv") ||
        strstr(p_format_ctx_->iformat->name, "matroska") || strstr(p_format_ctx_->iformat->name, "h264") ||
        strstr(p_format_ctx_->iformat->name, "rtsp")) {
        if (AV_CODEC_ID_H264 == codec_id) {
            // info_.codec_type = CodecType::H264;
            if (save_file) saver_.reset(new detail::FileSaver("out.h264"));
        } else if (AV_CODEC_ID_HEVC == codec_id) {
            // info_.codec_type = CodecType::H265;
            if (save_file) saver_.reset(new detail::FileSaver("out.h265"));
        } else {
            LOG_ERROR("url:" << url << " Unsupported codec id: " << codec_id);
            return false;
        }
    }
    av_init_packet(&packet_);
    have_video_source_.store(true);
    first_frame_ = true;  
    info_.url = url;
    frame_index_ = 0;
    
    if (handler_ && !handler_->OnParseInfo(info_)) {
        LOG_ERROR("url:" << url << " OnParseInfo err");
        return false;
    }

    return true;
}

void VideoParser::Close() {
    if (!have_video_source_.load()) return;
    LOG_INFO("url:" << info_.url << " VideoParser::Close(): Clear FFMpeg resources");

    if (p_format_ctx_) {
        avformat_close_input(&p_format_ctx_);
        avformat_free_context(p_format_ctx_);
        av_dict_free(&options_);
        p_format_ctx_ = nullptr;
        options_ = nullptr;
    }
    have_video_source_.store(false);
    
    saver_.reset();
    if (handler_)
    {
        handler_->Destroy();
    }
}

int VideoParser::ParseLoop(uint32_t frame_interval) {
    auto now_time = std::chrono::steady_clock::now();
    auto last_time = std::chrono::steady_clock::now();
    std::chrono::duration<double, std::milli> dura;

    while (parseLoopRunning_.load()) {
        if (!have_video_source_.load()) {
            LOG_ERROR("url:" << info_.url << " VideoParser::ParseLoop(): Video source has not been init");
            return -1;
        }

        int ret = 0;
        if ((ret = av_read_frame(p_format_ctx_, &packet_)) < 0) {
            // EOS
            if (ret == AVERROR_EOF) {
                LOG_INFO("url:" << info_.url << " VideoParser::ParseLoop(): av_read_frame hit end of stream");
                if (handler_) {handler_->OnEos();}
                return 1;
            } if (isRecoverableError(ret)) {
                LOG_WARN("url:" << info_.url << " VideoParser::ParseLoop(): av_read_frame recoverable error, ret:" << ret << ", errMsg:" << detail::avErrorToString(ret));
                continue;
            } else {
                LOG_ERROR("url:" << info_.url << " VideoParser::ParseLoop(): av_read_frame error, ret:" << ret << ", errMsg:" << detail::avErrorToString(ret));
                return -1;
            }
        }

        // update receive frame time
        last_receive_frame_time_ = std::chrono::steady_clock::now();

        // skip unmatched stream
        if (packet_.stream_index != video_index_) {
            av_packet_unref(&packet_);
            continue;
        }

        // filter non-key-frame in head
        if (first_frame_) {
            LOG_INFO("url:" << info_.url << " VideoParser::ParseLoop(): Check first frame");
            if (packet_.flags & AV_PKT_FLAG_KEY) {
                first_frame_ = false;
            } else {
                LOG_DEBUG("url:" << info_.url << " VideoParser::ParseLoop(): Skip first not-key-frame");
                av_packet_unref(&packet_);
                continue;
            }
        }

        // parse data from packet
        auto vstream = p_format_ctx_->streams[video_index_];
        frame_index_++;
        // find pts information
        if (AV_NOPTS_VALUE == packet_.pts) {
            LOG_INFO("url:" << info_.url << " VideoParser::ParseLoop(): Didn't find pts informations, use ordered numbers instead.");
            packet_.pts = frame_index_;
        } else {
            packet_.pts = av_rescale_q(packet_.pts, vstream->time_base, {1, 90000});
        }

        if (saver_) {
            saver_->Write(reinterpret_cast<char *>(packet_.data), packet_.size);
        }

        LOG_DEBUG("url:" << info_.url << " VideoParser::ParseLoop(): Get video packet, size=" << packet_.size
                     << ", pts=" << packet_.pts << ", dts=" << packet_.dts
                     << ", stream_index=" << packet_.stream_index << ", is key_frame=" << ((packet_.flags & AV_PKT_FLAG_KEY) ? "yes" : "no"));

        if (handler_ && !handler_->OnPacket(&packet_, frame_index_)) return -1;

        av_packet_unref(&packet_);

        // frame rate control
        if (frame_interval) {
            now_time = std::chrono::steady_clock::now();
            dura = now_time - last_time;
            if (frame_interval > dura.count()) {
                std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(frame_interval - dura.count()));
            }
            last_time = std::chrono::steady_clock::now();
        }
    }  // while (true)

    return 1;
}



bool VideoParser::isRecoverableError(int error_code) {
    // 定义哪些错误是可以恢复的
    switch (error_code) {
        case AVERROR(EAGAIN):    // 需要重试
        case AVERROR_INVALIDDATA: // 无效数据，跳过当前包
            return true;
        default:
            return false;
    }
}




}
 