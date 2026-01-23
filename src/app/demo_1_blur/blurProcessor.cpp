
#include "blurProcessor.h"
#include "jpegEncoderFfmpegCpu.h"

BlurProcessor::BlurProcessor() 
{
    // 初始化编码器（处理后的画面）
    if (!jpegEncoder_) 
    {
        jpegEncoder_ = std::make_shared<emai::JpegEncoderFfmpegCpu>();

        if (!jpegEncoder_ || !jpegEncoder_->Init(800, 600))
        {
            jpegEncoder_.reset();
            LOG_ERROR("in BlurProcessor jpegEncoder_ init failed");
        }
    } 

    // 分配临时帧
    output_frame = av_frame_alloc();
    
    // 预计算高斯模糊内核
    generate_gaussian_kernel(5);
}

BlurProcessor::~BlurProcessor() 
{
    if (sws_ctx) sws_freeContext(sws_ctx);
    if (output_frame) av_frame_free(&output_frame);
}
    
std::vector<uint8_t> BlurProcessor::process_and_encode(emai::YUVFrame& input_yuv) 
{
    std::lock_guard<std::mutex> lock(processor_mutex);
    std::vector<uint8_t> result;
    
    if (input_yuv.empty()) {
        return result;
    }
    
    try {
        // 准备输出帧
        if (!prepare_output_frame(input_yuv)) {
            return result;
        }
        
        // 如果启用模糊，对选定区域应用模糊
        if (blur_settings.enabled) {
            apply_blur_to_frame();
        }
        
        // 编码处理后的帧为JPEG
        emai::YUVFrame output_yuv(output_frame);
        if (!output_yuv.empty()) {
            result = jpegEncoder_->encode(output_yuv);
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error in process_and_encode: " << e.what() << std::endl;
    }
    
    return result;
}
    
void BlurProcessor::update_blur_settings(int x, int y, int width, int height,
                                         int blur_radius, int border_size,
                                         bool enabled, const std::string& shape) 
{
    std::lock_guard<std::mutex> lock(processor_mutex);
    blur_settings.x = x;
    blur_settings.y = y;
    blur_settings.width = width;
    blur_settings.height = height;
    blur_settings.blur_radius = blur_radius;
    blur_settings.border_size = border_size;
    blur_settings.enabled = enabled;
    blur_settings.shape = shape;
    
    // 重新生成高斯模糊内核
    generate_gaussian_kernel(blur_radius);
    
    std::cout << "Blur settings updated:" << std::endl;
    std::cout << "  Shape: " << shape << std::endl;
    std::cout << "  Position: (" << x << "," << y << ") " 
                << width << "x" << height << std::endl;
    std::cout << "  Blur radius: " << blur_radius << std::endl;
    std::cout << "  Enabled: " << (enabled ? "true" : "false") << std::endl;
}
    
BlurProcessor::BlurSettings BlurProcessor::get_settings() 
{
    std::lock_guard<std::mutex> lock(processor_mutex);
    return blur_settings;
}
    
bool BlurProcessor::prepare_output_frame(emai::YUVFrame& input_yuv) 
{
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
    
void BlurProcessor::generate_gaussian_kernel(int radius) 
{
    int size = 2 * radius + 1;
    gaussian_kernel.resize(size * size);
    
    float sigma = radius / 2.0f;
    float sum = 0.0f;
    
    for (int y = -radius; y <= radius; y++) {
        for (int x = -radius; x <= radius; x++) {
            float value = exp(-(x * x + y * y) / (2 * sigma * sigma));
            gaussian_kernel[(y + radius) * size + (x + radius)] = value;
            sum += value;
        }
    }
    
    // 归一化
    for (int i = 0; i < size * size; i++) {
        gaussian_kernel[i] /= sum;
    }
}
    
void BlurProcessor::apply_blur_to_frame() 
{
    if (!output_frame) return;
    
    // 根据形状选择不同的模糊方法
    if (blur_settings.shape == "circle") {
        apply_circular_blur_to_frame();
    } else {
        apply_rectangular_blur_to_frame();
    }
    
    // 添加边框（可选）
    if (blur_settings.border_size > 0) {
        add_border_to_region();
    }
}
    
void BlurProcessor::apply_circular_blur_to_frame() 
{
    // 计算圆形参数
    int center_x = blur_settings.x + blur_settings.width / 2;
    int center_y = blur_settings.y + blur_settings.height / 2;
    int radius = std::min(blur_settings.width, blur_settings.height) / 2;
    
    // 确保参数在有效范围内
    center_x = std::max(radius, std::min(center_x, output_frame->width - radius));
    center_y = std::max(radius, std::min(center_y, output_frame->height - radius));
    radius = std::min(radius, std::min(center_x, center_y));
    radius = std::min(radius, std::min(output_frame->width - center_x, output_frame->height - center_y));
    
    if (radius <= 0) return;
    
    // 复制原始数据用于模糊处理
    std::vector<uint8_t> y_copy(output_frame->linesize[0] * output_frame->height);
    memcpy(y_copy.data(), output_frame->data[0], y_copy.size());
    
    std::vector<uint8_t> u_copy(output_frame->linesize[1] * (output_frame->height / 2));
    memcpy(u_copy.data(), output_frame->data[1], u_copy.size());
    
    std::vector<uint8_t> v_copy(output_frame->linesize[2] * (output_frame->height / 2));
    memcpy(v_copy.data(), output_frame->data[2], v_copy.size());
    
    // 对Y平面应用高斯模糊（圆形区域）
    apply_circular_gaussian_blur(y_copy.data(), output_frame->data[0], 
                                output_frame->linesize[0], output_frame->height,
                                center_x, center_y, radius, blur_settings.blur_radius);
    
    // 对U平面应用高斯模糊（注意YUV420中UV平面尺寸减半）
    int uv_center_x = center_x / 2;
    int uv_center_y = center_y / 2;
    int uv_radius = radius / 2;
    
    apply_circular_gaussian_blur(u_copy.data(), output_frame->data[1],
                                output_frame->linesize[1], output_frame->height / 2,
                                uv_center_x, uv_center_y, uv_radius, blur_settings.blur_radius / 2);
    
    apply_circular_gaussian_blur(v_copy.data(), output_frame->data[2],
                                output_frame->linesize[2], output_frame->height / 2,
                                uv_center_x, uv_center_y, uv_radius, blur_settings.blur_radius / 2);
}
    
void BlurProcessor::apply_rectangular_blur_to_frame() 
{
    // 确保模糊区域在有效范围内
    int x = std::max(0, std::min(blur_settings.x, output_frame->width - 1));
    int y = std::max(0, std::min(blur_settings.y, output_frame->height - 1));
    int width = std::min(blur_settings.width, output_frame->width - x);
    int height = std::min(blur_settings.height, output_frame->height - y);
    int radius = std::max(1, blur_settings.blur_radius);
    
    if (width <= 0 || height <= 0) return;
    
    // 复制原始数据用于模糊处理
    std::vector<uint8_t> y_copy(output_frame->linesize[0] * output_frame->height);
    memcpy(y_copy.data(), output_frame->data[0], y_copy.size());
    
    std::vector<uint8_t> u_copy(output_frame->linesize[1] * (output_frame->height / 2));
    memcpy(u_copy.data(), output_frame->data[1], u_copy.size());
    
    std::vector<uint8_t> v_copy(output_frame->linesize[2] * (output_frame->height / 2));
    memcpy(v_copy.data(), output_frame->data[2], v_copy.size());
    
    // 对Y平面应用高斯模糊
    apply_rectangular_gaussian_blur(y_copy.data(), output_frame->data[0], 
                                    output_frame->linesize[0], output_frame->height,
                                    x, y, width, height, radius);
    
    // 对U平面应用高斯模糊（注意YUV420中UV平面尺寸减半）
    int uv_x = x / 2;
    int uv_y = y / 2;
    int uv_width = width / 2;
    int uv_height = height / 2;
    
    apply_rectangular_gaussian_blur(u_copy.data(), output_frame->data[1],
                                    output_frame->linesize[1], output_frame->height / 2,
                                    uv_x, uv_y, uv_width, uv_height, radius / 2);
    
    apply_rectangular_gaussian_blur(v_copy.data(), output_frame->data[2],
                                    output_frame->linesize[2], output_frame->height / 2,
                                    uv_x, uv_y, uv_width, uv_height, radius / 2);
}
    
void BlurProcessor::apply_circular_gaussian_blur(uint8_t* src, uint8_t* dst, int stride, int height,
                                                 int center_x, int center_y, int radius, int blur_radius) 
{
    int kernel_size = 2 * blur_radius + 1;
    
    // 只重新生成内核如果模糊半径改变了
    static int last_blur_radius = 0;
    if (blur_radius != last_blur_radius) {
        generate_gaussian_kernel(blur_radius);
        last_blur_radius = blur_radius;
    }
    
    // 计算圆形区域的边界框
    int start_y = std::max(0, center_y - radius);
    int end_y = std::min(height, center_y + radius);
    int start_x = std::max(0, center_x - radius);
    int end_x = std::min(stride, center_x + radius);
    
    for (int y = start_y; y < end_y; y++) {
        for (int x = start_x; x < end_x; x++) {
            // 检查像素是否在圆形内
            int dx = x - center_x;
            int dy = y - center_y;
            float distance = sqrt(dx * dx + dy * dy);
            
            if (distance > radius) {
                continue; // 跳过圆形外的像素
            }
            
            float sum = 0.0f;
            float weight_sum = 0.0f;
            
            // 应用卷积核
            for (int ky = -blur_radius; ky <= blur_radius; ky++) {
                int ny = y + ky;
                if (ny < 0 || ny >= height) continue;
                
                for (int kx = -blur_radius; kx <= blur_radius; kx++) {
                    int nx = x + kx;
                    if (nx < 0 || nx >= stride) continue;
                    
                    // 检查卷积核中的像素是否也在圆形内
                    int ndx = nx - center_x;
                    int ndy = ny - center_y;
                    float ndistance = sqrt(ndx * ndx + ndy * ndy);
                    
                    if (ndistance > radius) {
                        continue; // 跳过圆形外的像素
                    }
                    
                    float weight = gaussian_kernel[(ky + blur_radius) * kernel_size + (kx + blur_radius)];
                    sum += src[ny * stride + nx] * weight;
                    weight_sum += weight;
                }
            }
            
            if (weight_sum > 0) {
                // 确保值在有效范围内
                uint8_t value = static_cast<uint8_t>(std::max(0, std::min(255, static_cast<int>(sum / weight_sum))));
                dst[y * stride + x] = value;
            }
        }
    }
}
    
void BlurProcessor::apply_rectangular_gaussian_blur(uint8_t* src, uint8_t* dst, int stride, int height,
                                                    int region_x, int region_y, int region_width, int region_height,
                                                    int blur_radius) 
{
    int kernel_size = 2 * blur_radius + 1;
    
    // 只重新生成内核如果模糊半径改变了
    static int last_blur_radius = 0;
    if (blur_radius != last_blur_radius) {
        generate_gaussian_kernel(blur_radius);
        last_blur_radius = blur_radius;
    }
    
    for (int y = region_y; y < region_y + region_height; y++) {
        for (int x = region_x; x < region_x + region_width; x++) {
            if (y >= height || x >= stride) continue;
            
            float sum = 0.0f;
            
            // 应用卷积核
            for (int ky = -blur_radius; ky <= blur_radius; ky++) {
                int ny = y + ky;
                if (ny < 0 || ny >= height) continue;
                
                for (int kx = -blur_radius; kx <= blur_radius; kx++) {
                    int nx = x + kx;
                    if (nx < 0 || nx >= stride) continue;
                    
                    float weight = gaussian_kernel[(ky + blur_radius) * kernel_size + (kx + blur_radius)];
                    sum += src[ny * stride + nx] * weight;
                }
            }
            
            // 确保值在有效范围内
            uint8_t value = static_cast<uint8_t>(std::max(0, std::min(255, static_cast<int>(sum))));
            dst[y * stride + x] = value;
        }
    }
}
    
void BlurProcessor::add_border_to_region() 
{
    if (!output_frame) return;
    
    // 边框颜色（蓝色，YUV值）
    const uint8_t border_y = 41;    // 蓝色对应的Y值
    const uint8_t border_u = 240;   // 蓝色对应的U值
    const uint8_t border_v = 110;   // 蓝色对应的V值
    
    int border_size = std::min(blur_settings.border_size, 5);
    
    if (blur_settings.shape == "circle") {
        // 圆形边框
        int center_x = blur_settings.x + blur_settings.width / 2;
        int center_y = blur_settings.y + blur_settings.height / 2;
        int radius = std::min(blur_settings.width, blur_settings.height) / 2;
        
        // 绘制圆形边框
        for (int angle = 0; angle < 360; angle++) {
            float rad = angle * M_PI / 180.0f;
            int border_x = center_x + static_cast<int>((radius + border_size) * cos(rad));
            int border_y_pos = center_y + static_cast<int>((radius + border_size) * sin(rad));
            
            if (border_x >= 0 && border_x < output_frame->width && 
                border_y_pos >= 0 && border_y_pos < output_frame->height) {
                output_frame->data[0][border_y_pos * output_frame->linesize[0] + border_x] = border_y;
            }
        }
    } else {
        // 矩形边框
        int x = blur_settings.x;
        int y = blur_settings.y;
        int width = blur_settings.width;
        int height = blur_settings.height;
        
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
}

 