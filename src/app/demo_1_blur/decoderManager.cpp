
#include "decoderManager.h"


DecoderManager::DecoderManager() : decodedFrameBuffer_(std::make_unique<emai::YUVFrameBuffer>()),
                                   jpegBuffer_(std::make_unique<std::queue<std::vector<uint8_t>>>()), 
                                   blurProcessor_(std::make_unique<BlurProcessor>()) 
                                {
    lastStatTime_ = av_gettime() / 1000;
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
    
bool DecoderManager::update_blur_settings(int x, int y, int width, int height, int blur_radius, int border_size,
                                          bool enabled, const std::string& shape) {
    std::lock_guard<std::mutex> lock(managerMutex_);
    if (!blurProcessor_) return false;
    
    if (width <= 0 || height <= 0 || width > 800 || height > 600) {
        return false;
    }
    
    if (x < 0 || y < 0 || x + width > 800 || y + height > 600) {
        return false;
    }
    
    blurProcessor_->update_blur_settings(x, y, width, height,
                                        blur_radius, border_size, enabled, shape);
    return true;
}
    
std::string DecoderManager::get_blur_settings_json() 
{
    std::lock_guard<std::mutex> lock(managerMutex_);
    if (!blurProcessor_) return "{}";
    
    auto settings = blurProcessor_->get_settings();
    std::stringstream ss;
    ss << "{";
    ss << "\"x\":" << settings.x << ",";
    ss << "\"y\":" << settings.y << ",";
    ss << "\"width\":" << settings.width << ",";
    ss << "\"height\":" << settings.height << ",";
    ss << "\"blur_radius\":" << settings.blur_radius << ",";
    ss << "\"border_size\":" << settings.border_size << ",";
    ss << "\"enabled\":" << (settings.enabled ? "true" : "false") << ",";
    ss << "\"shape\":\"" << settings.shape << "\"";
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
        
        {
            std::lock_guard<std::mutex> lock(managerMutex_);  
            
            // 尝试获取帧，设置超时
            has_frame = decodedFrameBuffer_->get_latest(frame, 10);
        }
        
        std::vector<uint8_t> processed_frame;
        
        if (has_frame) {
            // 处理帧：应用模糊
            processed_frame = blurProcessor_->process_and_encode(frame);
            
            if (!processed_frame.empty()) {
                last_successful_frame = processed_frame;
                processedFrameCnt_++;
                
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
        if (now - lastStatTime_ >= 5000) { // 每5秒输出一次统计
            LOG_INFO("处理帧率: " << (processedFrameCnt_ * 1000 / (now - lastStatTime_)) << " fps");
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
    





