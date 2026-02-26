#pragma once

#include "mediaDataStruct.h"
#include <mutex>

extern "C" {
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

namespace emai {

// 检测框绘制器
class BBoxDrawer {
public:
    struct BBoxSettings {
        int line_thickness = 4;      // 线条粗细
        uint8_t y_color = 76;        // Y分量颜色 - 红色 (RGB 255,0,0 -> Y≈76)
        uint8_t u_color = 84;        // U分量颜色 - 红色
        uint8_t v_color = 255;       // V分量颜色 - 红色
        bool enabled = true;         // 是否启用绘制
        bool draw_label = true;      // 是否绘制标签
    };

    using Ptr = std::shared_ptr<BBoxDrawer>;
    using uPtr = std::unique_ptr<BBoxDrawer>;

    BBoxDrawer() = default;
    ~BBoxDrawer();

    // 初始化
    bool init(int width, int height);

    // 在YUV帧上绘制检测框
    AVFrame* draw_bbox(YUVFrame& input_frame, int x, int y, int width, int height,
                       const std::string& label = "person");

    // 更新设置
    void update_settings(const BBoxSettings& settings);
    BBoxSettings get_settings();

    // 获取输出分辨率
    void get_dst_size(int& width, int& height) { width = dst_width_; height = dst_height_; }

private:
    // 绘制矩形框（水平线）- 对齐版本
    void draw_h_line_aligned(uint8_t* y_data, uint8_t* u_data, uint8_t* v_data,
                            int x1, int x2, int y, int thickness,
                            int y_stride, int uv_stride,
                            uint8_t y_col, uint8_t u_col, uint8_t v_col,
                            int frame_width, int frame_height);

    // 绘制矩形框（垂直线）- 对齐版本
    void draw_v_line_aligned(uint8_t* y_data, uint8_t* u_data, uint8_t* v_data,
                            int x, int y1, int y2, int thickness,
                            int y_stride, int uv_stride,
                            uint8_t y_col, uint8_t u_col, uint8_t v_col,
                            int frame_width, int frame_height);

    // 绘制矩形框（水平线）
    void draw_h_line(uint8_t* y_data, uint8_t* u_data, uint8_t* v_data,
                     int x1, int x2, int y, int thickness,
                     int y_stride, int uv_stride,
                     uint8_t y_col, uint8_t u_col, uint8_t v_col,
                     int frame_width, int frame_height);

    // 绘制矩形框（垂直线）
    void draw_v_line(uint8_t* y_data, uint8_t* u_data, uint8_t* v_data,
                     int x, int y1, int y2, int thickness,
                     int y_stride, int uv_stride,
                     uint8_t y_col, uint8_t u_col, uint8_t v_col,
                     int frame_width, int frame_height);

    // 绘制标签文本（简化版本，只画一个标记）
    void draw_label_mark(uint8_t* y_data, int x, int y, int size,
                         int y_stride, int frame_width, int frame_height);

private:
    std::mutex drawer_mutex_;

    BBoxSettings settings_;

    // 缩放上下文
    SwsContext* sws_ctx_ = nullptr;

    // 输出分辨率
    int dst_width_ = 800;
    int dst_height_ = 600;

    // 临时输出帧
    AVFrame* output_frame_ = nullptr;
    bool init_ok_ = false;
};

} // namespace emai