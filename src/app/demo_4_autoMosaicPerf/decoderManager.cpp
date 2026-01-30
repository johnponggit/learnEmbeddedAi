
#include "decoderManager.h"
#include <iomanip>


DecoderManager::DecoderManager() : decodedFrameBuffer_(std::make_unique<emai::YUVFrameBuffer>()),
                                   jpegBuffer_(std::make_unique<std::queue<std::vector<uint8_t>>>()), 
                                   mosaicProcessor_(std::make_unique<MosaicProcessor>()) 
                                {
    lastStatTime_ = av_gettime() / 1000;

    if (mosaicProcessor_)
    {
        mosaicProcessor_->getDstWidthHeight(dst_width_, dst_height_);
    }

    detector_ = std::make_unique<emai::RknnYolov5Detector>();
    detector_->init();
}
    

DecoderManager::~DecoderManager() 
{
    stop_all();
}
    
int DecoderManager::start_stream(const std::string& rtsp_url) 
{

    LOG_INFO("Starting stream: " << rtsp_url);
    bool bRet = false;

    std::lock_guard<std::mutex> lock(managerMutex_);
    
    if (parserStartOk_)
    {
        LOG_WARN("VideoParser is already running, url:" << currentUrl_);
        return bRet = true;
    }

    if (rtsp_url.empty()) {
        LOG_ERROR("RTSP URL is required");
        return bRet;
    }
    
    currentUrl_ = rtsp_url;
    decodedFrameBuffer_->clear();

    LOG_INFO("Creating VideoDecoder type: FFMPEG_CPU");
    decoder_ = std::make_shared<emai::VideoDecoder>(shared_from_this(), emai::FFMPEG_CPU, -1); 
    // decoder_ = std::make_shared<emai::VideoDecoder>(shared_from_this(), emai::FFMPEG_RKMPP, -1); asdftest
    if (!decoder_)
    {
        LOG_ERROR("create VideoDecoder failed");
        return bRet;
    }

    LOG_INFO("Creating VideoParser");
    parser_ = std::make_shared<emai::VideoParser>(decoder_);
    if (!parser_)
    {
        LOG_ERROR("create VideoParser failed");
        return bRet;
    }

    bRet = parser_->Open(rtsp_url.c_str(), false);
    if (!bRet)
    {
        LOG_ERROR("VideoParser Open failed, url:" << currentUrl_);
        parser_.reset();
        return bRet;
    }

    parser_->setParseLoopRunning(true);
    parserStartOk_.store(true);

    parserThread_ = std::thread([this]() {
        LOG_INFO("VideoParser ParseLoop thread start, url:" << parser_->GetVideoInfo().url);

        while (parserStartOk_.load())
        {
            parser_->ParseLoop(1000 / parser_->GetVideoInfo().fps);
            parser_->Close();
            
            LOG_INFO("VideoParser ParseLoop to restart, url:" << currentUrl_);
            
            if (!parser_->Open(currentUrl_.c_str(), false))
            {
                LOG_ERROR("VideoParser Open failed, url:" << currentUrl_);
                std::this_thread::sleep_for(std::chrono::milliseconds(3000));
            }
            parser_->setParseLoopRunning(true);
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        LOG_INFO("VideoParser ParseLoop thread exit, url:" << parser_->GetVideoInfo().url);
    });


    if (!processing_) {
        processing_ = true;
        processThread_ = std::thread(&DecoderManager::processing_loop, this);
    }

    // 启动广播线程
    if (!broadcasting_) {
        broadcasting_ = true;
        broadcast_thread_ = std::thread(&DecoderManager::stream_broadcast_loop, this);
        LOG_INFO("Stream broadcast thread started");
    }

    init_h264_encoder(parser_->GetVideoInfo().width, parser_->GetVideoInfo().height);

    LOG_INFO("VideoParser ParseLoop thread start successfully, url:" << currentUrl_);

    return bRet = true;
}
    
void DecoderManager::stop_all() 
{
    LOG_INFO("in stop_all");

    std::lock_guard<std::mutex> lock(managerMutex_);

    {
        std::lock_guard<std::mutex> lock(jpegBufferMutex_);
        if (jpegBuffer_) {
            while (!jpegBuffer_->empty()) {
                jpegBuffer_->pop();
            }
        }
    }

    if (processThread_.joinable()) {
        processing_ = false;
        processThread_.join();
    }

    // 停止广播线程
    if (broadcast_thread_.joinable()) {
        broadcasting_ = false;
        h264_queue_cv_.notify_all();  // 唤醒等待的线程
        broadcast_thread_.join();
        LOG_INFO("Stream broadcast thread stopped");
    }

    parserStartOk_.store(false);
    if (parser_) {parser_->setParseLoopRunning(false);}

    if (parserThread_.joinable())
    {
        parserThread_.join();
    }

    if (parser_)
    {
        parser_->Close();
        parser_.reset();
    }

    if (decoder_)
    {
        decoder_.reset();
    }

    if (decodedFrameBuffer_) {
        decodedFrameBuffer_->clear();
    }

    if (decodedYuvSwsCtx_) {
        sws_freeContext(decodedYuvSwsCtx_);
        decodedYuvSwsCtx_ = nullptr;
    }

    // 清理H.264编码器和队列
    if (h264_encoder_) {
        h264_encoder_.reset();
        LOG_INFO("H.264 encoder released");
    }
    
    {
        std::lock_guard<std::mutex> lock(h264_queue_mutex_);
        while (!h264_frame_queue_.empty()) {
            h264_frame_queue_.pop();
        }
        LOG_INFO("H.264 frame queue cleared");
    }
    
    // 清理所有客户端
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        stream_clients_.clear();
        LOG_INFO("All stream clients cleared");
    }


    LOG_INFO("stop_all ok");
}
    
