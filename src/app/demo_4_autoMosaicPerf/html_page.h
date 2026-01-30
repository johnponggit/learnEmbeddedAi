// html_page.h
#pragma once

#include <string>
#include <cstdint>

// HTML页面内容 - 使用FLV流播放
const std::string HTML_PAGE = R"====(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>RTSP视频流马赛克处理器 - FLV流模式</title>
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
            display: flex;
            gap: 30px;
        }
        
        @media (max-width: 1200px) {
            .content {
                flex-direction: column;
            }
        }
        
        .left-panel {
            flex: 1;
            display: flex;
            flex-direction: column;
            gap: 20px;
        }
        
        .right-panel {
            width: 420px;
            display: flex;
            flex-direction: column;
            gap: 20px;
        }
        
        @media (max-width: 1200px) {
            .right-panel {
                width: 100%;
            }
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
        
        .video-container {
            width: 100%;
            background: #000;
            border-radius: 12px;
            overflow: hidden;
            box-shadow: 0 10px 30px rgba(0, 0, 0, 0.2);
            position: relative;
            aspect-ratio: 16/9;
        }
        
        #videoElement {
            width: 100%;
            height: 100%;
            object-fit: contain;
            background: #000;
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
        
        .connection-controls {
            margin-top: 15px;
            display: flex;
            gap: 10px;
        }
        
        .rtsp-input {
            flex: 1;
            padding: 12px 15px;
            border: 2px solid #e9ecef;
            border-radius: 10px;
            font-size: 14px;
            transition: border-color 0.3s;
        }
        
        .rtsp-input:focus {
            outline: none;
            border-color: #4b6cb7;
            box-shadow: 0 0 0 3px rgba(75, 108, 183, 0.1);
        }
        
        .btn {
            padding: 12px 25px;
            border: none;
            border-radius: 10px;
            font-size: 14px;
            font-weight: 500;
            cursor: pointer;
            transition: all 0.3s ease;
            display: flex;
            align-items: center;
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
        
        .status-container {
            margin-top: 15px;
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
        
        .mosaic-controls {
            background: #f8f9fa;
            padding: 20px;
            border-radius: 15px;
            border: 1px solid #e9ecef;
            display: flex;
            justify-content: space-between;
            align-items: center;
        }
        
        .toggle-container {
            display: flex;
            align-items: center;
            gap: 15px;
        }
        
        .toggle-label {
            font-size: 16px;
            font-weight: 500;
            color: #495057;
            display: flex;
            align-items: center;
            gap: 8px;
        }
        
        .toggle-switch {
            position: relative;
            display: inline-block;
            width: 60px;
            height: 34px;
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
            border-radius: 34px;
        }
        
        .toggle-slider:before {
            position: absolute;
            content: "";
            height: 26px;
            width: 26px;
            left: 4px;
            bottom: 4px;
            background-color: white;
            transition: .4s;
            border-radius: 50%;
        }
        
        input:checked + .toggle-slider {
            background-color: #28a745;
        }
        
        input:checked + .toggle-slider:before {
            transform: translateX(26px);
        }
        
        .perf-board {
            display: flex;
            flex-direction: column;
            gap: 15px;
            padding: 25px;
            background: white;
            border-radius: 15px;
            box-shadow: 0 10px 25px rgba(0,0,0,0.05);
            border: 1px solid #e9ecef;
        }
        
        .perf-board-title {
            font-size: 20px;
            font-weight: 600;
            color: #495057;
            display: flex;
            align-items: center;
            gap: 10px;
            margin-bottom: 10px;
            padding-bottom: 15px;
            border-bottom: 2px solid #e9ecef;
        }
        
        .perf-flow {
            display: flex;
            flex-direction: column;
            gap: 12px;
        }
        
        .perf-node {
            display: flex;
            align-items: center;
            justify-content: space-between;
            background: #f8f9fa;
            border-radius: 10px;
            padding: 12px 18px;
            border-left: 4px solid #4b6cb7;
            transition: all 0.3s ease;
        }
        
        .perf-node:hover {
            transform: translateX(5px);
            box-shadow: 0 3px 10px rgba(0,0,0,0.1);
            background: white;
        }
        
        .perf-label {
            font-size: 13px;
            color: #6c757d;
            font-weight: 600;
            display: flex;
            align-items: center;
            gap: 8px;
        }
        
        .perf-value {
            font-size: 18px;
            font-weight: bold;
            color: #2c3e50;
            font-family: 'Consolas', 'Monaco', monospace;
            min-width: 80px;
            text-align: right;
        }
        
        .perf-arrow {
            display: flex;
            align-items: center;
            justify-content: center;
            margin: -5px 0;
            color: #adb5bd;
        }
        
        .perf-node.highlight-red {
            border-left: 4px solid #dc3545;
            background: #fff5f5;
        }
        
        .perf-node.highlight-red .perf-value {
            color: #dc3545;
        }
        
        .perf-node.highlight-green {
            border-left: 4px solid #28a745;
            background: #f8fff9;
        }
        
        .perf-node.highlight-green .perf-value {
            color: #28a745;
        }
        
        .loading-overlay {
            position: absolute;
            top: 0;
            left: 0;
            right: 0;
            bottom: 0;
            background: rgba(0, 0, 0, 0.85);
            display: flex;
            flex-direction: column;
            align-items: center;
            justify-content: center;
            color: white;
            font-size: 18px;
            z-index: 100;
            border-radius: 12px;
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
        
        /* 兼容性提示 */
        .compatibility-notice {
            background: #fff3cd;
            border: 1px solid #ffecb5;
            color: #856404;
            padding: 12px;
            border-radius: 8px;
            margin-top: 15px;
            font-size: 14px;
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
    </style>
    <!-- 引入flv.js -->
    <script src="https://cdn.jsdelivr.net/npm/flv.js@latest/dist/flv.min.js"></script>
    <!-- Material Icons -->
    <link href="https://fonts.googleapis.com/icon?family=Material+Icons|Material+Icons+Outlined" rel="stylesheet">
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>🎥 RTSP视频流马赛克处理器 - FLV流模式</h1>
            <p>硬件H.264编码 • 实时视频流 • 低延迟</p>
        </div>
        
        <div class="content">
            <div class="left-panel">
                <div class="video-section">
                    <div class="video-title">
                        <span class="material-icons-outlined">videocam</span>
                        <span>视频预览 (FLV流)</span>
                    </div>
                    <div class="video-container" id="videoContainer">
                        <div id="loading" class="loading-overlay" style="display: none;">
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
                        <video id="videoElement" controls autoplay playsinline></video>
                    </div>
                    
                    <div class="connection-controls">
                        <input type="text" 
                               id="rtspUrl" 
                               class="rtsp-input" 
                               placeholder="rtsp://username:password@ip:port/path"
                               value="rtsp://192.168.1.100:554/stream">
                        <button id="startBtn" class="btn btn-start" onclick="startStream()">
                            <span class="material-icons">play_arrow</span>
                            <span>开始播放</span>
                        </button>
                        <button id="stopBtn" class="btn btn-stop" onclick="stopStream()">
                            <span class="material-icons">stop</span>
                            <span>停止播放</span>
                        </button>
                    </div>
                    
                    <div class="mosaic-controls">
                        <div class="toggle-container">
                            <span class="toggle-label">
                                <span class="material-icons-outlined">blur_on</span>
                                马赛克效果
                            </span>
                            <label class="toggle-switch">
                                <input type="checkbox" id="mosaicEnabled" checked>
                                <span class="toggle-slider"></span>
                            </label>
                            <span id="mosaicStatus" style="font-size: 14px; color: #28a745;">
                                <span class="material-icons-outlined" style="font-size: 18px;">check_circle</span>
                                已启用
                            </span>
                        </div>
                    </div>
                    
                    <div class="compatibility-notice">
                        <strong>注意：</strong> 本系统使用FLV流技术，需要现代浏览器支持。如果无法播放，请检查浏览器是否支持HTML5 Video和flv.js。推荐使用Google浏览器。
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
                

            </div>
            
            <!-- 右侧性能统计看板 -->
            <div class="right-panel">
                <div class="perf-board">
                    <div class="perf-board-title">
                        <span class="material-icons">speed</span>
                        <span>性能统计</span>
                    </div>
                    
                    <div class="perf-flow">
                        <div class="perf-node">
                            <div class="perf-label">
                                <span class="material-icons-outlined" style="color: #4b6cb7;">download</span>
                                解码 (Decode)
                            </div>
                            <div class="perf-value" id="valDecode">-- ms</div>
                        </div>
                        
                        <div class="perf-arrow">
                            <span class="material-icons">arrow_downward</span>
                        </div>
                        
                        <div class="perf-node">
                            <div class="perf-label">
                                <span class="material-icons-outlined" style="color: #4b6cb7;">search</span>
                                检测 (Detect)
                            </div>
                            <div class="perf-value" id="valDetect">-- ms</div>
                        </div>
                        
                        <div class="perf-arrow">
                            <span class="material-icons">arrow_downward</span>
                        </div>
                        
                        <div class="perf-node">
                            <div class="perf-label">
                                <span class="material-icons-outlined" style="color: #4b6cb7;">blur_on</span>
                                马赛克 (Mosaic)
                            </div>
                            <div class="perf-value" id="valMosaic">-- ms</div>
                        </div>
                        
                        <div class="perf-arrow">
                            <span class="material-icons">arrow_downward</span>
                        </div>
                        
                        <div class="perf-node">
                            <div class="perf-label">
                                <span class="material-icons-outlined" style="color: #4b6cb7;">code</span>
                                编码 (Encode)
                            </div>
                            <div class="perf-value" id="valEncode">-- ms</div>
                        </div>
                        
                        <div class="perf-arrow">
                            <span class="material-icons">keyboard_double_arrow_down</span>
                        </div>
                        
                        <div class="perf-node highlight-red">
                            <div class="perf-label" style="color: #dc3545;">
                                <span class="material-icons-outlined" style="color: #dc3545;">timer</span>
                                总计 (Total)
                            </div>
                            <div class="perf-value" id="valTotal">-- ms</div>
                        </div>
                        
                        <div class="perf-node highlight-green" style="margin-top: 10px;">
                            <div class="perf-label" style="color: #28a745;">
                                <span class="material-icons-outlined" style="color: #28a745;">speed</span>
                                帧率 (FPS)
                            </div>
                            <div class="perf-value" id="valFps">-- fps</div>
                        </div>
                        
                        <div class="perf-node" style="margin-top: 10px; border-left-color: #6f42c1;">
                            <div class="perf-label" style="color: #6f42c1;">
                                <span class="material-icons-outlined" style="color: #6f42c1;">wifi</span>
                                流延迟
                            </div>
                            <div class="perf-value" id="valLatency">-- ms</div>
                        </div>
                    </div>
                </div>
            </div>
        </div>
    </div>
    
    <script>
        let flvPlayer = null;
        let isStreaming = false;
        let currentStreamUrl = '';
        let statsInterval = null;
        let flvPort = 38081; // FLV流端口，将从服务器动态获取
        let streamLatency = 0;
        let lastStatsUpdate = Date.now();
        
        // 从服务器获取FLV端口配置
        async function fetchFlvPort() {
            try {
                const response = await fetch('/flv_status');
                if (response.ok) {
                    const data = await response.json();
                    if (data.port) {
                        flvPort = data.port;
                        console.log('FLV port from server:', flvPort);
                    }
                }
            } catch (error) {
                console.warn('Failed to fetch FLV port, using default:', flvPort, error);
            }
        }
        
        // 更新状态显示
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
        
        // 显示/隐藏加载动画
        function showLoading(show) {
            const loading = document.getElementById('loading');
            const videoElement = document.getElementById('videoElement');
            const videoPlaceholder = document.getElementById('videoPlaceholder');
            
            if (show) {
                loading.style.display = 'flex';
                videoElement.style.display = 'none';
                videoPlaceholder.style.display = 'none';
            } else {
                loading.style.display = 'none';
                videoElement.style.display = 'block';
                videoPlaceholder.style.display = 'none';
            }
        }
        
        // 检查flv.js支持性
        function checkFLVSupport() {
            if (typeof flvjs === 'undefined') {
                alert('错误：无法加载flv.js库，请检查网络连接或使用支持媒体源的浏览器。');
                return false;
            }
            
            if (!flvjs.isSupported()) {
                alert('错误：您的浏览器不支持FLV播放。请使用Chrome、Firefox或Edge等现代浏览器。');
                return false;
            }
            
            return true;
        }
        
        // 开始流
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
            
            if (!checkFLVSupport()) {
                return;
            }
            
            showLoading(true);
            updateStatus('正在连接RTSP流...', true);
            
            // 先启动RTSP流
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
                    
                    // 延迟一下再启动FLV播放器，确保流服务器已经准备好
                    setTimeout(() => {
                        startFLVPlayer();
                        isStreaming = true;
                        updateStatus('正在播放', true);
                        showLoading(false);
                        
                        // 开始更新性能统计
                        startStatsUpdate();
                        
                        // 应用当前设置
                        applyMosaicSettings();
                    }, 1000);
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
        
        // 启动FLV播放器
        function startFLVPlayer() {
            if (flvPlayer) {
                flvPlayer.destroy();
                flvPlayer = null;
            }
            
            const videoElement = document.getElementById('videoElement');
            const hostname = window.location.hostname;
            
            // 构建FLV流URL
            const flvUrl = `http://${hostname}:${flvPort}/live`;
            
            console.log('Connecting to FLV stream:', flvUrl);
            
            flvPlayer = flvjs.createPlayer({
                type: 'flv',
                url: flvUrl,
                isLive: true,
                hasAudio: false,
                enableWorker: true,
                enableStashBuffer: false, // 禁用缓冲以减少延迟
                stashInitialSize: 128,    // 较小的初始缓冲
                lazyLoad: false,
                lazyLoadMaxDuration: 3 * 60,
                deferLoadAfterSourceOpen: false
            }, {
                enableLogger: false,
                lazyLoad: false
            });
            
            flvPlayer.attachMediaElement(videoElement);
            flvPlayer.load();
            
            // 播放器事件监听
            flvPlayer.on(flvjs.Events.MEDIA_INFO, function(info) {
                console.log('Media info:', info);
                updateStatus('播放中', true);
            });
            
            flvPlayer.on(flvjs.Events.METADATA_ARRIVED, function(metadata) {
                console.log('Metadata arrived:', metadata);
            });
            
            flvPlayer.on(flvjs.Events.SCRIPTDATA_ARRIVED, function(data) {
                console.log('Script data arrived:', data);
            });
            
            flvPlayer.on(flvjs.Events.STATISTICS_INFO, function(info) {
                // 计算流延迟
                if (info.playerType === 'FlvPlayer') {
                    streamLatency = info.latency || 0;
                    updateLatencyDisplay();
                }
            });
            
            flvPlayer.on(flvjs.Events.ERROR, function(errorType, errorDetail, errorInfo) {
                console.error('FLV Player Error:', errorType, errorDetail, errorInfo);
                
                if (errorType === 'NetworkError') {
                    updateStatus('网络错误，正在重连...', false);
                    setTimeout(() => {
                        if (isStreaming) {
                            startFLVPlayer();
                        }
                    }, 2000);
                } else if (errorType === 'MediaError') {
                    alert('媒体播放错误，请检查流服务器状态');
                }
            });
            
            flvPlayer.on(flvjs.Events.LOADING_COMPLETE, function() {
                console.log('FLV stream loading complete');
            });
            
            // 开始播放
            flvPlayer.play().catch(e => {
                console.error('Play failed:', e);
                // 如果自动播放失败，显示播放按钮
                videoElement.controls = true;
            });
        }
        
        // 停止流
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
                        stopFLVPlayer();
                        resetDisplay();
                    }
                })
                .catch(error => {
                    console.error('停止流时出错:', error);
                    stopFLVPlayer();
                    resetDisplay();
                });
            }
        }
        
        // 停止FLV播放器
        function stopFLVPlayer() {
            if (flvPlayer) {
                flvPlayer.pause();
                flvPlayer.unload();
                flvPlayer.detachMediaElement();
                flvPlayer.destroy();
                flvPlayer = null;
            }
            
            if (statsInterval) {
                clearInterval(statsInterval);
                statsInterval = null;
            }
        }
        
        // 重置显示
        function resetDisplay() {
            isStreaming = false;
            currentStreamUrl = '';
            
            const videoElement = document.getElementById('videoElement');
            const videoPlaceholder = document.getElementById('videoPlaceholder');
            
            videoElement.style.display = 'none';
            videoElement.src = '';
            videoPlaceholder.style.display = 'flex';
            
            updateStatus('已停止播放', false);
            resetPerformanceDisplay();
        }
        
        // 重置性能显示
        function resetPerformanceDisplay() {
            document.getElementById('valFps').textContent = '-- fps';
            document.getElementById('valDecode').textContent = '-- ms';
            document.getElementById('valDetect').textContent = '-- ms';
            document.getElementById('valMosaic').textContent = '-- ms';
            document.getElementById('valEncode').textContent = '-- ms';
            document.getElementById('valTotal').textContent = '-- ms';
            document.getElementById('valLatency').textContent = '-- ms';
        }
        
        // 更新延迟显示
        function updateLatencyDisplay() {
            document.getElementById('valLatency').textContent = Math.round(streamLatency) + ' ms';
        }
        
        // 开始更新性能统计
        function startStatsUpdate() {
            if (statsInterval) {
                clearInterval(statsInterval);
            }
            
            statsInterval = setInterval(() => {
                if (isStreaming) {
                    fetch('/get_perf_stats')
                        .then(response => {
                            if (!response.ok) throw new Error('Network response was not ok');
                            return response.json();
                        })
                        .then(data => {
                            if (data.fps !== undefined) {
                                document.getElementById('valFps').textContent = data.fps + ' fps';
                            }
                            if (data.decode_ms !== undefined) {
                                document.getElementById('valDecode').textContent = data.decode_ms.toFixed(1) + ' ms';
                            }
                            if (data.detect_ms !== undefined) {
                                document.getElementById('valDetect').textContent = data.detect_ms.toFixed(1) + ' ms';
                            }
                            if (data.mosaic_ms !== undefined) {
                                document.getElementById('valMosaic').textContent = data.mosaic_ms.toFixed(1) + ' ms';
                            }
                            if (data.encode_ms !== undefined) {
                                document.getElementById('valEncode').textContent = data.encode_ms.toFixed(1) + ' ms';
                            }
                            if (data.total_ms !== undefined) {
                                document.getElementById('valTotal').textContent = data.total_ms.toFixed(1) + ' ms';
                            }
                            
                            lastStatsUpdate = Date.now();
                        })
                        .catch(error => {
                            console.error('Failed to fetch performance stats:', error);
                            // 如果长时间没有更新，检查连接状态
                            if (Date.now() - lastStatsUpdate > 5000) {
                                console.warn('Performance stats update timeout, checking connection...');
                            }
                        });
                }
            }, 1000); // 每秒更新一次
        }
        
        // 应用马赛克设置
        function applyMosaicSettings() {
            const enabled = document.getElementById('mosaicEnabled').checked;
            
            fetch('/update_mosaic_settings', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/x-www-form-urlencoded',
                },
                body: 'enabled=' + enabled
            })
            .then(response => response.json())
            .then(data => {
                if (data.success) {
                    const statusElement = document.getElementById('mosaicStatus');
                    if (enabled) {
                        statusElement.innerHTML = '<span class="material-icons-outlined" style="font-size: 18px;">check_circle</span> 已启用';
                        statusElement.style.color = '#28a745';
                    } else {
                        statusElement.innerHTML = '<span class="material-icons-outlined" style="font-size: 18px;">cancel</span> 已禁用';
                        statusElement.style.color = '#dc3545';
                    }
                }
            })
            .catch(error => {
                console.error('Failed to update mosaic settings:', error);
            });
        }
        

        
        // 输入框回车事件
        document.getElementById('rtspUrl').addEventListener('keypress', function(e) {
            if (e.key === 'Enter') {
                startStream();
            }
        });
        
        // 马赛克开关事件
        document.getElementById('mosaicEnabled').addEventListener('change', applyMosaicSettings);
        
        // 页面加载时检查状态
        window.onload = async function() {
            // 先获取FLV端口配置
            await fetchFlvPort();
            
            // 检查流状态
            fetch('/stream_status')
                .then(response => response.json())
                .then(data => {
                    if (data.is_streaming && data.current_url) {
                        document.getElementById('rtspUrl').value = data.current_url;
                        currentStreamUrl = data.current_url;
                        isStreaming = true;
                        
                        // 启动FLV播放器
                        setTimeout(() => {
                            startFLVPlayer();
                            updateStatus('正在播放', true);
                            startStatsUpdate();
                        }, 500);
                    }
                })
                .catch(error => {
                    console.error('检查状态时出错:', error);
                });
            
            // 获取当前设置
            fetch('/get_mosaic_settings')
                .then(response => response.json())
                .then(settings => {
                    if (settings && settings.enabled !== undefined) {
                        document.getElementById('mosaicEnabled').checked = settings.enabled;
                        applyMosaicSettings();
                    }
                });
        };
        
        // 页面卸载前清理
        window.addEventListener('beforeunload', function() {
            if (flvPlayer) {
                flvPlayer.destroy();
            }
        });
    </script>
</body>
</html>
)====";

// 空白JPEG图像（用于无视频时显示）- 保持兼容性
const uint8_t BLANK_JPEG[] = {
    0xff, 0xd8, 0xff, 0xe0, 0x00, 0x10, 0x4a, 0x46, 0x49, 0x46, 0x00, 0x01,
    0x01, 0x01, 0x00, 0x48, 0x00, 0x48, 0x00, 0x00, 0xff, 0xdb, 0x00, 0x43,
    0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0xff, 0xc0, 0x00, 0x0b, 0x08, 0x00, 0x01,
    0x00, 0x01, 0x01, 0x01, 0x11, 0x00, 0xff, 0xc4, 0x00, 0x14, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x03, 0xff, 0xc4, 0x00, 0x14, 0x10, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xff, 0xda, 0x00, 0x08, 0x01, 0x01, 0x00, 0x00, 0x3f, 0x00,
    0xff, 0xd9
};