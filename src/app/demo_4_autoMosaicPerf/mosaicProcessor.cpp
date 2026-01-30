
#include "mosaicProcessor.h"
#include "jpegEncoderFfmpegCpu.h"



MosaicProcessor::MosaicProcessor() {
    // 初始化编码器（处理后的画面）
    if (!jpegEncoder_) 
    {
        jpegEncoder_ = std::make_shared<emai::JpegEncoderFfmpegCpu>();

        if (!jpegEncoder_ || !jpegEncoder_->Init(dst_width, dst_height))
        {
            jpegEncoder_.reset();
            LOG_ERROR("in BlurProcessor jpegEncoder_ init failed");
        }
    } 
    
    // 分配临时帧
    output_frame = av_frame_alloc();
}

MosaicProcessor::~MosaicProcessor() {
    if (sws_ctx) sws_freeContext(sws_ctx);
    if (output_frame) av_frame_free(&output_frame);
}
    
std::vector<uint8_t> MosaicProcessor::process_and_encode(emai::YUVFrame& input_yuv, double& mosaic_time_ms, double& encode_time_ms) {
    std::lock_guard<std::mutex> lock(processor_mutex);
    std::vector<uint8_t> result;
    
    mosaic_time_ms = 0;
    encode_time_ms = 0;

    if (input_yuv.empty()) {
        return result;
    }
    
    try {
        // 准备输出帧
        if (!prepare_output_frame(input_yuv)) {
            return result;
        }
        
        // 如果启用马赛克，对选定区域应用马赛克
        if (mosaic_settings.enabled) {
            auto mosaic_start = std::chrono::high_resolution_clock::now();
            apply_mosaic_to_frame();
            auto mosaic_end = std::chrono::high_resolution_clock::now();
            mosaic_time_ms = std::chrono::duration_cast<std::chrono::microseconds>(mosaic_end - mosaic_start).count() / 1000.0;
        }
        
        // 编码处理后的帧为JPEG
        // 将 output_frame 转换为 YUVFrame 对象
        emai::YUVFrame output_yuv(output_frame);
        if (!output_yuv.empty()) {
            auto start = std::chrono::high_resolution_clock::now();
            result = jpegEncoder_->encode(output_yuv);
            auto end = std::chrono::high_resolution_clock::now();
            encode_time_ms = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 1000.0;
            LOG_DEBUG("JPEG encoding took " << encode_time_ms << " milliseconds");
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error in process_and_encode: " << e.what() << std::endl;
    }
    
    return result;
}
    
AVFrame* MosaicProcessor::process(emai::YUVFrame& input_yuv, double& mosaic_time_ms) 
{
    std::lock_guard<std::mutex> lock(processor_mutex);
    AVFrame* retYuv = nullptr;
    
    mosaic_time_ms = 0;

    if (input_yuv.empty()) {
        return retYuv;
    }
    
    try {
        // 准备输出帧
        if (!prepare_output_frame(input_yuv)) {
            return retYuv;
        }
        
        // 如果启用马赛克，对选定区域应用马赛克
        if (mosaic_settings.enabled) {
            auto mosaic_start = std::chrono::high_resolution_clock::now();
            apply_mosaic_to_frame();
            auto mosaic_end = std::chrono::high_resolution_clock::now();
            mosaic_time_ms = std::chrono::duration_cast<std::chrono::microseconds>(mosaic_end - mosaic_start).count() / 1000.0;
        }
        
        return output_frame;
        
        
    } catch (const std::exception& e) {
        std::cerr << "Error in process_and_encode: " << e.what() << std::endl;
    }
    
    return retYuv;
}

void MosaicProcessor::update_mosaic_settings(int x, int y, int width, int height,
                                                int block_size, int border_size,
                                                bool enabled) {
    std::lock_guard<std::mutex> lock(processor_mutex);
    mosaic_settings.x = x;
    mosaic_settings.y = y;
    mosaic_settings.width = width;
    mosaic_settings.height = height;
    mosaic_settings.block_size = block_size;
    mosaic_settings.border_size = border_size;
    mosaic_settings.enabled = enabled; 

    LOG_INFO("Mosaic settings updated:" << "  Region: (" << x << "," << y << ") " << width << "x" << height << 
             "  Block size: " << block_size << "  Enabled: " << (enabled ? "true" : "false"));
}

void MosaicProcessor::update_mosaic_settings(MosaicSettings settings) 
{
    std::lock_guard<std::mutex> lock(processor_mutex);
    mosaic_settings = settings;   

}

// 获取当前设置
MosaicProcessor::MosaicSettings MosaicProcessor::get_settings() {
    std::lock_guard<std::mutex> lock(processor_mutex);
    return mosaic_settings;
}
    

// 准备输出帧
bool MosaicProcessor::prepare_output_frame(emai::YUVFrame& input_yuv) {
    if (input_yuv.empty()) {
        return false;
    }
    
    // 确保输出帧已分配
    if (output_frame->width != 800 || output_frame->height != 600) {
        av_frame_unref(output_frame);
        output_frame->width = 800;
        output_frame->height = 600;
        output_frame->format = AV_PIX_FMT_YUV420P;
        if (av_frame_get_buffer(output_frame, 0) < 0) {
            std::cerr << "Failed to allocate output frame buffer" << std::endl;
            return false;
        }
    }
    
    // 创建或更新缩放上下文
    if (!sws_ctx) {
        sws_ctx = sws_getContext(
            input_yuv.width, input_yuv.height,
            AV_PIX_FMT_YUV420P,
            output_frame->width, output_frame->height,
            AV_PIX_FMT_YUV420P,
            SWS_BILINEAR, nullptr, nullptr, nullptr
        );
        if (!sws_ctx) {
            std::cerr << "Failed to create sws context" << std::endl;
            return false;
        }
    }
    
    // 转换为AVFrame
    AVFrame* input_frame = input_yuv.to_avframe();
    if (!input_frame) {
        return false;
    }
    
    // 执行缩放
    sws_scale(sws_ctx, input_frame->data, input_frame->linesize,
                0, input_frame->height,
                output_frame->data, output_frame->linesize);
    
    av_frame_free(&input_frame);
    return true;
}
    
// 对帧的选定区域应用马赛克效果
void MosaicProcessor::apply_mosaic_to_frame() {
    if (!output_frame) return;
    
    // 确保马赛克区域在有效范围内
    int x = std::max(0, std::min(mosaic_settings.x, output_frame->width - 1));
    int y = std::max(0, std::min(mosaic_settings.y, output_frame->height - 1));
    int width = std::min(mosaic_settings.width, output_frame->width - x);
    int height = std::min(mosaic_settings.height, output_frame->height - y);
    int block_size = std::max(2, mosaic_settings.block_size);
    
    if (width <= 0 || height <= 0) return;
    
    // 应用马赛克效果到Y平面
    for (int block_y = y; block_y < y + height; block_y += block_size) {
        for (int block_x = x; block_x < x + width; block_x += block_size) {
            // 计算当前块的边界
            int block_end_x = std::min(block_x + block_size, x + width);
            int block_end_y = std::min(block_y + block_size, y + height);
            int block_width = block_end_x - block_x;
            int block_height = block_end_y - block_y;
            
            // 计算当前块的平均Y值
            int sum_y = 0;
            for (int by = block_y; by < block_end_y; by++) {
                uint8_t* y_line = output_frame->data[0] + by * output_frame->linesize[0];
                for (int bx = block_x; bx < block_end_x; bx++) {
                    sum_y += y_line[bx];
                }
            }
            uint8_t avg_y = sum_y / (block_width * block_height);
            
            // 填充当前块的Y值
            for (int by = block_y; by < block_end_y; by++) {
                uint8_t* y_line = output_frame->data[0] + by * output_frame->linesize[0];
                for (int bx = block_x; bx < block_end_x; bx++) {
                    y_line[bx] = avg_y;
                }
            }
            
            // 处理UV平面（注意YUV420中UV平面尺寸减半）
            int uv_x = block_x / 2;
            int uv_y = block_y / 2;
            int uv_block_size = block_size / 2;
            int uv_block_end_x = std::min(uv_x + uv_block_size, (x + width) / 2);
            int uv_block_end_y = std::min(uv_y + uv_block_size, (y + height) / 2);
            int uv_block_width = uv_block_end_x - uv_x;
            int uv_block_height = uv_block_end_y - uv_y;
            
            if (uv_block_width > 0 && uv_block_height > 0) {
                // 计算当前块的平均U值
                int sum_u = 0;
                for (int by = uv_y; by < uv_block_end_y; by++) {
                    uint8_t* u_line = output_frame->data[1] + by * output_frame->linesize[1];
                    for (int bx = uv_x; bx < uv_block_end_x; bx++) {
                        sum_u += u_line[bx];
                    }
                }
                uint8_t avg_u = sum_u / (uv_block_width * uv_block_height);
                
                // 计算当前块的平均V值
                int sum_v = 0;
                for (int by = uv_y; by < uv_block_end_y; by++) {
                    uint8_t* v_line = output_frame->data[2] + by * output_frame->linesize[2];
                    for (int bx = uv_x; bx < uv_block_end_x; bx++) {
                        sum_v += v_line[bx];
                    }
                }
                uint8_t avg_v = sum_v / (uv_block_width * uv_block_height);
                
                // 填充当前块的UV值
                for (int by = uv_y; by < uv_block_end_y; by++) {
                    uint8_t* u_line = output_frame->data[1] + by * output_frame->linesize[1];
                    uint8_t* v_line = output_frame->data[2] + by * output_frame->linesize[2];
                    for (int bx = uv_x; bx < uv_block_end_x; bx++) {
                        u_line[bx] = avg_u;
                        v_line[bx] = avg_v;
                    }
                }
            }
        }
    }
    
    // 添加边框（可选）
    if (mosaic_settings.border_size > 0) {
        add_border_to_region(x, y, width, height);
    }
}
    
// 为区域添加边框
void MosaicProcessor::add_border_to_region(int x, int y, int width, int height) {
    if (!output_frame) return;
    
    // 边框颜色（红色，YUV值）
    const uint8_t border_y = 76;    // 红色对应的Y值
    const uint8_t border_u = 84;    // 红色对应的U值
    const uint8_t border_v = 255;   // 红色对应的V值
    
    int border_size = std::min(mosaic_settings.border_size, 
                                std::min(width, height) / 4);
    
    // 上边框
    for (int by = 0; by < border_size; by++) {
        int actual_y = y + by;
        if (actual_y >= 0 && actual_y < output_frame->height) {
            uint8_t* y_line = output_frame->data[0] + actual_y * output_frame->linesize[0];
            for (int bx = x; bx < x + width; bx++) {
                if (bx >= 0 && bx < output_frame->width) {
                    y_line[bx] = border_y;
                }
            }
        }
    }
    
    // 下边框
    for (int by = 0; by < border_size; by++) {
        int actual_y = y + height - 1 - by;
        if (actual_y >= 0 && actual_y < output_frame->height) {
            uint8_t* y_line = output_frame->data[0] + actual_y * output_frame->linesize[0];
            for (int bx = x; bx < x + width; bx++) {
                if (bx >= 0 && bx < output_frame->width) {
                    y_line[bx] = border_y;
                }
            }
        }
    }
    
    // 左边框
    for (int bx = 0; bx < border_size; bx++) {
        int actual_x = x + bx;
        if (actual_x >= 0 && actual_x < output_frame->width) {
            for (int by = y; by < y + height; by++) {
                if (by >= 0 && by < output_frame->height) {
                    uint8_t* y_line = output_frame->data[0] + by * output_frame->linesize[0];
                    y_line[actual_x] = border_y;
                }
            }
        }
    }
    
    // 右边框
    for (int bx = 0; bx < border_size; bx++) {
        int actual_x = x + width - 1 - bx;
        if (actual_x >= 0 && actual_x < output_frame->width) {
            for (int by = y; by < y + height; by++) {
                if (by >= 0 && by < output_frame->height) {
                    uint8_t* y_line = output_frame->data[0] + by * output_frame->linesize[0];
                    y_line[actual_x] = border_y;
                }
            }
        }
    }
}

void MosaicProcessor::getDstWidthHeight(int& width, int& height)
{
    width = dst_width;
    height = dst_height;
}