bool DecoderManager::get_processed_frame(std::vector<uint8_t>& frame) {
    static std::vector<uint8_t> last_frame;  // 静态变量存储最后一帧
    
    std::lock_guard<std::mutex> lock(jpegBufferMutex_);
    
    if (jpegBuffer_ && !jpegBuffer_->empty()) {
        frame = jpegBuffer_->front();
        jpegBuffer_->pop();
        last_frame = frame;  // 保存为最后一帧
        return true;
    }
    
    // 如果没有新帧，返回最后一帧
    if (!last_frame.empty()) {
        frame = last_frame;
        return true;
    }
    
    return false;
}
    


bool DecoderManager::update_mosaic_settings(int x, int y, int width, int height, int block_size, int border_size, bool enabled) {
    std::lock_guard<std::mutex> lock(managerMutex_);
    if (!mosaicProcessor_) return false;
    
    if (width <= 0 || height <= 0 || width > 800 || height > 600) {
        return false;
    }
    
    if (x < 0 || y < 0 || x + width > 800 || y + height > 600) {
        return false;
    }
    
    mosaicProcessor_->update_mosaic_settings(x, y, width, height, block_size, border_size, enabled);
    return true;
}
    


std::string DecoderManager::get_mosaic_settings_json() 
{
    std::lock_guard<std::mutex> lock(managerMutex_);
    if (!mosaicProcessor_) return "{}";
    
    auto settings = mosaicProcessor_->get_settings();
    std::stringstream ss;
    ss << "{";
    ss << "\"x\":" << settings.x << ",";
    ss << "\"y\":" << settings.y << ",";
    ss << "\"width\":" << settings.width << ",";
    ss << "\"height\":" << settings.height << ",";
    ss << "\"block_size\":" << settings.block_size << ",";
    ss << "\"border_size\":" << settings.border_size << ",";
    ss << "\"enabled\":" << (settings.enabled ? "true" : "false");
    ss << "}";
    return ss.str();
}
    
std::string DecoderManager::get_current_url() 
{
    std::lock_guard<std::mutex> lock(managerMutex_);
    return currentUrl_;
}
    
