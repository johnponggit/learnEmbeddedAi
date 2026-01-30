#pragma once

#include <vector>
#include <cstdint>
#include <string>
#include <cstring>  
#include <memory>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>

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

struct YUVFrame {
    enum ToAVFrameMode {
        REFERENCE,      // 返回引用计数的AVFrame（共享数据）
        CLONE,         // 完整克隆AVFrame
        COPY_WRITABLE  // 拷贝数据并确保可写（用于编码）
    };

    int width = 0;
    int height = 0;
    AVPixelFormat format = AV_PIX_FMT_NONE;
    std::shared_ptr<AVFrame> frame;  // 保存AVFrame的智能指针
    bool own_data = false;            // 是否拥有数据所有权
    int64_t pts = 0;
    int64_t timestamp = 0;
    
    YUVFrame() = default;
    
    // 构造函数：直接引用AVFrame，不拷贝数据
    YUVFrame(AVFrame* src_frame, bool copy_data = true) {
        from_avframe(src_frame, copy_data);
    }
    
    // 拷贝构造函数（深拷贝）
    YUVFrame(const YUVFrame& other) {
        *this = other;
    }
    
    // 拷贝赋值操作符（深拷贝）
    YUVFrame& operator=(const YUVFrame& other) {
        if (this == &other) {
            return *this;
        }
        
        // 清空当前数据
        reset();
        
        // 复制基本属性
        width = other.width;
        height = other.height;
        format = other.format;
        pts = other.pts;
        timestamp = other.timestamp;
        own_data = false; // 初始化为false，会在下面根据情况设置
        
        // 复制AVFrame数据
        if (other.frame) {
            // 如果other是自己拥有数据，或者我们强制需要拷贝数据
            if (other.own_data) {
                // 使用av_frame_clone进行深拷贝
                AVFrame* cloned_frame = av_frame_clone(other.frame.get());
                if (cloned_frame) {
                    frame = std::shared_ptr<AVFrame>(cloned_frame, 
                                                    [](AVFrame* f) { av_frame_free(&f); });
                    own_data = true;
                }
            } else {
                // 如果other只是引用，我们也只保存引用
                frame = std::shared_ptr<AVFrame>(other.frame.get(),
                                                [](AVFrame* f) { av_frame_unref(f); });
                // 增加引用计数
                av_frame_ref(frame.get(), other.frame.get());
                own_data = false;
            }
        }
        
        return *this;
    }
    
    // 移动构造函数
    YUVFrame(YUVFrame&& other) noexcept {
        // LOG_DEBUG("YUVFrame move constructor"); 
        *this = std::move(other);
    }
    
    // 移动赋值操作符
    YUVFrame& operator=(YUVFrame&& other) noexcept {
        if (this != &other) {
            reset();
            
            width = other.width;
            height = other.height;
            format = other.format;
            pts = other.pts;
            timestamp = other.timestamp;
            frame = std::move(other.frame);
            own_data = other.own_data;
            
            other.width = 0;
            other.height = 0;
            other.format = AV_PIX_FMT_NONE;
            other.pts = 0;
            other.timestamp = 0;
            other.own_data = false;

            // LOG_DEBUG("YUVFrame move assignment"); 

        }
        return *this;
    }
    
    // 从AVFrame初始化
    void from_avframe(AVFrame* src_frame, bool copy_data = false) {
        if (!src_frame) return;
        
        width = src_frame->width;
        height = src_frame->height;
        format = (AVPixelFormat)src_frame->format;
        pts = src_frame->pts;
        timestamp = av_gettime() / 1000;
        
        if (copy_data) {
            // 需要拷贝数据
            frame = std::shared_ptr<AVFrame>(av_frame_clone(src_frame), 
                                            [](AVFrame* f) { av_frame_free(&f); });
            own_data = true;
        } else {
            // 只保存引用，增加引用计数
            frame = std::shared_ptr<AVFrame>(src_frame, 
                                            [](AVFrame* f) { 
                                                // 只减少引用计数，不实际释放
                                                av_frame_unref(f);
                                            });
            // 增加引用计数
            av_frame_ref(frame.get(), src_frame);
            own_data = false;
        }
    }
    
    // 转换为AVFrame（支持多种模式）
    AVFrame* to_avframe(ToAVFrameMode mode = REFERENCE) const {
        if (!frame) return nullptr;
        
        switch (mode) {
            case REFERENCE: {
                // 模式1：返回引用计数的AVFrame（效率最高）
                AVFrame* new_frame = av_frame_alloc();
                if (!new_frame) return nullptr;
                
                // 复制基本属性
                av_frame_copy_props(new_frame, frame.get());
                new_frame->width = width;
                new_frame->height = height;
                new_frame->format = format;
                new_frame->pts = pts;
                
                // 复制数据引用（不拷贝实际数据）
                for (int i = 0; i < AV_NUM_DATA_POINTERS; i++) {
                    if (frame->data[i]) {
                        new_frame->data[i] = frame->data[i];
                        new_frame->linesize[i] = frame->linesize[i];
                    }
                }
                
                // 增加引用计数
                av_frame_ref(new_frame, frame.get());
                return new_frame;
            }
            
            case CLONE: {
                // 模式2：完整克隆AVFrame（包括数据）
                return av_frame_clone(frame.get());
            }
            
            case COPY_WRITABLE: {
                // 模式3：拷贝数据并确保可写（用于编码）
                AVFrame* new_frame = av_frame_alloc();
                if (!new_frame) return nullptr;
                
                // 复制属性
                new_frame->width = width;
                new_frame->height = height;
                new_frame->format = format;
                new_frame->pts = pts;
                
                // 分配新缓冲区
                if (av_frame_get_buffer(new_frame, 32) < 0) {
                    av_frame_free(&new_frame);
                    return nullptr;
                }
                
                // 复制数据
                if (av_frame_copy(new_frame, frame.get()) < 0) {
                    av_frame_free(&new_frame);
                    return nullptr;
                }
                
                // 确保可写（如果需要修改数据）
                if (av_frame_make_writable(new_frame) < 0) {
                    av_frame_free(&new_frame);
                    return nullptr;
                }
                
                return new_frame;
            }
            
            default:
                return nullptr;
        }
    }
    
