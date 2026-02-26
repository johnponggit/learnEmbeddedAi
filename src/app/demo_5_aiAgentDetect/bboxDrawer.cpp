#include "bboxDrawer.h"
#include <cstring>
#include "LogMacros.h"

namespace emai {

BBoxDrawer::~BBoxDrawer() {
    std::lock_guard<std::mutex> lock(drawer_mutex_);

    if (sws_ctx_) {
        sws_freeContext(sws_ctx_);
        sws_ctx_ = nullptr;
    }

    if (output_frame_) {
        av_frame_free(&output_frame_);
        output_frame_ = nullptr;
    }

    init_ok_ = false;
}

bool BBoxDrawer::init(int width, int height) {
    std::lock_guard<std::mutex> lock(drawer_mutex_);

    // 清理旧资源
    if (sws_ctx_) {
        sws_freeContext(sws_ctx_);
        sws_ctx_ = nullptr;
    }

    if (output_frame_) {
        av_frame_free(&output_frame_);
        output_frame_ = nullptr;
    }

    dst_width_ = width;
    dst_height_ = height;

    // 不再需要输出帧，直接在输入帧上绘制
    // BBoxDrawer 不做缩放，缩放由 MppH264Encoder 处理
    output_frame_ = nullptr;

    init_ok_ = true;
    LOG_INFO("BBoxDrawer initialized: " << width << "x" << height << " (no scaling)");
    return true;
}

AVFrame* BBoxDrawer::draw_bbox(YUVFrame& input_frame, int x, int y, int box_width, int box_height,
                               const std::string& label) {
    std::lock_guard<std::mutex> lock(drawer_mutex_);

    if (!init_ok_) {
        LOG_ERROR("BBoxDrawer not initialized");
        return input_frame.frame.get();
    }

    if (!settings_.enabled || box_width <= 0 || box_height <= 0) {
        return input_frame.frame.get();
    }

    // 直接在输入帧上绘制，不做缩放
    AVFrame* draw_frame = input_frame.frame.get();

    // 检查帧格式
    LOG_DEBUG("draw_bbox: format=" << draw_frame->format
              << " width=" << draw_frame->width << " height=" << draw_frame->height
              << " y_stride=" << draw_frame->linesize[0]
              << " uv_stride=" << draw_frame->linesize[1]);

    // 只支持 YUV420P 格式
    if (draw_frame->format != AV_PIX_FMT_YUV420P) {
        static bool warned = false;
        if (!warned) {
            LOG_WARN("BBoxDrawer only supports YUV420P, got format: " << draw_frame->format);
            warned = true;
        }
        return draw_frame;
    }

    // 限制边界
    x = std::max(0, std::min(x, draw_frame->width - 1));
    y = std::max(0, std::min(y, draw_frame->height - 1));
    int x2 = std::min(x + box_width, draw_frame->width - 1);
    int y2 = std::min(y + box_height, draw_frame->height - 1);
    box_width = x2 - x;
    box_height = y2 - y;

    if (box_width <= 0 || box_height <= 0) {
        return draw_frame;
    }

    // 获取YUV数据指针
    uint8_t* y_data = draw_frame->data[0];
    uint8_t* u_data = draw_frame->data[1];
    uint8_t* v_data = draw_frame->data[2];

    int y_stride = draw_frame->linesize[0];
    int uv_stride = draw_frame->linesize[1];
    int frame_width = draw_frame->width;
    int frame_height = draw_frame->height;

    int thickness = settings_.line_thickness;
    uint8_t y_col = settings_.y_color;
    uint8_t u_col = settings_.u_color;
    uint8_t v_col = settings_.v_color;

    // 绘制矩形框的四条边 - 确保从偶数位置开始，避免 UV 对齐问题
    // YUV420P 要求 UV 在 2x2 像素块上对齐
    int aligned_x = x & ~1;  // 偶数对齐
    int aligned_x2 = (x2 + 1) & ~1;  // 确保结束也是偶数
    int aligned_y = y & ~1;  // 偶数对齐
    int aligned_y2 = (y2 + 1) & ~1;  // 确保结束也是偶数

    // 上边
    draw_h_line_aligned(y_data, u_data, v_data, aligned_x, aligned_x2, aligned_y, thickness, y_stride, uv_stride, y_col, u_col, v_col, frame_width, frame_height);

    // 下边 (减去 thickness 避免重叠)
    draw_h_line_aligned(y_data, u_data, v_data, aligned_x, aligned_x2, aligned_y2, thickness, y_stride, uv_stride, y_col, u_col, v_col, frame_width, frame_height);

    // 左边
    draw_v_line_aligned(y_data, u_data, v_data, aligned_x, aligned_y, aligned_y2, thickness, y_stride, uv_stride, y_col, u_col, v_col, frame_width, frame_height);

    // 右边
    draw_v_line_aligned(y_data, u_data, v_data, aligned_x2, aligned_y, aligned_y2, thickness, y_stride, uv_stride, y_col, u_col, v_col, frame_width, frame_height);

    // // 绘制标签标记（简单的点或小矩形）
    // if (settings_.draw_label && label == "person") {
    //     draw_label_mark(y_data, x, std::max(0, y - 10), 8, y_stride, frame_width, frame_height);
    // }

    LOG_DEBUG("Drew bbox at (" << x << "," << y << ") size " << box_width << "x" << box_height);

    return draw_frame;
}

void BBoxDrawer::draw_h_line_aligned(uint8_t* y_data, uint8_t* u_data, uint8_t* v_data,
                                       int x1, int x2, int y, int thickness,
                                       int y_stride, int uv_stride,
                                       uint8_t y_col, uint8_t u_col, uint8_t v_col,
                                       int frame_width, int frame_height) {
    // 确保坐标是偶数（2的倍数）以与 UV 对齐
    x1 = (x1 + 1) & ~1;  // 向上取偶数
    x2 = x2 & ~1;         // 向下取偶数
    y = y & ~1;           // 确保 y 是偶数
    thickness = (thickness + 1) & ~1;  // 确保厚度是偶数

    if (x1 >= x2 || y >= frame_height) {
        return;
    }

    // 绘制 Y 分量
    for (int t = 0; t < thickness; t++) {
        int yy = y + t;
        if (yy >= frame_height) break;

        uint8_t* y_line = y_data + yy * y_stride;
        for (int x = x1; x < x2 && x < frame_width; x++) {
            y_line[x] = y_col;
        }
    }

    // // 绘制 UV 分量（每一行 Y 对应两行 UV 的一半）
    // int uv_y_start = y / 2;
    // int uv_y_end = (y + thickness - 1) / 2;
    // int uv_x1 = x1 / 2;
    // int uv_x2 = x2 / 2;

    // for (int uv_y = uv_y_start; uv_y <= uv_y_end && uv_y < frame_height / 2; uv_y++) {
    //     uint8_t* u_line = u_data + uv_y * uv_stride;
    //     uint8_t* v_line = v_data + uv_y * uv_stride;
    //     for (int x = uv_x1; x < uv_x2 && x < frame_width / 2; x++) {
    //         u_line[x] = u_col;
    //         v_line[x] = v_col;
    //     }
    // }
}

void BBoxDrawer::draw_v_line_aligned(uint8_t* y_data, uint8_t* u_data, uint8_t* v_data,
                                       int x, int y1, int y2, int thickness,
                                       int y_stride, int uv_stride,
                                       uint8_t y_col, uint8_t u_col, uint8_t v_col,
                                       int frame_width, int frame_height) {
    // 确保坐标是偶数（2的倍数）以与 UV 对齐
    x = x & ~1;           // 确保 x 是偶数
    y1 = (y1 + 1) & ~1;   // 向上取偶数
    y2 = y2 & ~1;         // 向下取偶数
    thickness = (thickness + 1) & ~1;  // 确保厚度是偶数

    if (y1 >= y2 || x >= frame_width) {
        return;
    }

    // 绘制 Y 分量
    for (int t = 0; t < thickness; t++) {
        int xx = x + t;
        if (xx >= frame_width) break;

        for (int y = y1; y < y2 && y < frame_height; y++) {
            uint8_t* y_line = y_data + y * y_stride;
            y_line[xx] = y_col;
        }
    }

    // 绘制 UV 分量（每一列 Y 对应两列 UV 的一半）asdftest
    // int uv_x_start = x / 2;
    // int uv_x_end = (x + thickness - 1) / 2;
    // int uv_y1 = y1 / 2;
    // int uv_y2 = y2 / 2;

    // for (int uv_x = uv_x_start; uv_x < uv_x_end && uv_x < frame_width / 2; uv_x++) {
    //     for (int uv_y = uv_y1; uv_y < uv_y2 && uv_y < frame_height / 2; uv_y++) {
    //         uint8_t* u_line = u_data + uv_y * uv_stride;
    //         uint8_t* v_line = v_data + uv_y * uv_stride;
    //         u_line[uv_x] = u_col;
    //         v_line[uv_x] = v_col;
    //     }
    // }
}

void BBoxDrawer::draw_h_line(uint8_t* y_data, uint8_t* u_data, uint8_t* v_data,
                             int x1, int x2, int y, int thickness,
                             int y_stride, int uv_stride,
                             uint8_t y_col, uint8_t u_col, uint8_t v_col,
                             int frame_width, int frame_height) {
    // 绘制 Y 分量（每一行都画）
    for (int t = 0; t < thickness; t++) {
        int yy = y + t;
        if (yy >= frame_height) break;

        uint8_t* y_line = y_data + yy * y_stride;
        for (int x = x1; x <= x2 && x < frame_width; x++) {
            y_line[x] = y_col;
        }
    }

    // 绘制 UV 分量（每两行 Y 对应一行 UV）
    // 从 y 的偶数行开始，画到 y + thickness 的范围
    int uv_y_start = y / 2;
    int uv_y_end = (y + thickness - 1) / 2;
    int uv_x1 = x1 / 2;
    int uv_x2 = x2 / 2;

    for (int uv_y = uv_y_start; uv_y <= uv_y_end && uv_y < frame_height / 2; uv_y++) {
        uint8_t* u_line = u_data + uv_y * uv_stride;
        uint8_t* v_line = v_data + uv_y * uv_stride;
        for (int x = uv_x1; x <= uv_x2 && x < frame_width / 2; x++) {
            u_line[x] = u_col;
            v_line[x] = v_col;
        }
    }
}

void BBoxDrawer::draw_v_line(uint8_t* y_data, uint8_t* u_data, uint8_t* v_data,
                             int x, int y1, int y2, int thickness,
                             int y_stride, int uv_stride,
                             uint8_t y_col, uint8_t u_col, uint8_t v_col,
                             int frame_width, int frame_height) {
    // 绘制 Y 分量（每一列都画）
    for (int t = 0; t < thickness; t++) {
        int xx = x + t;
        if (xx >= frame_width) break;

        for (int y = y1; y <= y2 && y < frame_height; y++) {
            uint8_t* y_line = y_data + y * y_stride;
            y_line[xx] = y_col;
        }
    }

    // 绘制 UV 分量（每两列 Y 对应一列 UV）
    int uv_x_start = x / 2;
    int uv_x_end = (x + thickness - 1) / 2;
    int uv_y1 = y1 / 2;
    int uv_y2 = y2 / 2;

    for (int uv_x = uv_x_start; uv_x <= uv_x_end && uv_x < frame_width / 2; uv_x++) {
        for (int uv_y = uv_y1; uv_y <= uv_y2 && uv_y < frame_height / 2; uv_y++) {
            uint8_t* u_line = u_data + uv_y * uv_stride;
            uint8_t* v_line = v_data + uv_y * uv_stride;
            u_line[uv_x] = u_col;
            v_line[uv_x] = v_col;
        }
    }
}

void BBoxDrawer::draw_label_mark(uint8_t* y_data, int x, int y, int size,
                                   int y_stride, int frame_width, int frame_height) {
    if (y < 0 || x < 0) return;

    // 在框上方画一个小标记
    for (int dy = 0; dy < size && y + dy < frame_height; dy++) {
        for (int dx = 0; dx < size && x + dx < frame_width; dx++) {
            uint8_t* y_line = y_data + (y + dy) * y_stride;
            y_line[x + dx] = 255;  // 白色标记
        }
    }
}

void BBoxDrawer::update_settings(const BBoxSettings& settings) {
    std::lock_guard<std::mutex> lock(drawer_mutex_);
    settings_ = settings;
    LOG_INFO("BBoxDrawer settings updated: thickness=" << settings_.line_thickness
             << ", enabled=" << settings_.enabled);
}

BBoxDrawer::BBoxSettings BBoxDrawer::get_settings() {
    std::lock_guard<std::mutex> lock(drawer_mutex_);
    return settings_;
}

} // namespace emai