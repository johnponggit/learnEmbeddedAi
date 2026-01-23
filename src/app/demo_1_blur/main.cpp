
#include "decoderManager.h"
#include "html_page.h"


using json = nlohmann::json;

// 创建通用 JSON 响应
std::string create_json_response(bool success, const std::string& error = "") {
    json response;
    response["success"] = success;
    
    if (!error.empty()) {
        response["error"] = error;
    }
    
    return response.dump(); // 返回 JSON 字符串
}

// 创建状态 JSON
std::string create_status_json(bool is_streaming, const std::string& current_url = "") {
    json response;
    response["is_streaming"] = is_streaming;
    
    if (!current_url.empty()) {
        response["current_url"] = current_url;
    }
    
    return response.dump(); // 返回 JSON 字符串
}


// RTSP解码器类
class RTSPDecoder {
private:
    std::string rtsp_url;
    std::atomic<bool> running{false};
    std::thread decode_thread;
    
    AVFormatContext* fmt_ctx = nullptr;
    AVCodecContext* codec_ctx = nullptr;
    int video_stream_idx = -1;
    
    emai::YUVFrameBuffer& frame_buffer;
    std::mutex decoder_mutex;
    
    // 重连相关变量
    int consecutive_eof_count = 0;
    const int MAX_CONSECUTIVE_EOF = 5;
    int64_t last_reconnect_time = 0;
    const int64_t RECONNECT_INTERVAL = 5000; // 5秒
    
public:
    RTSPDecoder(const std::string& url, emai::YUVFrameBuffer& buffer)
        : rtsp_url(url), frame_buffer(buffer) {}
    
    ~RTSPDecoder() {
        stop();
    }
    
    bool start() {
        if (running) return false;
        
        std::lock_guard<std::mutex> lock(decoder_mutex);
        
        // 清理之前的缓冲区
        frame_buffer.clear();
        
        // 初始化FFmpeg
        avformat_network_init();
        
        // 打开RTSP流
        AVDictionary* options = nullptr;
        av_dict_set(&options, "rtsp_transport", "tcp", 0);
        av_dict_set(&options, "stimeout", "5000000", 0); // 5秒超时
        
        std::cout << "Connecting to RTSP stream: " << rtsp_url << std::endl;
        
        if (avformat_open_input(&fmt_ctx, rtsp_url.c_str(), nullptr, &options) != 0) {
            std::cerr << "Could not open RTSP stream: " << rtsp_url << std::endl;
            av_dict_free(&options);
            return false;
        }
        av_dict_free(&options);
        
        if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
            std::cerr << "Could not find stream information" << std::endl;
            avformat_close_input(&fmt_ctx);
            return false;
        }
        
        // 查找视频流 - 修复段错误：添加nullptr检查
        video_stream_idx = -1;
        for (unsigned int i = 0; i < fmt_ctx->nb_streams; i++) {
            // 添加nullptr检查
            if (fmt_ctx->streams[i] && 
                fmt_ctx->streams[i]->codecpar && 
                fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                video_stream_idx = i;
                break;
            }
        }
        
        if (video_stream_idx == -1) {
            std::cerr << "Could not find video stream" << std::endl;
            avformat_close_input(&fmt_ctx);
            return false;
        }
        
        // 获取解码器 - 添加更多安全检查
        AVStream* video_stream = fmt_ctx->streams[video_stream_idx];
        if (!video_stream || !video_stream->codecpar) {
            std::cerr << "Invalid video stream or codec parameters" << std::endl;
            avformat_close_input(&fmt_ctx);
            return false;
        }
        
        AVCodecParameters* codecpar = video_stream->codecpar;
        const AVCodec* codec = avcodec_find_decoder(codecpar->codec_id);
        if (!codec) {
            std::cerr << "Unsupported codec: " << avcodec_get_name(codecpar->codec_id) << std::endl;
            avformat_close_input(&fmt_ctx);
            return false;
        }
        
        codec_ctx = avcodec_alloc_context3(codec);
        if (!codec_ctx) {
            std::cerr << "Could not allocate codec context" << std::endl;
            avformat_close_input(&fmt_ctx);
            return false;
        }
        
        if (avcodec_parameters_to_context(codec_ctx, codecpar) < 0) {
            std::cerr << "Could not copy codec parameters to context" << std::endl;
            avcodec_free_context(&codec_ctx);
            avformat_close_input(&fmt_ctx);
            return false;
        }
        
