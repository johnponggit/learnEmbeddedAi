
#include "videoParserAsync.h"


namespace emai{




bool VideoParserAsync::CheckTimeout() {
    std::chrono::duration<float, std::milli> dura = std::chrono::steady_clock::now() - last_receive_frame_time_;
    if (dura.count() > max_receive_timeout_) {
        return true;
    }
    return false;
}

bool VideoParserAsync::Open(const char *url, bool save_file) {
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
    
    // 清空异步队列
    ClearAsyncQueue();
    
    // 启动异步处理线程
    async_running_ = true;
    async_thread_ = std::thread(&VideoParserAsync::AsyncProcessingThread, this);
    
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
        // 停止异步线程
        StopAsyncProcessing();
        return false;
    }
    // find video stream information
    ret_code = avformat_find_stream_info(p_format_ctx_, NULL);
    if (ret_code < 0) {
        LOG_ERROR("url:" << url << " Can not find stream information, ret_code:" << ret_code << ", errMsg:" << detail::avErrorToString(ret_code));
        // 停止异步线程
        StopAsyncProcessing();
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
        // 停止异步线程
        StopAsyncProcessing();
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
            // 停止异步线程
            StopAsyncProcessing();
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
        // 停止异步线程
        StopAsyncProcessing();
        return false;
    }

    return true;
}

