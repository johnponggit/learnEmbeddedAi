
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
            parser_->ParseLoop(0);
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

void DecoderManager::onDecodeEos() {
    LOG_INFO("Decode EOS received");
}

void DecoderManager::onDecodeFrame(AVFrame* frame, const int64_t frmIndex) {
    LOG_DEBUG("Decode frame received, frame index: " << frmIndex);

    if (!frame)
    {
        LOG_ERROR("frame is null");
        return;
    }

    // 转换到YUV420P格式（如果需要）
    AVFrame* converted_frame = const_cast<AVFrame*>(frame); 
    
    if (frame->format != AV_PIX_FMT_YUV420P) 
    {
        LOG_WARN("frame format is not YUV420P, converting...");
        
        if (!decodedYuvSwsCtx_) {
            decodedYuvSwsCtx_ = sws_getContext(
                frame->width, frame->height,
                (AVPixelFormat)frame->format,
                frame->width, frame->height,
                AV_PIX_FMT_YUV420P,
                SWS_BILINEAR, nullptr, nullptr, nullptr
            );
        }
        
        if (decodedYuvSwsCtx_) {
            AVFrame* yuv_frame = av_frame_alloc();
            yuv_frame->width = frame->width;
            yuv_frame->height = frame->height;
            yuv_frame->format = AV_PIX_FMT_YUV420P;
            yuv_frame->pts = frame->pts;
            av_frame_get_buffer(yuv_frame, 0);
            
            sws_scale(decodedYuvSwsCtx_, frame->data, frame->linesize,
                    0, frame->height,
                    yuv_frame->data, yuv_frame->linesize);
            
            converted_frame = yuv_frame;
        }
    }
    
    // 存储YUV数据
    emai::YUVFrame yuv_frame(converted_frame);
    if (!yuv_frame.empty()) {
        decodedFrameBuffer_->push(yuv_frame);
    }
    
    // 清理临时帧
    if (converted_frame != frame) {
        av_frame_free(&converted_frame);
    }
    
    av_frame_unref(frame);

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