bool DecoderManager::is_streaming() 
{
    std::lock_guard<std::mutex> lock(managerMutex_);
    return parserStartOk_;
}    

#if 0
void DecoderManager::processing_loop() 
{
    std::vector<uint8_t> last_successful_frame;  // 存储最后一帧成功处理的帧
    int64_t last_process_time = 0;
    const int64_t target_process_interval = 33; // 目标处理间隔 33ms (~30fps)
    
    while (processing_) {
        int64_t current_time = av_gettime() / 1000; // 毫秒
        
        #if 0
        // 控制处理帧率
        if (current_time - last_process_time < target_process_interval) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        #endif 
        
        last_process_time = current_time;
        
        emai::YUVFrame frame;
        bool has_frame = false;
        
        // 性能统计变量
        double decode_ms = 0;
        double detect_ms = 0;
        double mosaic_ms = 0;
        double encode_ms = 0;
        auto total_start = std::chrono::high_resolution_clock::now();
        
        {
            std::lock_guard<std::mutex> lock(managerMutex_);  
            
            // 尝试获取帧，设置超时
            auto get_frame_start = std::chrono::high_resolution_clock::now();
            has_frame = decodedFrameBuffer_->get_latest(frame, 10);
            auto get_frame_end = std::chrono::high_resolution_clock::now();
            decode_ms = std::chrono::duration_cast<std::chrono::microseconds>(get_frame_end - get_frame_start).count() / 1000.0;
        }
        
        std::vector<uint8_t> processed_frame;
        
        if (has_frame) {
            updateMosaicSettingsByDetect(frame, detect_ms);    

            // 处理帧：应用马赛克
            processed_frame = mosaicProcessor_->process_and_encode(frame, mosaic_ms, encode_ms);
            auto total_end = std::chrono::high_resolution_clock::now();
            double total_ms = std::chrono::duration_cast<std::chrono::microseconds>(total_end - total_start).count() / 1000.0;

            LOG_DEBUG("Processing frame done, time: " << total_ms << " ms");

            if (!processed_frame.empty()) {
                last_successful_frame = processed_frame;
                processedFrameCnt_++;
                
                // 更新性能统计
                {
                    std::lock_guard<std::mutex> lock(perfMutex_);
                    perfStats_.decode_ms = decode_ms;
                    perfStats_.detect_ms = detect_ms;
                    perfStats_.mosaic_ms = mosaic_ms;
                    perfStats_.encode_ms = encode_ms;
                    perfStats_.total_ms = total_ms;
                }
                
                std::lock_guard<std::mutex> lock(jpegBufferMutex_);
                if (jpegBuffer_) {
                    // 限制缓冲区大小，只保留最新的一帧
                    while (!jpegBuffer_->empty()) {
                        jpegBuffer_->pop();
                    }
                    jpegBuffer_->push(processed_frame);
                }
            }
        }
        
        // 如果处理失败但有上一次成功的帧，使用上一次的帧
        if (processed_frame.empty() && !last_successful_frame.empty()) {
            std::lock_guard<std::mutex> lock(jpegBufferMutex_);
            if (jpegBuffer_) {
                while (!jpegBuffer_->empty()) {
                    jpegBuffer_->pop();
                }
                jpegBuffer_->push(last_successful_frame);
            }
        }
        
        // 性能统计
        int64_t now = av_gettime() / 1000;
        if (now - lastStatTime_ >= 1000) { // 每1秒输出一次统计
            int current_fps = (processedFrameCnt_ * 1000 / (now - lastStatTime_));
            LOG_INFO("处理帧率: " << current_fps << " fps");
            
            {
                std::lock_guard<std::mutex> lock(perfMutex_);
                perfStats_.fps = current_fps;
            }
            
            processedFrameCnt_ = 0;
            lastStatTime_ = now;
        }
        
        // 短暂休眠，避免占用过多CPU
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
#endif

void DecoderManager::processing_loop() 
{
    LOG_INFO("Processing loop started (H.264 streaming mode)");
    
    // 性能监控
    auto last_stats_time = std::chrono::steady_clock::now();
    int frames_processed = 0;
    int idle_cycles = 0;
    
    while (processing_) {
        auto frame_start = std::chrono::high_resolution_clock::now();
        
        // 1. 获取YUV帧
        emai::YUVFrame frame;
        bool frame_available = false;
        double decode_time = 0;
        
        {
            std::unique_lock<std::mutex> lock(managerMutex_, std::try_to_lock);
            if (lock.owns_lock()) {
                auto start = std::chrono::high_resolution_clock::now();
                frame_available = decodedFrameBuffer_->get_latest(frame, 0);
                auto end = std::chrono::high_resolution_clock::now();
                if (frame_available) 
                {
                    LOG_INFO("processing_loop fetch frame tm: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << "ms" <<
                             ", decodedFrameBuffer_ size: " << decodedFrameBuffer_->size() );
                }             

            }
        }
        
        if (!frame_available) {
            idle_cycles++;
            if (idle_cycles > 100) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                idle_cycles = 100;
            }
            continue;
        }
        
        idle_cycles = 0;
        frames_processed++;
        
        // 2. 目标检测
        double detect_time = 0;
        if (enable_auto_detect_) {
            updateMosaicSettingsByDetect(frame, detect_time); 
        }

        // 3. 马赛克处理
        double mosaic_time = 0;
        auto mosaic_start = std::chrono::high_resolution_clock::now();
        AVFrame* processed_yuv = mosaicProcessor_->process(frame, mosaic_time); 
        auto mosaic_end = std::chrono::high_resolution_clock::now();
        mosaic_time = std::chrono::duration_cast<std::chrono::microseconds>(
            mosaic_end - mosaic_start).count() / 1000.0;
        
        // 4. H.264硬件编码
        double encode_time = 0;
        if (h264_encoder_ && processed_yuv) {
            auto encode_start = std::chrono::high_resolution_clock::now();
            
            std::vector<uint8_t> h264_data;
            if (h264_encoder_->EncodeFrame(processed_yuv, h264_data)) {
                encode_time = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::high_resolution_clock::now() - encode_start).count() / 1000.0;
                
                // 推送到广播队列
                std::lock_guard<std::mutex> lock(h264_queue_mutex_);
        
                LOG_DEBUG("to push H.264 data, size: " << h264_data.size() << " bytes");
                h264_frame_queue_.push(std::move(h264_data));
                LOG_DEBUG("queue size: " << h264_frame_queue_.size());

                h264_queue_cv_.notify_one();
            }else
            {
                LOG_ERROR("Failed to encode frame");
            }

        }else
        {
            LOG_ERROR("H.264 encoder is not initialized or processed_yuv is null:" << (!h264_encoder_) << ", " << (!processed_yuv));
        }


        av_frame_unref(processed_yuv);
        
        // 5. 更新性能统计
        auto frame_end = std::chrono::high_resolution_clock::now();
        double total_time = std::chrono::duration_cast<std::chrono::microseconds>(
            frame_end - frame_start).count() / 1000.0;
        
        {
            std::lock_guard<std::mutex> lock(perfMutex_);
            perfStats_.decode_ms = decode_time;
            perfStats_.detect_ms = detect_time;
            perfStats_.mosaic_ms = mosaic_time;
            perfStats_.encode_ms = encode_time;
            perfStats_.total_ms = total_time;
        }
        
        // 6. 控制帧率
        auto now = std::chrono::steady_clock::now();
        auto frame_interval = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_stats_time).count();
        
        if (frame_interval >= 1000) { // 每秒输出统计
            double fps = frames_processed * 1000.0 / frame_interval;
            {
                std::lock_guard<std::mutex> lock(perfMutex_);
                perfStats_.fps = static_cast<int>(fps);
            }
            
            LOG_INFO("Processing: " << fps << " fps, "
                     << "Decode=" << decode_time << "ms, "
                     << "Detect=" << detect_time << "ms, "
                     << "Mosaic=" << mosaic_time << "ms, "
                     << "Encode=" << encode_time << "ms, "
                     << "Total=" << total_time << "ms");
            
            frames_processed = 0;
            last_stats_time = now;
        }
        
        // 动态休眠控制帧率
        int64_t target_frame_time = 1000 / 25; // 25fps
        int64_t actual_frame_time = static_cast<int64_t>(total_time);
        if (actual_frame_time < target_frame_time) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(target_frame_time - actual_frame_time));
        }
    }
    
    LOG_INFO("Processing loop ended");
}