void VideoParserAsync::Close() {
    if (!have_video_source_.load()) return;
    LOG_INFO("url:" << info_.url << " VideoParserAsync::Close(): Clear FFMpeg resources");

    // 停止异步处理线程
    StopAsyncProcessing();
    
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

int VideoParserAsync::ParseLoop(uint32_t frame_interval) {
    auto now_time = std::chrono::steady_clock::now();
    auto last_time = std::chrono::steady_clock::now();
    std::chrono::duration<double, std::milli> dura;

    while (parseLoopRunning_.load()) {
        if (!have_video_source_.load()) {
            LOG_ERROR("url:" << info_.url << " VideoParserAsync::ParseLoop(): Video source has not been init");
            return -1;
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - onParseFrameTime_).count();
        onParseFrameTime_ = std::chrono::high_resolution_clock::now();
        LOG_DEBUG("ParseLoop duration: " << duration << " ms");


        int ret = 0;
        if ((ret = av_read_frame(p_format_ctx_, &packet_)) < 0) {
            // EOS
            if (ret == AVERROR_EOF) {
                LOG_INFO("url:" << info_.url << " VideoParserAsync::ParseLoop(): av_read_frame hit end of stream");
                if (handler_) {
                    // 异步发送EOS
                    SendEosToAsyncQueue();
                }
                return 1;
            } if (isRecoverableError(ret)) {
                LOG_WARN("url:" << info_.url << " VideoParserAsync::ParseLoop(): av_read_frame recoverable error, ret:" << ret << ", errMsg:" << detail::avErrorToString(ret));
                continue;
            } else {
                LOG_ERROR("url:" << info_.url << " VideoParserAsync::ParseLoop(): av_read_frame error, ret:" << ret << ", errMsg:" << detail::avErrorToString(ret));
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
            LOG_INFO("url:" << info_.url << " VideoParserAsync::ParseLoop(): Check first frame");
            if (packet_.flags & AV_PKT_FLAG_KEY) {
                first_frame_ = false;
            } else {
                LOG_DEBUG("url:" << info_.url << " VideoParserAsync::ParseLoop(): Skip first not-key-frame");
                av_packet_unref(&packet_);
                continue;
            }
        }

        // parse data from packet
        auto vstream = p_format_ctx_->streams[video_index_];
        frame_index_++;
        // find pts information
        if (AV_NOPTS_VALUE == packet_.pts) {
            LOG_INFO("url:" << info_.url << " VideoParserAsync::ParseLoop(): Didn't find pts informations, use ordered numbers instead.");
            packet_.pts = frame_index_;
        } else {
            packet_.pts = av_rescale_q(packet_.pts, vstream->time_base, {1, 90000});
        }

        if (saver_) {
            saver_->Write(reinterpret_cast<char *>(packet_.data), packet_.size);
        }

        LOG_DEBUG("url:" << info_.url << " VideoParserAsync::ParseLoop(): Get video packet, size=" << packet_.size
                     << ", pts=" << packet_.pts << ", dts=" << packet_.dts
                     << ", stream_index=" << packet_.stream_index << ", is key_frame=" << ((packet_.flags & AV_PKT_FLAG_KEY) ? "yes" : "no"));

        // 异步处理数据包
        if (handler_ && !AsyncProcessPacket(&packet_, frame_index_)) {
            LOG_WARN("url:" << info_.url << " Failed to queue packet for async processing");
        }

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

bool VideoParserAsync::isRecoverableError(int error_code) {
    // 定义哪些错误是可以恢复的
    switch (error_code) {
        case AVERROR(EAGAIN):    // 需要重试
        case AVERROR_INVALIDDATA: // 无效数据，跳过当前包
            return true;
        default:
            return false;
    }
}

// 异步处理相关方法实现
bool VideoParserAsync::AsyncProcessPacket(const AVPacket* packet, int64_t frame_index) {
    if (!async_running_) {
        LOG_WARN("Async processing not running");
        return false;
    }
    
    // 检查队列大小，防止内存溢出
    {
        std::lock_guard<std::mutex> lock(async_queue_mutex_);
        
        if (async_packet_queue_.size() >= max_async_queue_size_) {
            // 队列满了，可以根据策略处理（这里简单丢弃最旧的包）
            LOG_WARN("Async queue full (" << async_packet_queue_.size() << " packets), dropping oldest packet");
            
            if (!async_packet_queue_.empty()) {
                async_packet_queue_.pop();
                dropped_packets_++;
                
                // 定期报告丢弃的包数量
                if (dropped_packets_ % 10 == 0) {
                    LOG_WARN("Total dropped packets: " << dropped_packets_);
                }
            }
        }
        
        // 将数据包加入异步队列
        try {
            async_packet_queue_.emplace(std::make_unique<AsyncPacket>(packet, frame_index));
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to create async packet: " << e.what());
            return false;
        }
    }
    
    // 通知异步处理线程有新数据
    async_queue_cv_.notify_one();
    return true;
}

void VideoParserAsync::SendEosToAsyncQueue() {
    // 设置EOS标志
    async_eos_sent_ = true;
    // 通知异步处理线程检查EOS
    async_queue_cv_.notify_one();
}

void VideoParserAsync::AsyncProcessingThread() {
    LOG_INFO("Async processing thread started");
    
    while (async_running_) {
        std::unique_ptr<AsyncPacket> packet;
        
        // 从队列中获取数据包
        {
            std::unique_lock<std::mutex> lock(async_queue_mutex_);
            
            // 等待数据包或停止信号
            async_queue_cv_.wait(lock, [this]() {
                return !async_packet_queue_.empty() || !async_running_ || async_eos_sent_;
            });
            
            if (!async_running_) {
                break;
            }
            
            // 检查是否应该发送EOS
            if (async_eos_sent_ && async_packet_queue_.empty()) {
                LOG_INFO("Async queue empty and EOS sent, calling handler OnEos");
                if (handler_) {
                    handler_->OnEos();
                }
                async_eos_sent_ = false;
                continue;
            }
            
            if (async_packet_queue_.empty()) {
                continue;
            }
            
            // 取出数据包
            packet = std::move(async_packet_queue_.front());
            async_packet_queue_.pop();
        }
        
        // 处理数据包
        if (packet && handler_) {
            try {
                // 计算队列延迟（用于监控）
                auto now = std::chrono::steady_clock::now();
                auto queue_delay = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - packet->enqueue_time).count();
                
                if (queue_delay > 100) { // 延迟超过100ms
                    LOG_WARN("Packet queue delay: " << queue_delay << "ms for frame " << packet->frame_index);
                }
                
                // 调用handler处理数据包
                bool success = handler_->OnPacket(&packet->packet, packet->frame_index);
                if (!success) {
                    LOG_WARN("Handler failed to process packet for frame " << packet->frame_index);
                }
                
                processed_packets_++;
                
                // 定期报告处理进度
                if (processed_packets_ % 100 == 0) {
                    LOG_INFO("Async processed packets: " << processed_packets_ << ", queue size: " << async_packet_queue_.size());
                }
                
            } catch (const std::exception& e) {
                LOG_ERROR("Exception in async processing: " << e.what());
            }
        }
    }
    
    LOG_INFO("Async processing thread stopped");
}

void VideoParserAsync::StopAsyncProcessing() {
    if (!async_running_) {
        return;
    }
    
    LOG_INFO("Stopping async processing thread");
    async_running_ = false;
    async_queue_cv_.notify_all();
    
    if (async_thread_.joinable()) {
        async_thread_.join();
    }
    
    // 清空队列
    ClearAsyncQueue();
    
    LOG_INFO("Async processing stopped, processed packets: " << processed_packets_ << ", dropped packets: " << dropped_packets_);
}

void VideoParserAsync::ClearAsyncQueue() {
    std::lock_guard<std::mutex> lock(async_queue_mutex_);
    
    size_t cleared_count = async_packet_queue_.size();
    while (!async_packet_queue_.empty()) {
        async_packet_queue_.pop();
    }
    
    if (cleared_count > 0) {
        LOG_INFO("Cleared " << cleared_count << " packets from async queue");
    }
    
    async_eos_sent_ = false;
    processed_packets_ = 0;
    dropped_packets_ = 0;
}

}