    // 获取Y平面数据（延迟分配）
    const uint8_t* y_data() const {
        return frame ? frame->data[0] : nullptr;
    }
    
    const uint8_t* u_data() const {
        return frame ? frame->data[1] : nullptr;
    }
    
    const uint8_t* v_data() const {
        return frame ? frame->data[2] : nullptr;
    }
    
    int y_stride() const {
        return frame ? frame->linesize[0] : 0;
    }
    
    int u_stride() const {
        return frame ? frame->linesize[1] : 0;
    }
    
    int v_stride() const {
        return frame ? frame->linesize[2] : 0;
    }
    
    // 转换为RGB时，使用libswscale（性能更好）
    std::vector<uint8_t> to_rgb() const {
        if (!frame || format != AV_PIX_FMT_YUV420P) {
            return {};
        }
        
        std::vector<uint8_t> rgb_data(width * height * 3);
        
        // 使用libswscale进行转换
        SwsContext* sws_ctx = sws_getContext(
            width, height, format,
            width, height, AV_PIX_FMT_RGB24,
            SWS_BILINEAR, nullptr, nullptr, nullptr);
        
        if (!sws_ctx) {
            return {};
        }
        
        uint8_t* dst_data[1] = { rgb_data.data() };
        int dst_linesize[1] = { width * 3 };
        
        sws_scale(sws_ctx, frame->data, frame->linesize,
                  0, height, dst_data, dst_linesize);
        
        sws_freeContext(sws_ctx);
        return rgb_data;
    }
    
    // 需要拷贝数据时调用
    void copy_from_frame(AVFrame* src_frame) {
        from_avframe(src_frame, true);
    }
    
    // 交换两个YUVFrame对象的内容
    void swap(YUVFrame& other) noexcept {
        std::swap(width, other.width);
        std::swap(height, other.height);
        std::swap(format, other.format);
        std::swap(pts, other.pts);
        std::swap(timestamp, other.timestamp);
        std::swap(frame, other.frame);
        std::swap(own_data, other.own_data);
    }
    
    // 释放引用，但保留AVFrame结构
    void unref() {
        if (frame) {
            av_frame_unref(frame.get());
        }
    }
    
    // 完全释放
    void reset() {
        frame.reset();
        width = 0;
        height = 0;
        format = AV_PIX_FMT_NONE;
        pts = 0;
        timestamp = 0;
        own_data = false;
    }
    
    bool empty() const {
        return !frame || width == 0 || height == 0;
    }
    
    // 获取引用计数
    int ref_count() const {
        return frame ? frame.use_count() : 0;
    }
    
    // 确保AVFrame可写（用于修改数据）
    bool make_writable() {
        if (!frame) return false;
        
        // 如果引用计数大于1，需要拷贝数据
        if (ref_count() > 1) {
            AVFrame* new_frame = av_frame_clone(frame.get());
            if (!new_frame) return false;
            
            // 释放旧frame，使用新的
            frame.reset(new_frame, [](AVFrame* f) { av_frame_free(&f); });
            own_data = true;
        }
        
        // 确保AVFrame内部缓冲区可写
        return av_frame_make_writable(frame.get()) >= 0;
    }
    
    // 比较操作符
    bool operator==(const YUVFrame& other) const {
        if (this == &other) return true;
        if (width != other.width || height != other.height || format != other.format)
            return false;
        if (frame.get() == other.frame.get()) return true;
        
        // 比较数据内容（如果需要）
        // 这里简化比较，只比较指针
        return false;
    }
    
    bool operator!=(const YUVFrame& other) const {
        return !(*this == other);
    }
};

// 全局swap函数
inline void swap(YUVFrame& a, YUVFrame& b) noexcept {
    a.swap(b);
}

// YUV帧缓冲区
class YUVFrameBuffer {
private:
    std::queue<YUVFrame>    frames;
    std::mutex              mtx;                  // 互斥锁
    std::condition_variable cv;                   // 条件变量
    size_t                  max_size = 100;        // 缓冲区最大大小
    std::atomic<bool>       has_new_frame{false}; // 标记是否有新帧
    
public:
    void push(YUVFrame& frame) {
        std::unique_lock<std::mutex> lock(mtx);
        
        // 如果缓冲区已满，丢弃最旧的帧
        if (frames.size() >= max_size) {
            frames.pop();
        }
        
        // 注意：这里需要拷贝frame，因为frame可能在其他地方还会被使用
        // 使用拷贝构造函数
        frames.push(frame); // 这会调用拷贝构造函数
        has_new_frame = true;
        cv.notify_one();  // 通知等待的线程
    }
    
    // 优化版本：使用移动语义，避免拷贝
    void push(YUVFrame&& frame) {
        std::unique_lock<std::mutex> lock(mtx);
        
        // 如果缓冲区已满，丢弃最旧的帧
        if (frames.size() >= max_size) {
            frames.pop();
        }
        
        // 使用移动构造函数
        frames.push(std::move(frame));
        // LOG_DEBUG("YUVFrameBuffer push move,frame size:" << frames.size());
        has_new_frame = true;
        cv.notify_one();  // 通知等待的线程
    }
    
    bool get_latest(YUVFrame& frame, int timeout_ms = 50) {
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
        frame = std::move(frames.back());
        
        while (frames.size() > 0) {
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