#pragma once

// HTML页面内容
const std::string HTML_PAGE = R"====(
    <!DOCTYPE html>
    <html lang="zh-CN">
    <head>
        <meta charset="UTF-8">
        <meta name="viewport" content="width=device-width, initial-scale=1.0">
        <title>RTSP视频流模糊处理器</title>
        <style>
            * {
                margin: 0;
                padding: 0;
                box-sizing: border-box;
            }
            
            body {
                font-family: "Microsoft YaHei", "Segoe UI", sans-serif;
                background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
                min-height: 100vh;
                padding: 20px;
            }
            
            .container {
                background: white;
                border-radius: 20px;
                box-shadow: 0 20px 40px rgba(0, 0, 0, 0.1);
                overflow: hidden;
                width: 100%;
                max-width: 1400px;
                margin: 0 auto;
                animation: fadeIn 0.5s ease;
            }
            
            @keyframes fadeIn {
                from { opacity: 0; transform: translateY(20px); }
                to { opacity: 1; transform: translateY(0); }
            }
            
            .header {
                background: linear-gradient(135deg, #4b6cb7 0%, #182848 100%);
                color: white;
                padding: 30px;
                text-align: center;
            }
            
            .header h1 {
                font-size: 32px;
                margin-bottom: 10px;
                font-weight: 600;
            }
            
            .header p {
                opacity: 0.9;
                font-size: 16px;
            }
            
            .content {
                padding: 30px;
                display: grid;
                grid-template-columns: 2fr 1fr;
                gap: 30px;
            }
            
            @media (max-width: 1200px) {
                .content {
                    grid-template-columns: 1fr;
                }
            }
            
            .main-content {
                display: flex;
                flex-direction: column;
                gap: 30px;
            }
            
            .video-section {
                background: #f8f9fa;
                padding: 20px;
                border-radius: 15px;
                border: 1px solid #e9ecef;
            }
            
            .video-title {
                color: #495057;
                margin-bottom: 15px;
                font-size: 20px;
                font-weight: 600;
                display: flex;
                align-items: center;
                gap: 10px;
            }
            
            .video-title i {
                color: #4b6cb7;
            }
            
            .video-container {
                width: 100%;
                background: #000;
                border-radius: 12px;
                overflow: hidden;
                box-shadow: 0 10px 30px rgba(0, 0, 0, 0.2);
                position: relative;
                aspect-ratio: 16/9;
            }
            
            #video {
                width: 100%;
                height: 100%;
                object-fit: contain;
                display: block;
            }
            
            .video-placeholder {
                position: absolute;
                top: 0;
                left: 0;
                right: 0;
                bottom: 0;
                display: flex;
                flex-direction: column;
                align-items: center;
                justify-content: center;
                color: white;
                font-size: 18px;
                background: linear-gradient(135deg, #2c3e50 0%, #4ca1af 100%);
                text-align: center;
            }
            
            .status-container {
                margin-top: 20px;
            }
            
            .status-box {
                background: #e7f5ff;
                padding: 20px;
                border-radius: 12px;
                border: 1px solid #d0ebff;
            }
            
            .status-title {
                font-size: 16px;
                color: #495057;
                margin-bottom: 10px;
                display: flex;
                align-items: center;
                gap: 8px;
            }
            
            .status-indicator {
                display: inline-block;
                width: 12px;
                height: 12px;
                border-radius: 50%;
                background: #6c757d;
                margin-right: 10px;
            }
            
            .status-indicator.active {
                background: #28a745;
                box-shadow: 0 0 10px rgba(40, 167, 69, 0.5);
                animation: pulseIndicator 1.5s infinite;
            }
            
            @keyframes pulseIndicator {
                0% { opacity: 1; }
                50% { opacity: 0.5; }
                100% { opacity: 1; }
            }
            
            .stream-url {
                margin-top: 10px;
                padding: 10px;
                background: white;
                border-radius: 8px;
                font-size: 12px;
                color: #1864ab;
                word-break: break-all;
            }
            
            .control-panel {
                background: #f8f9fa;
                padding: 25px;
                border-radius: 15px;
                border: 1px solid #e9ecef;
            }
            
            .panel-title {
                color: #495057;
                margin-bottom: 20px;
                font-size: 20px;
                font-weight: 600;
                display: flex;
                align-items: center;
                gap: 10px;
            }
            
            .panel-title i {
                color: #4b6cb7;
            }
            
            .input-group {
                margin-bottom: 20px;
            }
            
            .input-group label {
                display: block;
                margin-bottom: 8px;
                color: #495057;
                font-weight: 500;
                font-size: 14px;
                display: flex;
                align-items: center;
                gap: 5px;
            }
            
            .rtsp-input {
                width: 100%;
                padding: 12px 15px;
                border: 2px solid #e9ecef;
                border-radius: 10px;
                font-size: 14px;
                margin-bottom: 10px;
                transition: border-color 0.3s;
            }
            
            .rtsp-input:focus {
                outline: none;
                border-color: #4b6cb7;
                box-shadow: 0 0 0 3px rgba(75, 108, 183, 0.1);
            }
            
            .rtsp-input::placeholder {
                color: #adb5bd;
            }
            
            .btn {
                padding: 12px 20px;
                border: none;
                border-radius: 10px;
                font-size: 14px;
                font-weight: 500;
                cursor: pointer;
                transition: all 0.3s ease;
                width: 100%;
                margin-bottom: 10px;
                display: flex;
                align-items: center;
                justify-content: center;
                gap: 8px;
            }
            
            .btn-start {
                background: linear-gradient(135deg, #28a745 0%, #20c997 100%);
                color: white;
            }
            
            .btn-stop {
                background: linear-gradient(135deg, #dc3545 0%, #e83e8c 100%);
                color: white;
            }
            
            .btn:hover {
                opacity: 0.9;
                transform: translateY(-2px);
                box-shadow: 0 5px 15px rgba(0, 0, 0, 0.1);
            }
            
            .blur-controls {
                background: linear-gradient(135deg, #f8f9fa 0%, #e9ecef 100%);
                padding: 30px;
                border-radius: 15px;
                border: 2px solid #4b6cb7;
                box-shadow: 0 5px 15px rgba(75, 108, 183, 0.1);
                text-align: center;
                display: flex;
                flex-direction: column;
                align-items: center;
                justify-content: center;
                min-height: 200px;
            }
            
            .blur-controls .panel-title {
                color: #4b6cb7;
                justify-content: center;
                margin-bottom: 30px;
            }
            
            .blur-toggle-container {
                display: flex;
                flex-direction: column;
                align-items: center;
                justify-content: center;
                gap: 20px;
                width: 100%;
            }
            
            .toggle-switch {
                position: relative;
                display: inline-block;
                width: 80px;
                height: 40px;
            }
            
            .toggle-switch input {
                opacity: 0;
                width: 0;
                height: 0;
            }
            
            .toggle-slider {
                position: absolute;
                cursor: pointer;
                top: 0;
                left: 0;
                right: 0;
                bottom: 0;
                background-color: #ccc;
                transition: .4s;
                border-radius: 40px;
                box-shadow: inset 0 2px 5px rgba(0, 0, 0, 0.2);
            }
            
            .toggle-slider:before {
                position: absolute;
                content: "";
                height: 32px;
                width: 32px;
                left: 4px;
                bottom: 4px;
                background-color: white;
                transition: .4s;
                border-radius: 50%;
                box-shadow: 0 2px 5px rgba(0, 0, 0, 0.2);
            }
            
            input:checked + .toggle-slider {
                background: linear-gradient(135deg, #28a745 0%, #20c997 100%);
            }
            
            input:checked + .toggle-slider:before {
                transform: translateX(40px);
            }
            
            .toggle-label {
                display: flex;
                flex-direction: column;
                align-items: center;
                gap: 10px;
                font-weight: 600;
                color: #495057;
                font-size: 18px;
            }
            
            .toggle-status {
                font-size: 14px;
                color: #6c757d;
                margin-top: 5px;
            }
            
            .toggle-status.enabled {
                color: #28a745;
                font-weight: 600;
            }
            
            .toggle-status.disabled {
                color: #dc3545;
                font-weight: 600;
            }
            
            .auto-apply-note {
                font-size: 12px;
                color: #28a745;
                padding: 10px;
                background: #e7f5ff;
                border-radius: 8px;
                text-align: center;
                margin-top: 20px;
                display: flex;
                align-items: center;
                justify-content: center;
                gap: 5px;
                width: 100%;
            }
            
            .info-panel {
                margin-top: 30px;
                padding: 25px;
                background: linear-gradient(135deg, #f8f9fa 0%, #e9ecef 100%);
                border-radius: 15px;
                border-left: 5px solid #4b6cb7;
            }
            
            .info-panel h3 {
                color: #495057;
                margin-bottom: 15px;
                font-size: 20px;
                display: flex;
                align-items: center;
                gap: 10px;
            }
            
            .info-list {
                list-style: none;
            }
            
            .info-list li {
                margin: 12px 0;
                padding: 12px 15px;
                background: white;
                border-radius: 8px;
                font-size: 14px;
                color: #6c757d;
                border: 1px solid #dee2e6;
                display: flex;
                align-items: flex-start;
                gap: 10px;
            }
            
            .info-list li::before {
                content: "✓";
                color: #28a745;
                font-weight: bold;
            }
            
            .loading-overlay {
                display: none;
                position: absolute;
                top: 0;
                left: 0;
                right: 0;
                bottom: 0;
                background: rgba(0, 0, 0, 0.85);
                flex-direction: column;
                align-items: center;
                justify-content: center;
                color: white;
                font-size: 18px;
                z-index: 100;
            }
            
            .spinner {
                width: 50px;
                height: 50px;
                border: 4px solid rgba(255, 255, 255, 0.3);
                border-radius: 50%;
                border-top-color: white;
                animation: spin 1s ease-in-out infinite;
                margin-bottom: 20px;
            }
            
            @keyframes spin {
                to { transform: rotate(360deg); }
            }
            
            /* 图标样式 */
            .material-icons {
                font-family: 'Material Icons';
                font-weight: normal;
                font-style: normal;
                font-size: 24px;
                line-height: 1;
                letter-spacing: normal;
                text-transform: none;
                display: inline-block;
                white-space: nowrap;
                word-wrap: normal;
                direction: ltr;
                -webkit-font-smoothing: antialiased;
            }
            
            .material-icons-outlined {
                font-family: 'Material Icons Outlined';
                font-weight: normal;
                font-style: normal;
                font-size: 24px;
                line-height: 1;
                letter-spacing: normal;
                text-transform: none;
                display: inline-block;
                white-space: nowrap;
                word-wrap: normal;
                direction: ltr;
                -webkit-font-smoothing: antialiased;
            }
            
            .blur-icon {
                font-size: 48px;
                margin-bottom: 15px;
                color: #4b6cb7;
            }
            
            .blur-icon.enabled {
                color: #28a745;
            }
            
            .blur-icon.disabled {
                color: #dc3545;
            }
        </style>
        <!-- Material Icons -->
        <link href="https://fonts.googleapis.com/icon?family=Material+Icons|Material+Icons+Outlined" rel="stylesheet">
    </head>
    <body>
        <div class="container">
            <div class="header">
                <h1>🎥 RTSP视频流模糊处理器</h1>
                <p>单路视频流 • 实时预览</p>
            </div>
            
            <div class="content">
                <div class="main-content">
                    <div class="video-section">
                        <div class="video-title">
                            <span class="material-icons-outlined">videocam</span>
                            <span>视频预览画面</span>
                        </div>
                        <div class="video-container" id="videoContainer">
                            <div id="loading" class="loading-overlay">
                                <div class="spinner"></div>
                                <div>正在连接视频流...</div>
                            </div>
                            <div id="videoPlaceholder" class="video-placeholder">
                                <div style="margin-bottom: 20px;">
                                    <span class="material-icons-outlined" style="font-size: 48px;">videocam_off</span>
                                </div>
                                <h3 style="margin-bottom: 10px;">等待视频流</h3>
                                <p>请输入RTSP地址并开始播放</p>
                            </div>
                            <img id="video" src="" style="display: none;">
                        </div>
                        
                        <div class="status-container">
                            <div class="status-box">
                                <div class="status-title">
                                    <span class="material-icons-outlined">info</span>
                                    <span>播放状态</span>
                                </div>
                                <div id="statusText">未连接</div>
                                <div class="status-indicator" id="statusIndicator"></div>
                                <div id="currentStream" class="stream-url">
                                    <strong>当前流:</strong> <span id="streamUrl">未连接</span>
                                </div>
                            </div>
                        </div>
                    </div>
                    
                    <div class="info-panel">
                        <h3><span class="material-icons-outlined">help_outline</span> 使用说明</h3>
                        <ul class="info-list">
                            <li>输入RTSP地址开始播放视频流</li>
                            <li>点击右侧的开关来启用或禁用模糊效果</li>
                            <li>模糊区域在服务器端预定义</li>
                            <li>所有设置修改后立即生效</li>
                            <li>使用高斯模糊算法实现平滑的模糊效果</li>
                            <li>实时显示视频流状态和性能信息</li>
                            <li>无需重启即可应用设置</li>
                        </ul>
                    </div>
                </div>
                
                <div class="right-panel">
                    <div class="blur-controls">
                        <div class="panel-title">
                            <span class="material-icons">blur_on</span>
                            <span>模糊控制</span>
                        </div>
                        
                        <div class="blur-toggle-container">
                            <div id="blurIcon" class="blur-icon material-icons-outlined enabled">blur_on</div>
                            
                            <div class="toggle-label">
                                <span>模糊效果</span>
                                <label class="toggle-switch">
                                    <input type="checkbox" id="blurEnabled" checked>
                                    <span class="toggle-slider"></span>
                                </label>
                                <div id="blurStatus" class="toggle-status enabled">已启用</div>
                            </div>
                        </div>
                        
                        <div class="auto-apply-note" id="autoApplyNote">
                            <span class="material-icons-outlined" style="font-size: 14px;">autorenew</span>
                            <span>设置已启用自动应用</span>
                        </div>
                    </div>
                    
                    <div class="control-panel">
                        <div class="panel-title">
                            <span class="material-icons">settings</span>
                            <span>视频流控制</span>
                        </div>
                        
                        <div class="input-group">
                            <label>
                                <span class="material-icons-outlined" style="font-size: 18px;">videocam</span>
                                <span>RTSP地址</span>
                            </label>
                            <input type="text" 
                                   id="rtspUrl" 
                                   class="rtsp-input" 
                                   placeholder="rtsp://username:password@ip:port/path"
                                   value="rtsp://192.168.1.100:554/stream">
                        </div>
                        
                        <button id="startBtn" class="btn btn-start" onclick="startStream()">
                            <span class="material-icons">play_arrow</span>
                            <span>开始播放</span>
                        </button>
                        <button id="stopBtn" class="btn btn-stop" onclick="stopStream()">
                            <span class="material-icons">stop</span>
                            <span>停止播放</span>
                        </button>
                        
                        <div style="margin-top: 20px;">
                            <div class="panel-title" style="margin-bottom: 15px; font-size: 16px;">
                                <span class="material-icons-outlined" style="font-size: 20px;">speed</span>
                                <span>性能信息</span>
                            </div>
                            <div id="performanceInfo" style="font-size: 12px; color: #6c757d; padding: 10px; background: white; border-radius: 8px; border: 1px solid #dee2e6;">
                                帧率: -- fps<br>
                                延迟: -- ms<br>
                                状态: 等待连接...
                            </div>
                        </div>
                    </div>
                </div>
            </div>
        </div>
        
        <script>
            let streamInterval = null;
            let isStreaming = false;
            let currentStreamUrl = '';
            let retryCount = 0;
            const maxRetries = 3;
            let lastFrameTime = 0;
            let frameCount = 0;
            let fps = 0;
            let applyDebounceTimer = null; // 防抖定时器
            let pageJustLoaded = true; // 标记页面是否刚刚加载
            
            // 更新模糊控制UI状态
            function updateBlurUI(enabled) {
                const blurIcon = document.getElementById('blurIcon');
                const blurStatus = document.getElementById('blurStatus');
                const blurEnabled = document.getElementById('blurEnabled');
                
                if (enabled) {
                    blurIcon.className = 'blur-icon material-icons-outlined enabled';
                    blurIcon.textContent = 'blur_on';
                    blurStatus.textContent = '已启用';
                    blurStatus.className = 'toggle-status enabled';
                    blurEnabled.checked = true;
                } else {
                    blurIcon.className = 'blur-icon material-icons-outlined disabled';
                    blurIcon.textContent = 'blur_off';
                    blurStatus.textContent = '已禁用';
                    blurStatus.className = 'toggle-status disabled';
                    blurEnabled.checked = false;
                }
            }
            
            // 防抖函数 - 防止频繁发送请求
            function debounceApplySettings(delay = 500) {
                if (applyDebounceTimer) {
                    clearTimeout(applyDebounceTimer);
                }
                applyDebounceTimer = setTimeout(() => {
                    applyBlurSettings();
                }, delay);
            }
            
            function updateStatus(status, isActive = false) {
                const statusText = document.getElementById('statusText');
                const statusIndicator = document.getElementById('statusIndicator');
                const streamUrl = document.getElementById('streamUrl');
                
                statusText.textContent = status;
                statusIndicator.className = 'status-indicator' + (isActive ? ' active' : '');
                
                if (currentStreamUrl) {
                    streamUrl.textContent = currentStreamUrl;
                } else {
                    streamUrl.textContent = '未连接';
                }
            }
            
            function updatePerformanceInfo() {
                const perfInfo = document.getElementById('performanceInfo');
                const now = performance.now();
                const currentLatency = lastFrameTime ? (now - lastFrameTime).toFixed(0) : '--';
                
                perfInfo.innerHTML = `
                    帧率: ${fps} fps<br>
                    延迟: ${currentLatency} ms<br>
                    状态: ${isStreaming ? '播放中' : '已停止'}
                `;
            }
            
            function showLoading(show) {
                const loading = document.getElementById('loading');
                const video = document.getElementById('video');
                const videoPlaceholder = document.getElementById('videoPlaceholder');
                
                if (show) {
                    loading.style.display = 'flex';
                    video.style.display = 'none';
                    videoPlaceholder.style.display = 'none';
                } else {
                    loading.style.display = 'none';
                }
            }
            
            function startStream() {
                const rtspUrl = document.getElementById('rtspUrl').value.trim();
                
                if (!rtspUrl) {
                    alert('请输入RTSP地址');
                    return;
                }
                
                if (isStreaming) {
                    alert('已经在播放中，请先停止当前播放');
                    return;
                }
                
                showLoading(true);
                updateStatus('正在连接视频流...', true);
                
                fetch('/start_stream', {
                    method: 'POST',
                    headers: {
                        'Content-Type': 'application/x-www-form-urlencoded',
                    },
                    body: 'rtsp_url=' + encodeURIComponent(rtspUrl)
                })
                .then(response => response.json())
                .then(data => {
                    if (data.success) {
                        currentStreamUrl = rtspUrl;
                        isStreaming = true;
                        
                        // 显示视频元素
                        const video = document.getElementById('video');
                        const videoPlaceholder = document.getElementById('videoPlaceholder');
                        
                        video.style.display = 'block';
                        videoPlaceholder.style.display = 'none';
                        
                        // 开始获取视频帧
                        startVideoStream();
                        
                        updateStatus('正在播放', true);
                        showLoading(false);
                        
                        // 页面加载后第一次启动流时，自动应用设置
                        if (pageJustLoaded) {
                            pageJustLoaded = false;
                            setTimeout(() => {
                                applyBlurSettings();
                            }, 500);
                        }
                    } else {
                        updateStatus('连接失败: ' + data.error);
                        showLoading(false);
                        alert('无法连接到RTSP流: ' + data.error);
                    }
                })
                .catch(error => {
                    updateStatus('连接失败');
                    showLoading(false);
                    alert('请求失败: ' + error.message);
                    console.error('Error:', error);
                });
            }
            
            function startVideoStream() {
                if (streamInterval) {
                    clearInterval(streamInterval);
                }
                
                // 立即显示第一帧
                updateVideoFrame();
                
                // 每33ms更新一次帧（约30fps）
                streamInterval = setInterval(updateVideoFrame, 33);
                
                // 每100ms更新性能信息
                setInterval(() => {
                    if (isStreaming) {
                        updatePerformanceInfo();
                    }
                }, 100);
            }
            
            function updateVideoFrame() {
                if (!isStreaming) return;
                
                const video = document.getElementById('video');
                const timestamp = new Date().getTime();
                
                // 更新帧率统计
                frameCount++;
                const now = Date.now();
                if (now - lastFrameTime >= 1000) {
                    fps = frameCount;
                    frameCount = 0;
                    lastFrameTime = now;
                }
                
                // 添加一个随机参数防止缓存
                video.src = '/video_frame?t=' + timestamp + '&r=' + Math.random();
                
                // 添加加载超时处理
                const loadTimeout = setTimeout(() => {
                    if (video.complete === false || video.naturalWidth === 0) {
                        retryCount++;
                        if (retryCount < maxRetries) {
                            console.log('Frame load timeout, retrying...');
                            updateVideoFrame();
                        } else {
                            console.error('Failed to load frame after ' + maxRetries + ' retries');
                        }
                    }
                    clearTimeout(loadTimeout);
                }, 2000);
                
                // 图片加载成功后更新性能统计
                video.onload = function() {
                    retryCount = 0;
                    lastFrameTime = performance.now();
                };
            }
            
            function stopStream() {
                if (!isStreaming) {
                    alert('当前没有正在播放的视频流');
                    return;
                }
                
                if (confirm('确定要停止播放吗？')) {
                    fetch('/stop_stream', {
                        method: 'POST'
                    })
                    .then(response => response.json())
                    .then(data => {
                        if (data.success) {
                            resetVideoDisplay();
                        }
                    })
                    .catch(error => {
                        console.error('停止流时出错:', error);
                        resetVideoDisplay();
                    });
                }
            }
            
            function resetVideoDisplay() {
                if (streamInterval) {
                    clearInterval(streamInterval);
                    streamInterval = null;
                }
                
                isStreaming = false;
                currentStreamUrl = '';
                retryCount = 0;
                fps = 0;
                
                const video = document.getElementById('video');
                const videoPlaceholder = document.getElementById('videoPlaceholder');
                
                video.style.display = 'none';
                video.src = '';
                videoPlaceholder.style.display = 'flex';
                
                updateStatus('已停止播放', false);
                updatePerformanceInfo(0, 0);
            }
            
            // 应用模糊设置
            function applyBlurSettings() {
                const blurEnabled = document.getElementById('blurEnabled').checked;
                
                // 显示应用中的状态
                const autoApplyNote = document.getElementById('autoApplyNote');
                const originalText = autoApplyNote.innerHTML;
                autoApplyNote.innerHTML = '<span class="material-icons-outlined" style="font-size: 14px;">sync</span> <span>正在应用设置...</span>';
                autoApplyNote.style.color = '#ff9800';
                
                // 先更新UI状态
                updateBlurUI(blurEnabled);
                
                fetch('/update_blur_settings', {
                    method: 'POST',
                    headers: {
                        'Content-Type': 'application/x-www-form-urlencoded',
                    },
                    body: `enabled=${blurEnabled}`
                })
                .then(response => {
                    if (!response.ok) {
                        throw new Error('网络响应不正常');
                    }
                    return response.json();
                })
                .then(data => {
                    if (data.success) {
                        // 显示成功提示
                        autoApplyNote.innerHTML = '<span class="material-icons-outlined" style="font-size: 14px;">check_circle</span> <span>设置已应用</span>';
                        autoApplyNote.style.color = '#28a745';
                        
                        setTimeout(() => {
                            autoApplyNote.innerHTML = '<span class="material-icons-outlined" style="font-size: 14px;">autorenew</span> <span>设置已启用自动应用</span>';
                            autoApplyNote.style.color = '#28a745';
                        }, 2000);
                    } else {
                        // 显示错误提示
                        autoApplyNote.innerHTML = '<span class="material-icons-outlined" style="font-size: 14px;">error</span> <span>应用失败: ' + (data.error || '未知错误') + '</span>';
                        autoApplyNote.style.color = '#f44336';
                        
                        // 恢复之前的复选框状态
                        fetch('/get_blur_settings')
                            .then(response => response.json())
                            .then(settings => {
                                if (settings && settings.enabled !== undefined) {
                                    updateBlurUI(settings.enabled);
                                }
                            });
                        
                        setTimeout(() => {
                            autoApplyNote.innerHTML = originalText;
                            autoApplyNote.style.color = '#28a745';
                        }, 3000);
                    }
                })
                .catch(error => {
                    // 显示网络错误提示
                    autoApplyNote.innerHTML = '<span class="material-icons-outlined" style="font-size: 14px;">wifi_off</span> <span>网络错误，请重试</span>';
                    autoApplyNote.style.color = '#f44336';
                    
                    // 恢复之前的复选框状态
                    fetch('/get_blur_settings')
                        .then(response => response.json())
                        .then(settings => {
                            if (settings && settings.enabled !== undefined) {
                                updateBlurUI(settings.enabled);
                            }
                        });
                    
                    setTimeout(() => {
                        autoApplyNote.innerHTML = originalText;
                        autoApplyNote.style.color = '#28a745';
                    }, 3000);
                    
                    console.error('Error:', error);
                });
            }
            
            // 窗口大小变化时更新预览
            window.addEventListener('resize', function() {
                // 如果需要的话，可以在这里添加响应式调整
            });
            
            // 输入框回车事件
            document.getElementById('rtspUrl').addEventListener('keypress', function(e) {
                if (e.key === 'Enter') {
                    startStream();
                }
            });
            
            // 视频加载错误处理
            document.getElementById('video').addEventListener('error', function(e) {
                console.error('视频加载错误:', e);
                retryCount++;
                if (isStreaming && retryCount < maxRetries) {
                    setTimeout(updateVideoFrame, 100);
                } else {
                    console.error('视频流加载失败');
                }
            });
            
            // 开关事件 - 自动应用设置
            document.getElementById('blurEnabled').addEventListener('change', function() {
                // 开关改变时立即应用设置，不使用防抖
                applyBlurSettings();
            });
            
            // 页面加载时检查当前状态
            window.onload = function() {
                // 获取模糊设置
                fetch('/get_blur_settings')
                    .then(response => {
                        if (!response.ok) {
                            throw new Error('获取设置失败');
                        }
                        return response.json();
                    })
                    .then(settings => {
                        if (settings && settings.enabled !== undefined) {
                            // 更新UI
                            updateBlurUI(settings.enabled);
                        }
                        
                        // 获取流状态
                        return fetch('/stream_status');
                    })
                    .then(response => response.json())
                    .then(data => {
                        if (data.is_streaming && data.current_url) {
                            document.getElementById('rtspUrl').value = data.current_url;
                            currentStreamUrl = data.current_url;
                            isStreaming = true;
                            
                            const video = document.getElementById('video');
                            const videoPlaceholder = document.getElementById('videoPlaceholder');
                            
                            video.style.display = 'block';
                            videoPlaceholder.style.display = 'none';
                            
                            startVideoStream();
                            updateStatus('正在播放', true);
                        }
                    })
                    .catch(error => {
                        console.error('检查状态时出错:', error);
                    });
            };
        </script>
    </body>
    </html>
    )====";

// 空白JPEG图像（用于无视频时显示）
const uint8_t BLANK_JPEG[] = {
    0xff, 0xd8, 0xff, 0xe0, 0x00, 0x10, 0x4a, 0x46, 0x49, 0x46, 0x00, 0x01,
    0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0xff, 0xdb, 0x00, 0x43,
    0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xc0, 0x00, 0x0b, 0x08, 0x00, 0x01, 0x00,
    0x01, 0x01, 0x01, 0x11, 0x00, 0xff, 0xc4, 0x00, 0x14, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x03, 0xff, 0xc4, 0x00, 0x14, 0x10, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0xff, 0xda, 0x00, 0x0c, 0x03, 0x01, 0x00, 0x02, 0x10, 0x03, 0x10,
    0x00, 0x00, 0x01, 0x3f, 0x00, 0xff, 0xd9
};  