void DecoderManager::onDecodeEos() {
    LOG_INFO("Decode EOS received");
}

void DecoderManager::onDecodeFrame(AVFrame* frame, const int64_t frmIndex) {
    LOG_DEBUG("Decode frame received, frame index: " << frmIndex);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - onDecodeFrameTime_).count();
    onDecodeFrameTime_ = std::chrono::high_resolution_clock::now();
    LOG_DEBUG("onDecodeFrame duration total: " << duration << " ms");

    if (!frame)
    {
        LOG_ERROR("frame is null");
        return;
    }  
    
    if (frame->format != AV_PIX_FMT_YUV420P) 
    {
        LOG_ERROR("frame format is not YUV420P");        
        return;
    }
    
    // 存储YUV数据
    auto start1 = std::chrono::high_resolution_clock::now();

    emai::YUVFrame yuv_frame(frame);

    auto end1 = std::chrono::high_resolution_clock::now();
    LOG_DEBUG("onDecodeFrame duration 1: " << std::chrono::duration_cast<std::chrono::milliseconds>(end1 - start1).count() << " ms");

    if (!yuv_frame.empty()) {
        auto start = std::chrono::high_resolution_clock::now();

        decodedFrameBuffer_->push(std::move(yuv_frame));

        auto end = std::chrono::high_resolution_clock::now();
        LOG_DEBUG("onDecodeFrame duration 2: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " ms" <<
                   ", decodedFrameBuffer_ size: " << static_cast<int>(decodedFrameBuffer_->size()));
    }
    


}
    



