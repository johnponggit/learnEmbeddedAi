
## 🔄 **整体架构**

```
HTML/JavaScript 前端 (浏览器)
        ↓ HTTP 请求/响应
C++ HTTP 服务器 (后端)
        ↓ 内部调用
视频处理引擎 (OpenCV/GStreamer)
```

## 🌐 **HTTP API 接口**

### **1. 视频流控制接口**

```cpp
// 1. 开始播放视频流
POST /start_stream
参数: rtsp_url=rtsp://...
响应: JSON { "success": true/false, "error": "..." }

// 2. 停止播放视频流  
POST /stop_stream
响应: JSON { "success": true/false }

// 3. 获取当前视频帧 (实时)
GET /video_frame?t=时间戳&r=随机数
响应: JPEG 图像二进制数据
```

### **2. 模糊设置接口**

```cpp
// 4. 更新模糊设置
POST /update_blur_settings
参数: x=100&y=100&width=200&height=150&blur_radius=5&...

// 5. 获取模糊设置
GET /get_blur_settings
响应: JSON 模糊设置

// 6. 获取流状态
GET /stream_status
响应: JSON 流状态
```


## 🔧 **详细交互流程**

### **1. 页面加载过程**
```javascript
// 前端加载时自动调用
window.onload = function() {
    // 检查当前状态
    fetch('/stream_status')
        .then(response => response.json())
        .then(data => {
            if (data.is_streaming) {
                // 如果已经在播放，恢复播放
                startVideoStream();
            }
        });
    
    // 获取当前模糊设置
    fetch('/get_blur_settings')
        .then(response => response.json())
        .then(settings => {
            // 更新前端UI滑块和复选框
            document.getElementById('blurX').value = settings.x;
            document.getElementById('blurEnabled').checked = settings.enabled;
            // ...
        });
};
```

### **2. 开始播放视频流**
```javascript
// 前端点击"开始播放"按钮
function startStream() {
    const rtspUrl = document.getElementById('rtspUrl').value;
    
    fetch('/start_stream', {
        method: 'POST',
        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
        body: 'rtsp_url=' + encodeURIComponent(rtspUrl)
    })
    .then(response => response.json())
    .then(data => {
        if (data.success) {
            // 开始获取视频帧
            startVideoStream();
        }
    });
}
```

### **3. 实时获取视频帧**
```javascript
// 前端定期获取视频帧
function updateVideoFrame() {
    if (!isStreaming) return;
    
    // 使用时间戳防止缓存
    const timestamp = new Date().getTime();
    const video = document.getElementById('video');
    
    // 设置img的src为视频帧URL
    video.src = '/video_frame?t=' + timestamp + '&r=' + Math.random();
}
```

### **4. 点击画面设置模糊区域**
```javascript
// 前端处理视频点击
function handleVideoClick(event) {
    const rect = videoContainer.getBoundingClientRect();
    const clickX = event.clientX - rect.left;
    const clickY = event.clientY - rect.top;
    
    // 转换为视频坐标（800x600）
    const originalX = (clickX - videoOffsetX) * (800 / videoDisplayWidth);
    const originalY = (clickY - videoOffsetY) * (600 / videoDisplayHeight);
    
    // 更新滑块
    document.getElementById('blurX').value = originalX;
    
    // 自动应用设置
    applyBlurSettings();  // 调用POST /update_blur_settings
}
```