        if (avcodec_open2(codec_ctx, codec, nullptr) < 0) {
            std::cerr << "Could not open codec" << std::endl;
            avcodec_free_context(&codec_ctx);
            avformat_close_input(&fmt_ctx);
            return false;
        }
        
        std::cout << "RTSP stream connected successfully" << std::endl;
        std::cout << "Video codec: " << avcodec_get_name(codecpar->codec_id) << std::endl;
        std::cout << "Resolution: " << codecpar->width << "x" << codecpar->height << std::endl;
        std::cout << "Pixel format: " << av_get_pix_fmt_name((AVPixelFormat)codecpar->format) << std::endl;
        
        running = true;
        decode_thread = std::thread(&RTSPDecoder::decode_loop, this);
        
        return true;
    }
    
    void stop() {
        running = false;
        if (decode_thread.joinable()) {
            decode_thread.join();
        }
        
        std::lock_guard<std::mutex> lock(decoder_mutex);
        if (codec_ctx) {
            avcodec_free_context(&codec_ctx);
            codec_ctx = nullptr;
        }
        if (fmt_ctx) {
            avformat_close_input(&fmt_ctx);
            fmt_ctx = nullptr;
        }
        
        avformat_network_deinit();
    }
    
    bool is_running() const {
        return running;
    }
    
    std::string get_url() const {
        return rtsp_url;
    }
    