int DecoderManager::updateMosaicSettingsByDetect(const emai::YUVFrame& frame, double& detect_time_ms)
{
    int iRet = -1;
    detect_time_ms = 0;

    /* 抽帧检测 */
    detectFrameCnt_++;
    LOG_DEBUG("detectFrameCnt_: " << detectFrameCnt_);
    if (!detectFrameCnt_ || (detectFrameCnt_ % detectFrameSkipNum_ == 0))
    {
        LOG_DEBUG("detectFrameCnt_: " << detectFrameCnt_ << ", detectFrameSkipNum_: " << detectFrameSkipNum_);        

        if (!detector_)
        {
            LOG_ERROR("detector is null");
            return iRet;
        }

        emai::detect_result_group_t detectResultGroup;
        int org_height = 0;
        int org_width = 0;

        auto detect_start = std::chrono::high_resolution_clock::now();
        detector_->detect(frame, detectResultGroup, org_width, org_height);
        auto detect_end = std::chrono::high_resolution_clock::now();
        detect_time_ms = std::chrono::duration_cast<std::chrono::microseconds>(detect_end - detect_start).count() / 1000.0;

        if (detectResultGroup.count <= 0)
        {
            LOG_INFO("No detection results");

            auto settings = mosaicProcessor_->get_settings();
            settings.x = 0;
            settings.y = 0;
            settings.width = 0;
            settings.height = 0;
            mosaicProcessor_->update_mosaic_settings(settings);
            LOG_INFO("No detection results, mosaic settings reset");
            return iRet;
        }

        emai::BOX_RECT bestBox;
        float          bestProp = 0.0f;

        /* 区置信度最高的检测结果 */
        for (int i = 0; i < detectResultGroup.count; i++)
        {
            emai::detect_result_t *det_result = &(detectResultGroup.results[i]);

            LOG_INFO("i:" << i << ",det_result->name: " << det_result->name << " @ (" << det_result->box.left << 
                    " " << det_result->box.top << " " << det_result->box.right << " " << det_result->box.bottom << 
                    ") " << det_result->prop);  
                    
            if ((enLabel_ == det_result->name) && (det_result->prop > bestProp))
            {
                bestProp = det_result->prop;
                bestBox = det_result->box;
            }
        }

        /* 缩放适配到当前画面分辨率 */
        float scale_w = (float)dst_width_ / org_width;
        float scale_h = (float)dst_height_ / org_height;
        bestBox.left = (int)(bestBox.left * scale_w);
        bestBox.top = (int)(bestBox.top * scale_h);
        bestBox.right = (int)(bestBox.right * scale_w);
        bestBox.bottom = (int)(bestBox.bottom * scale_h);

        LOG_INFO("bestProp: " << bestProp << ", bestBox: (" << bestBox.left << " " << bestBox.top << " " << bestBox.right << " " << bestBox.bottom << ")");

        if (!mosaicProcessor_)
        {
            LOG_ERROR("mosaicProcessor is null");
            return iRet;
        }

        auto settings = mosaicProcessor_->get_settings();

        LOG_INFO("current mosaic settings: (" << settings.x << "," << settings.y << ") " << settings.width << "x" << settings.height <<
                ", enabled: " << settings.enabled << ")" << ",bestBox: (" << bestBox.left << " " << bestBox.top << ") width: " << 
                (bestBox.right - bestBox.left) << ", height: " << (bestBox.bottom - bestBox.top));

        if ((settings.x != bestBox.left) || (settings.y != bestBox.top) ||
            settings.width != (bestBox.right - bestBox.left) ||
            settings.height != (bestBox.bottom - bestBox.top))
        {
            settings.x = bestBox.left;
            settings.y = bestBox.top;
            settings.width = bestBox.right - bestBox.left;
            settings.height = bestBox.bottom - bestBox.top;

            LOG_INFO("Updating mosaic settings: (" << settings.x << "," << settings.y << ") " 
                << settings.width << "x" << settings.height);
            mosaicProcessor_->update_mosaic_settings(settings);
        }
    }

    return iRet;
}


