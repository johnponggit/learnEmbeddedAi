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


class MosaicProcessor {
public:
    // 马赛克区域设置
    struct MosaicSettings {
        int x = 0;              // 马赛克区域左上角X坐标
        int y = 0;              // 马赛克区域左上角Y坐标
        int width = 0;          // 马赛克区域宽度
        int height = 0;         // 马赛克区域高度
        int block_size = 16;    // 马赛克块大小
        int border_size = 2;    // 边框大小
        bool enabled = true;    // 是否启用马赛克
    } mosaic_settings;
    
    using Ptr = std::shared_ptr<MosaicProcessor>;
    using uPtr = std::unique_ptr<MosaicProcessor>;
    
    MosaicProcessor();    
    ~MosaicProcessor();
    
    // 处理主画面：应用马赛克并编码为JPEG
    std::vector<uint8_t> process_and_encode(emai::YUVFrame& input_yuv, double& mosaic_time_ms, double& encode_time_ms); 
    
    AVFrame* process(emai::YUVFrame& input_yuv, double& mosaic_time_ms); 

    // 更新马赛克设置
    void update_mosaic_settings(int x, int y, int width, int height,
                               int block_size = 16, int border_size = 2,
                               bool enabled = true);
    void update_mosaic_settings(MosaicSettings settings);

    // 获取当前设置
    MosaicSettings get_settings();
    void getDstWidthHeight(int& width, int& height);
   
private:
    // 准备输出帧
    bool prepare_output_frame(emai::YUVFrame& input_yuv);
    
    // 对帧的选定区域应用马赛克效果
    void apply_mosaic_to_frame();

    // 为区域添加边框
    void add_border_to_region(int x, int y, int width, int height) ;

private:
    std::mutex processor_mutex;  
    
    // 编码器
    emai::IJpegEncoder::Ptr     jpegEncoder_{nullptr};
    
    // 用于图像缩放
    SwsContext* sws_ctx = nullptr;
    
    // 临时帧
    AVFrame* output_frame = nullptr;

    int                    dst_width = 800;
    int                    dst_height = 600;
    
};