private:
    void decode_loop() {
        AVFrame* frame = av_frame_alloc();
        AVPacket* pkt = av_packet_alloc();
        SwsContext* sws_ctx = nullptr;
        int64_t last_frame_time = 0;
        const int64_t min_frame_interval = 40; // 最小帧间隔 40ms (~25fps)
        int frame_count = 0;
        int consecutive_eof_count = 0;  // 连续EOF计数
        const int MAX_CONSECUTIVE_EOF = 5; // 最大连续EOF次数
        int64_t last_reconnect_time = 0;
        const int64_t RECONNECT_INTERVAL = 5000; // 重连间隔 5秒

        while (running) {
            std::unique_lock<std::mutex> lock(decoder_mutex);
            
            if (!fmt_ctx) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            
            int ret = av_read_frame(fmt_ctx, pkt);
            if (ret < 0) {
                // 处理各种错误情况
                if (ret == AVERROR_EOF) {
                    consecutive_eof_count++;
                    
                    // 如果连续EOF次数过多，尝试重新连接
                    if (consecutive_eof_count >= MAX_CONSECUTIVE_EOF) {
                        int64_t current_time = av_gettime() / 1000;
                        if (current_time - last_reconnect_time >= RECONNECT_INTERVAL) {
                            std::cout << "Too many EOFs, attempting to reconnect RTSP stream: " << rtsp_url << std::endl;
                            
                            // 保存当前设置
                            int video_idx = video_stream_idx;
                            AVCodecParameters* saved_params = nullptr;
                            if (codec_ctx) {
                                saved_params = avcodec_parameters_alloc();
                                avcodec_parameters_from_context(saved_params, codec_ctx);
                            }
                            
                            // 关闭当前连接
                            if (codec_ctx) {
                                avcodec_free_context(&codec_ctx);
                                codec_ctx = nullptr;
                            }
                            if (fmt_ctx) {
                                avformat_close_input(&fmt_ctx);
                                fmt_ctx = nullptr;
                            }
                            
                            // 清空缓冲区
                            frame_buffer.clear();
                            
                            // 重新连接
                            AVDictionary* options = nullptr;
                            av_dict_set(&options, "rtsp_transport", "tcp", 0);
                            av_dict_set(&options, "stimeout", "5000000", 0); // 5秒超时
                            av_dict_set(&options, "reconnect", "1", 0); // 启用重连
                            av_dict_set(&options, "reconnect_at_eof", "1", 0); // EOF时重连
                            av_dict_set(&options, "reconnect_streamed", "1", 0); // 流式重连
                            
                            std::cout << "Reconnecting to RTSP stream..." << std::endl;
                            ret = avformat_open_input(&fmt_ctx, rtsp_url.c_str(), nullptr, &options);
                            av_dict_free(&options);
                            
                            if (ret == 0) {
                                ret = avformat_find_stream_info(fmt_ctx, nullptr);
                                if (ret >= 0) {
                                    // 重新查找视频流
                                    video_stream_idx = -1;
                                    for (unsigned int i = 0; i < fmt_ctx->nb_streams; i++) {
                                        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                                            video_stream_idx = i;
                                            break;
                                        }
                                    }
                                    
                                    if (video_stream_idx != -1) {
                                        // 重新创建解码器上下文
                                        AVCodecParameters* codecpar = fmt_ctx->streams[video_stream_idx]->codecpar;
                                        const AVCodec* codec = avcodec_find_decoder(codecpar->codec_id);
                                        if (codec) {
                                            codec_ctx = avcodec_alloc_context3(codec);
                                            if (codec_ctx) {
                                                avcodec_parameters_to_context(codec_ctx, codecpar);
                                                if (avcodec_open2(codec_ctx, codec, nullptr) >= 0) {
                                                    std::cout << "RTSP stream reconnected successfully" << std::endl;
                                                    consecutive_eof_count = 0;
                                                    last_reconnect_time = current_time;
                                                    av_packet_unref(pkt);
                                                    continue;
                                                } else {
                                                    avcodec_free_context(&codec_ctx);
                                                    codec_ctx = nullptr;
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                            
                            // 重连失败，恢复原来的设置
                            std::cerr << "Reconnection failed, resetting connection..." << std::endl;
                            if (saved_params) {
                                avcodec_parameters_free(&saved_params);
                            }
                            
                            // 尝试更彻底的重启
                            stop();
                            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                            
                            // 重新初始化
                            avformat_network_init();
                            AVDictionary* retry_options = nullptr;
                            av_dict_set(&retry_options, "rtsp_transport", "tcp", 0);
                            av_dict_set(&retry_options, "stimeout", "10000000", 0); // 10秒超时
                            av_dict_set(&retry_options, "max_delay", "5000000", 0); // 最大延迟
                            
                            if (avformat_open_input(&fmt_ctx, rtsp_url.c_str(), nullptr, &retry_options) == 0) {
                                if (avformat_find_stream_info(fmt_ctx, nullptr) >= 0) {
                                    // 重新查找视频流
                                    video_stream_idx = -1;
                                    for (unsigned int i = 0; i < fmt_ctx->nb_streams; i++) {
                                        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                                            video_stream_idx = i;
                                            break;
                                        }
                                    }
                                    
                                    if (video_stream_idx != -1) {
                                        AVCodecParameters* codecpar = fmt_ctx->streams[video_stream_idx]->codecpar;
                                        const AVCodec* codec = avcodec_find_decoder(codecpar->codec_id);
                                        if (codec) {
                                            codec_ctx = avcodec_alloc_context3(codec);
                                            avcodec_parameters_to_context(codec_ctx, codecpar);
                                            if (avcodec_open2(codec_ctx, codec, nullptr) >= 0) {
                                                std::cout << "RTSP stream successfully reinitialized" << std::endl;
                                                consecutive_eof_count = 0;
                                                last_reconnect_time = current_time;
                                            }
                                        }
                                    }
                                }
                            }
                            av_dict_free(&retry_options);
                        } else {
                            // 还没到重连时间，等待
                            std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        }
                    } else {
                        // 正常EOF，尝试seek到开头
                        std::cout << "End of stream reached, seeking to beginning..." << std::endl;
                        av_seek_frame(fmt_ctx, -1, 0, AVSEEK_FLAG_BACKWARD);
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    }
                } else {
                    // 其他错误，重置EOF计数
                    consecutive_eof_count = 0;
                    
                    // 网络错误，短暂等待后继续
                    if (ret == AVERROR(EAGAIN)) {
                        // 资源暂时不可用，短暂等待
                        lock.unlock();
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        lock.lock();
                    } else if (ret == AVERROR_EXIT) {
                        // 解码器退出，需要重新初始化
                        std::cerr << "Decoder exited, attempting to restart..." << std::endl;
                        lock.unlock();
                        stop();
                        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                        start();
                        break;
                    } else {
                        // 其他错误，等待后重试
                        std::cerr << "Error reading frame (code: " << ret << "), retrying..." << std::endl;
                        lock.unlock();
                        std::this_thread::sleep_for(std::chrono::milliseconds(500));
                        lock.lock();
                    }
                }
                
                av_packet_unref(pkt);
                continue;
            }
            
            // 重置连续EOF计数
            consecutive_eof_count = 0;
            
            if (pkt->stream_index == video_stream_idx) {
                ret = avcodec_send_packet(codec_ctx, pkt);
                if (ret < 0) {
                    // 发送包失败，可能是解码器问题
                    if (ret == AVERROR(EAGAIN)) {
                        // 解码器需要更多输出帧
                        avcodec_receive_frame(codec_ctx, frame);
                        av_frame_unref(frame);
                    } else if (ret == AVERROR_INVALIDDATA) {
                        // 无效数据，跳过
                        std::cerr << "Invalid packet data, skipping..." << std::endl;
                    } else if (ret == AVERROR_EXIT) {
                        // 解码器退出
                        std::cerr << "Decoder exited, attempting to restart..." << std::endl;
                        lock.unlock();
                        stop();
                        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                        start();
                        break;
                    }
                    
                    av_packet_unref(pkt);
                    continue;
                }
                
                while (ret >= 0) {
                    ret = avcodec_receive_frame(codec_ctx, frame);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                        break;
                    } else if (ret < 0) {
                        // 解码错误
                        std::cerr << "Error decoding frame, skipping..." << std::endl;
                        break;
                    }
                    
                    // 控制帧率：确保帧间隔不小于最小值
                    int64_t current_time = av_gettime() / 1000; // 毫秒
                    if (current_time - last_frame_time < min_frame_interval) {
                        av_frame_unref(frame);
                        break;
                    }
                    last_frame_time = current_time;
                    
                    // 降低帧率，每3帧取1帧
                    if (++frame_count % 3 != 0) {
                        av_frame_unref(frame);
                        break;
                    }
                    
                    // 转换到YUV420P格式（如果需要）
                    AVFrame* converted_frame = frame;
                    
                    if (frame->format != AV_PIX_FMT_YUV420P) {
                        if (!sws_ctx) {
                            sws_ctx = sws_getContext(
                                frame->width, frame->height,
                                (AVPixelFormat)frame->format,
                                frame->width, frame->height,
                                AV_PIX_FMT_YUV420P,
                                SWS_BILINEAR, nullptr, nullptr, nullptr
                            );
                        }
                        
                        if (sws_ctx) {
                            AVFrame* yuv_frame = av_frame_alloc();
                            yuv_frame->width = frame->width;
                            yuv_frame->height = frame->height;
                            yuv_frame->format = AV_PIX_FMT_YUV420P;
                            yuv_frame->pts = frame->pts;
                            av_frame_get_buffer(yuv_frame, 0);
                            
                            sws_scale(sws_ctx, frame->data, frame->linesize,
                                    0, frame->height,
                                    yuv_frame->data, yuv_frame->linesize);
                            
                            converted_frame = yuv_frame;
                        }
                    }
                    
                    // 存储YUV数据
                    emai::YUVFrame yuv_frame(converted_frame);
                    if (!yuv_frame.empty()) {
                        frame_buffer.push(yuv_frame);
                    }
                    
                    // 清理临时帧
                    if (converted_frame != frame) {
                        av_frame_free(&converted_frame);
                    }
                    
                    av_frame_unref(frame);
                    break;  // 每次只处理一个帧
                }
            }
            
            av_packet_unref(pkt);
            
            // 短暂休眠，避免占用过多CPU
            lock.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            lock.lock();
        }
        
        if (sws_ctx) {
            sws_freeContext(sws_ctx);
        }
        av_frame_free(&frame);
        av_packet_free(&pkt);
    }
};


void print_usage(int port) {
    LOG_INFO("RTSP视频流模糊处理器服务器");
    LOG_INFO("============================================");
    LOG_INFO("服务器启动在端口 " << port);
    LOG_INFO("在浏览器中打开 http://localhost:" << port);
    LOG_INFO("可用端点:");
    LOG_INFO("  GET  /                      - 网页界面");
    LOG_INFO("  GET  /video_frame           - 获取处理后的视频帧");
    LOG_INFO("  POST /start_stream          - 启动RTSP流");
    LOG_INFO("  GET  /stream_status         - 获取流状态");
    LOG_INFO("  POST /update_blur_settings  - 更新模糊设置");
    LOG_INFO("  GET  /get_blur_settings     - 获取当前模糊设置");
    LOG_INFO("功能说明:");
    LOG_INFO("  1. 只使用一路RTSP流");
    LOG_INFO("  2. 支持圆形和矩形两种模糊区域");
    LOG_INFO("  3. 支持鼠标点击视频画面定位模糊区域");
    LOG_INFO("  4. 页面刷新时会自动应用当前设置");
    LOG_INFO("  5. 可实时调整模糊区域的位置、大小和模糊半径");
    LOG_INFO("  6. 可启用或禁用模糊效果");
    LOG_INFO("  7. 使用高斯模糊算法实现平滑的模糊效果");
}

int httpServer(int port) {
    // 创建HTTP服务器
    httplib::Server server;
    DecoderManager::Ptr decoder_manager = std::make_shared<DecoderManager>();

    // 主页
    server.Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(HTML_PAGE, "text/html");
    });
    
    // 获取处理后的视频帧
    server.Get("/video_frame", [&decoder_manager](const httplib::Request& req, httplib::Response& res) {
        std::vector<uint8_t> frame;
        if (decoder_manager->get_processed_frame(frame)) {
            res.set_content(reinterpret_cast<char*>(frame.data()), frame.size(), "image/jpeg");
        } else {
            // 返回空白图像
            res.set_content(reinterpret_cast<const char*>(BLANK_JPEG), sizeof(BLANK_JPEG), "image/jpeg");
        }
    });
    
    // 启动流
    server.Post("/start_stream", [&decoder_manager](const httplib::Request& req, httplib::Response& res) {
        std::string rtsp_url = req.get_param_value("rtsp_url");
        
        if (rtsp_url.empty()) {
            res.set_content(create_json_response(false, "RTSP URL is required"), "application/json");
            return;
        }
        
        if (decoder_manager->start_stream(rtsp_url)) {
            res.set_content(create_json_response(true), "application/json");
        } else {
            res.set_content(create_json_response(false, "Failed to connect to RTSP stream"), "application/json");
        }
    });
    
    // 停止流
    server.Post("/stop_stream", [&decoder_manager](const httplib::Request&, httplib::Response& res) {
        decoder_manager->stop_all();
        res.set_content(create_json_response(true), "application/json");
    });
    
    // 获取流状态
    server.Get("/stream_status", [&decoder_manager](const httplib::Request&, httplib::Response& res) {
        res.set_content(create_status_json(
            decoder_manager->is_streaming(),
            decoder_manager->get_current_url()
        ), "application/json");
    });
    
    // 更新模糊设置
    server.Post("/update_blur_settings", [&decoder_manager](const httplib::Request& req, httplib::Response& res) {
        std::string x_str = req.get_param_value("x");
        std::string y_str = req.get_param_value("y");
        std::string width_str = req.get_param_value("width");
        std::string height_str = req.get_param_value("height");
        std::string blur_radius_str = req.get_param_value("blur_radius");
        std::string border_size_str = req.get_param_value("border_size");
        std::string enabled_str = req.get_param_value("enabled");
        std::string shape_str = req.get_param_value("shape");
        
        if (x_str.empty() || y_str.empty() || width_str.empty() || height_str.empty()) {
            res.set_content(create_json_response(false, "Position and size parameters are required"), "application/json");
            return;
        }
        
        try {
            int x = std::stoi(x_str);
            int y = std::stoi(y_str);
            int width = std::stoi(width_str);
            int height = std::stoi(height_str);
            
            int blur_radius = blur_radius_str.empty() ? 5 : std::stoi(blur_radius_str);
            int border_size = border_size_str.empty() ? 2 : std::stoi(border_size_str);
            bool enabled = enabled_str.empty() ? true : (enabled_str == "true");
            std::string shape = shape_str.empty() ? "circle" : shape_str;
            
            if (decoder_manager->update_blur_settings(x, y, width, height,
                                                   blur_radius, border_size, enabled, shape)) {
                res.set_content(create_json_response(true), "application/json");
            } else {
                res.set_content(create_json_response(false, "Invalid blur settings"), "application/json");
            }
        } catch (const std::exception& e) {
            res.set_content(create_json_response(false, "Invalid parameter format"), "application/json");
        }
    });
    
    // 获取当前模糊设置
    server.Get("/get_blur_settings", [&decoder_manager](const httplib::Request&, httplib::Response& res) {
        res.set_content(decoder_manager->get_blur_settings_json(), "application/json");
    });
    
    // 设置HTTP服务器选项
    server.set_keep_alive_max_count(100);
    server.set_read_timeout(10, 0); // 10秒读超时
    server.set_write_timeout(10, 0); // 10秒写超时
    
    // 启动服务器
    LOG_INFO("按 Ctrl+C 停止服务器");
    
    if (!server.listen("0.0.0.0", port)) {
        LOG_ERROR("无法在端口 " << port << " 启动服务器");
        return 1;
    }
    return 0;
}

int main(int argc, char* argv[]) {
    
    int port = 38080;
    
    if (argc >= 2) {
        port = std::stoi(argv[1]);
    }    
    
    print_usage(port);
    return httpServer(port);       
}