MosaicProcessor::MosaicSettings DecoderManager::getMosaicSettings()
{
    return mosaicProcessor_->get_settings();
}

std::string DecoderManager::get_perf_stats_json()
{
    std::lock_guard<std::mutex> lock(perfMutex_);
    
    std::stringstream ss;
    ss << "{";
    ss << "\"decode_ms\":" << std::fixed << std::setprecision(2) << perfStats_.decode_ms << ",";
    ss << "\"detect_ms\":" << std::fixed << std::setprecision(2) << perfStats_.detect_ms << ",";
    ss << "\"mosaic_ms\":" << std::fixed << std::setprecision(2) << perfStats_.mosaic_ms << ",";
    ss << "\"encode_ms\":" << std::fixed << std::setprecision(2) << perfStats_.encode_ms << ",";
    ss << "\"total_ms\":" << std::fixed << std::setprecision(2) << perfStats_.total_ms << ",";
    ss << "\"fps\":" << perfStats_.fps;
    ss << "}";
    return ss.str();
}

void DecoderManager::stream_broadcast_loop() {
    LOG_INFO("Stream broadcast loop started");
    
    const size_t MAX_CLIENT_QUEUE_SIZE = 30;  // 每个客户端最多缓存30帧
    
    while (broadcasting_) {
        std::vector<uint8_t> h264_data;
        
        // 等待编码数据
        {
            std::unique_lock<std::mutex> lock(h264_queue_mutex_);
            h264_queue_cv_.wait_for(lock, std::chrono::milliseconds(100), 
                [this]() { return !h264_frame_queue_.empty() || !broadcasting_; });
            
            if (!broadcasting_) break;
            
            if (!h264_frame_queue_.empty()) {
                h264_data = std::move(h264_frame_queue_.front());
                h264_frame_queue_.pop();
            }
        }
        
        if (h264_data.empty()) {
            LOG_WARN("H.264 data is empty");
            continue;
        }
        
        // 将H.264数据分发给所有客户端
        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            int64_t now = av_gettime() / 1000;
            
            for (auto& [client_id, client] : stream_clients_) {
                // 限制队列大小，避免内存无限增长
                if (client.data_queue.size() >= MAX_CLIENT_QUEUE_SIZE) {
                    // 移除最旧的帧
                    client.data_queue.pop();
                    LOG_WARN("Client " << client_id << " queue full, dropping oldest frame");
                }
                
                // 推送数据到客户端队列
                client.data_queue.push(h264_data);
                client.sequence++;
            }
            
            perfStats_.stream_clients = stream_clients_.size();
            
            LOG_DEBUG("Broadcasted H.264 data to " << stream_clients_.size() 
                     << " clients, data size: " << h264_data.size());
        }
    }
    
    LOG_INFO("Stream broadcast loop ended");
}

