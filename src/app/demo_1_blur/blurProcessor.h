#pragma once    


#include "util.h"
#include "httplib.h"
#include "json.hpp"

#include "mediaDataStruct.h"
#include "IJpegEncoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>
}


// 模糊处理器
class BlurProcessor {
   
// 模糊区域设置
struct BlurSettings {
    int x = 100;            // 模糊区域左上角X坐标（对于圆形，是圆心X坐标）
    int y = 100;            // 模糊区域左上角Y坐标（对于圆形，是圆心Y坐标）
    int width = 200;        // 模糊区域宽度（对于圆形，是直径）
    int height = 150;       // 模糊区域高度（对于圆形，是直径）
    int blur_radius = 5;    // 模糊半径
    int border_size = 2;    // 边框大小
    bool enabled = true;    // 是否启用模糊
    std::string shape = "circle"; // 模糊形状: "circle" 或 "rectangle"
} blur_settings;
    
public:
    BlurProcessor();    
    ~BlurProcessor();
    
    // 处理主画面：应用模糊并编码为JPEG
    std::vector<uint8_t> process_and_encode(emai::YUVFrame& input_yuv);
    
    // 更新模糊设置
    void update_blur_settings(int x, int y, int width, int height,
                             int blur_radius = 5, int border_size = 2,
                             bool enabled = true, const std::string& shape = "circle");
    
    // 获取当前设置
    BlurSettings get_settings() ;
    
private:
    // 准备输出帧
    bool prepare_output_frame(emai::YUVFrame& input_yuv);
    
    // 生成高斯模糊内核
    void generate_gaussian_kernel(int radius);
    
    // 对帧的选定区域应用高斯模糊效果
    void apply_blur_to_frame();
    
    // 对圆形区域应用模糊
    void apply_circular_blur_to_frame();
    
    // 对矩形区域应用模糊
    void apply_rectangular_blur_to_frame();
    
    // 应用圆形高斯模糊
    void apply_circular_gaussian_blur(uint8_t* src, uint8_t* dst, int stride, int height,
                                     int center_x, int center_y, int radius, int blur_radius);
    
    // 应用矩形高斯模糊
    void apply_rectangular_gaussian_blur(uint8_t* src, uint8_t* dst, int stride, int height,
                                        int region_x, int region_y, int region_width, int region_height,
                                        int blur_radius) ;
    
    // 为区域添加边框
    void add_border_to_region() ;

private:
    std::mutex processor_mutex;
        
    emai::IJpegEncoder::Ptr     jpegEncoder_{nullptr};

    // 用于图像缩放
    SwsContext* sws_ctx = nullptr;
    
    // 临时帧
    AVFrame* output_frame = nullptr;
    
    // 高斯模糊内核
    std::vector<float> gaussian_kernel;

};


