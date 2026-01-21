#pragma once

#include <vector>
#include <cstdint>
#include <string>
#include <cstring>  

#ifdef __cplusplus
extern "C" {
#endif
#include <libswscale/swscale.h>
#include <libavutil/opt.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/hwcontext.h>
#include <libavutil/time.h>  
#ifdef __cplusplus
}
#endif

namespace emai {

// YUV帧结构
struct YUVFrame {
    int                  width = 0;
    int                  height = 0;
    AVPixelFormat        format = AV_PIX_FMT_NONE;
    std::vector<uint8_t> y_plane;
    std::vector<uint8_t> u_plane;
    std::vector<uint8_t> v_plane;
    int                  y_stride = 0;
    int                  uv_stride = 0;
    int64_t              pts = 0; // 时间戳
    int64_t              timestamp = 0; // 系统时间戳
    
    YUVFrame() = default;
    
    YUVFrame(AVFrame* frame) {
        if (!frame) return;
        
        width = frame->width;
        height = frame->height;
        format = (AVPixelFormat)frame->format;
        pts = frame->pts;
        timestamp = av_gettime() / 1000; // 毫秒
        
        // 计算步长
        y_stride = frame->linesize[0];
        uv_stride = frame->linesize[1];
        
        // 复制Y平面
        size_t y_size = y_stride * height;
        y_plane.resize(y_size);
        memcpy(y_plane.data(), frame->data[0], y_size);
        
        // 复制UV平面（对于YUV420P，UV平面高度是Y平面的一半）
        size_t uv_height = height / 2;
        size_t u_size = uv_stride * uv_height;
        size_t v_size = uv_stride * uv_height;
        
        u_plane.resize(u_size);
        v_plane.resize(v_size);
        
        memcpy(u_plane.data(), frame->data[1], u_size);
        memcpy(v_plane.data(), frame->data[2], v_size);
    }
    
    // 复制构造函数
    YUVFrame(const YUVFrame& other) {
        width = other.width;
        height = other.height;
        format = other.format;
        y_stride = other.y_stride;
        uv_stride = other.uv_stride;
        pts = other.pts;
        timestamp = other.timestamp;
        
        y_plane = other.y_plane;
        u_plane = other.u_plane;
        v_plane = other.v_plane;
    }
    
    // 赋值运算符
    YUVFrame& operator=(const YUVFrame& other) {
        if (this != &other) {
            width = other.width;
            height = other.height;
            format = other.format;
            y_stride = other.y_stride;
            uv_stride = other.uv_stride;
            pts = other.pts;
            timestamp = other.timestamp;
            
            y_plane = other.y_plane;
            u_plane = other.u_plane;
            v_plane = other.v_plane;
        }
        return *this;
    }
    
    // 移动构造函数
    YUVFrame(YUVFrame&& other) noexcept {
        width = other.width;
        height = other.height;
        format = other.format;
        y_stride = other.y_stride;
        uv_stride = other.uv_stride;
        pts = other.pts;
        timestamp = other.timestamp;
        
        y_plane = std::move(other.y_plane);
        u_plane = std::move(other.u_plane);
        v_plane = std::move(other.v_plane);
        
        other.width = 0;
        other.height = 0;
        other.y_stride = 0;
        other.uv_stride = 0;
        other.pts = 0;
        other.timestamp = 0;
    }
    
    // 转换为AVFrame（用于编码）- 修改为const方法
    AVFrame* to_avframe() const {
        AVFrame* frame = av_frame_alloc();
        if (!frame) return nullptr;
        
        frame->width = width;
        frame->height = height;
        frame->format = format;
        frame->pts = pts;
        
        // 分配缓冲区
        if (av_frame_get_buffer(frame, 32) < 0) {
            av_frame_free(&frame);
            return nullptr;
        }
        
        // 复制数据
        memcpy(frame->data[0], y_plane.data(), y_plane.size());
        memcpy(frame->data[1], u_plane.data(), u_plane.size());
        memcpy(frame->data[2], v_plane.data(), v_plane.size());
        
        return frame;
    }
    
    bool empty() const {
        return width == 0 || height == 0 || y_plane.empty();
    }
};

// YUV帧缓冲区
class YUVFrameBuffer {
private:
    std::queue<emai::YUVFrame>    frames;
    std::mutex              mtx;                  // 互斥锁
    std::condition_variable cv;                   // 条件变量
    size_t                  max_size = 25;        // 缓冲区最大大小
    std::atomic<bool>       has_new_frame{false}; // 标记是否有新帧
    
public:
    void push(const emai::YUVFrame& frame) {
        std::unique_lock<std::mutex> lock(mtx);
        
        // 如果缓冲区已满，丢弃最旧的帧
        if (frames.size() >= max_size) {
            frames.pop();
        }
        
        frames.push(frame);
        has_new_frame = true;
        cv.notify_one();  // 通知等待的线程
    }
    
    bool get_latest(emai::YUVFrame& frame, int timeout_ms = 50) {
        std::unique_lock<std::mutex> lock(mtx);
        
        // 等待新帧到来
        if (frames.empty()) {
            if (timeout_ms > 0) {
                // 等待指定的时间
                auto status = cv.wait_for(lock, std::chrono::milliseconds(timeout_ms));
                if (status == std::cv_status::timeout) {
                    return false;  // 超时返回
                }
            } else {
                return false;
            }
        }
        
        // 获取最新帧
        frame = frames.back();
        
        // 清空旧帧，只保留最新的一帧
        while (frames.size() > 1) {
            frames.pop();
        }
        
        has_new_frame = false;
        return true;
    }
    
    void clear() {
        std::lock_guard<std::mutex> lock(mtx);
        while (!frames.empty()) {
            frames.pop();
        }
        has_new_frame = false;
        cv.notify_all();  // 通知所有等待的线程
    }
    
    bool empty() {
        std::lock_guard<std::mutex> lock(mtx);
        return frames.empty();
    }
    
    size_t size() {
        std::lock_guard<std::mutex> lock(mtx);
        return frames.size();
    }
};

} // namespace emai