bool DecoderManager::init_h264_encoder(int width, int height) {

    if (h264_encoder_) {
        LOG_INFO("H.264 encoder already initialized");
        return true;
    }
    
    h264_encoder_ = std::make_unique<emai::MppH264Encoder>();
    if (!h264_encoder_->Init(width, height, dst_width_, dst_height_, 25, 2000000)) {
        LOG_ERROR("Failed to initialize H.264 hardware encoder");
        h264_encoder_.reset();
        return false;
    }
   
    
    LOG_INFO("H.264 hardware encoder initialized: " << width << "x" << height);
    return true;
}

int DecoderManager::register_flv_client() {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    int client_id = next_client_id_++;
    
    StreamClient client;
    client.last_active_time = av_gettime() / 1000;
    client.sequence = 0;
    // data_queue 会自动初始化为空队列
    
    stream_clients_[client_id] = std::move(client);
    
    LOG_INFO("New FLV client registered: " << client_id 
             << ", total clients: " << stream_clients_.size());
    return client_id;
}

void DecoderManager::unregister_flv_client(int client_id) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    if (stream_clients_.erase(client_id)) {
        LOG_INFO("FLV client unregistered: " << client_id
                 << ", remaining clients: " << stream_clients_.size());
    }
}

bool DecoderManager::get_sps_pps_data(std::vector<uint8_t>& sps, std::vector<uint8_t>& pps) {
    if (!h264_encoder_) {
        LOG_ERROR("H.264 encoder not initialized");
        return false;
    }
    
    // 从编码器获取SPS和PPS
    sps = h264_encoder_->GetSpsData();
    pps = h264_encoder_->GetPpsData();
    
    if (sps.empty() || pps.empty()) {
        LOG_WARN("SPS or PPS is empty from encoder (SPS: " << sps.size() << ", PPS: " << pps.size() << ")");
        return false;
    }
    
    LOG_INFO("Got SPS/PPS from encoder: SPS=" << sps.size() << " bytes, PPS=" << pps.size() << " bytes");
    return true;
}

bool DecoderManager::get_flv_stream_data(std::vector<uint8_t>& data, int client_id) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    // 查找客户端
    auto it = stream_clients_.find(client_id);
    if (it == stream_clients_.end()) {
        LOG_WARN("Client " << client_id << " not found");
        return false;
    }
    
    // 从客户端队列中获取数据
    if (!it->second.data_queue.empty()) {
        data = std::move(it->second.data_queue.front());
        it->second.data_queue.pop();
        it->second.last_active_time = av_gettime() / 1000;
        
        LOG_DEBUG("Client " << client_id << " retrieved data, size: " << data.size() 
                 << ", remaining queue: " << it->second.data_queue.size());
        return true;
    }
    
    return false;
}


bool DecoderManager::get_encoder_config(int& width, int& height, int& fps) 
{
    width = dst_width_;
    height = dst_height_;
    fps = 25;
    return true;
}





