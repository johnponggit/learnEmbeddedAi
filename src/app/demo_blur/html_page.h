
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
                cursor: crosshair;
            }
            
            #video {
                width: 100%;
                height: 100%;
                object-fit: contain;
                display: block;
            }
            
            .click-indicator {
                position: absolute;
                width: 20px;
                height: 20px;
                border-radius: 50%;
                background: rgba(255, 107, 107, 0.8);
                border: 2px solid white;
                pointer-events: none;
                transform: translate(-50%, -50%);
                animation: pulse 1.5s ease-out;
                display: none;
                z-index: 10;
            }
            
            @keyframes pulse {
                0% { transform: translate(-50%, -50%) scale(1); opacity: 1; }
                70% { transform: translate(-50%, -50%) scale(2); opacity: 0.7; }
                100% { transform: translate(-50%, -50%) scale(2.5); opacity: 0; }
            }
            
            .click-hint {
                position: absolute;
                bottom: 15px;
                left: 0;
                right: 0;
                text-align: center;
                color: white;
                font-size: 14px;
                background: rgba(0, 0, 0, 0.6);
                padding: 8px 15px;
                border-radius: 20px;
                margin: 0 auto;
                width: fit-content;
                display: none;
                z-index: 5;
                pointer-events: none;
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
            
            .btn-apply {
                background: linear-gradient(135deg, #007bff 0%, #0056b3 100%);
                color: white;
            }
            
            .btn:hover {
                opacity: 0.9;
                transform: translateY(-2px);
                box-shadow: 0 5px 15px rgba(0, 0, 0, 0.1);
            }
            
            .blur-controls {
                background: linear-gradient(135deg, #f8f9fa 0%, #e9ecef 100%);
                padding: 25px;
                border-radius: 15px;
                border: 2px solid #4b6cb7;
                box-shadow: 0 5px 15px rgba(75, 108, 183, 0.1);
            }
            
            .blur-controls .panel-title {
                color: #4b6cb7;
            }
            
            .control-section {
                margin-bottom: 25px;
                padding-bottom: 15px;
                border-bottom: 1px solid #dee2e6;
            }
            
            .control-section:last-child {
                border-bottom: none;
                margin-bottom: 0;
                padding-bottom: 0;
            }
            
            .control-section-title {
                color: #495057;
                margin-bottom: 15px;
                font-size: 16px;
                font-weight: 600;
                display: flex;
                align-items: center;
                gap: 8px;
            }
            
            .slider-group {
                margin-bottom: 15px;
            }
            
            .slider-container {
                display: flex;
                align-items: center;
                gap: 15px;
            }
            
            .slider-container label {
                min-width: 80px;
                color: #495057;
                font-weight: 500;
                font-size: 14px;
                display: flex;
                align-items: center;
                gap: 5px;
            }
            
            input[type="range"] {
                flex: 1;
                height: 8px;
                -webkit-appearance: none;
                background: linear-gradient(90deg, #4b6cb7 0%, #e9ecef 100%);
                border-radius: 4px;
                outline: none;
            }
            
            input[type="range"]::-webkit-slider-thumb {
                -webkit-appearance: none;
                width: 22px;
                height: 22px;
                background: #4b6cb7;
                border-radius: 50%;
                cursor: pointer;
                border: 3px solid white;
                box-shadow: 0 2px 5px rgba(0, 0, 0, 0.2);
            }
            
            .slider-value {
                min-width: 40px;
                text-align: center;
                font-weight: 600;
                color: #4b6cb7;
                background: white;
                padding: 4px 8px;
                border-radius: 6px;
                border: 1px solid #dee2e6;
            }
            
            .shape-selector {
                display: flex;
                gap: 15px;
                margin: 15px 0;
            }
            
            .shape-option {
                flex: 1;
                padding: 10px;
                border: 2px solid #dee2e6;
                border-radius: 10px;
                text-align: center;
                cursor: pointer;
                transition: all 0.3s ease;
                background: white;
            }
            
            .shape-option:hover {
                border-color: #4b6cb7;
                background: #f8f9fa;
            }
            
            .shape-option.selected {
                border-color: #4b6cb7;
                background: #4b6cb7;
                color: white;
            }
            
            .shape-icon {
                font-size: 24px;
                margin-bottom: 5px;
                display: block;
            }
            
            .checkbox-group {
                display: flex;
                align-items: center;
                gap: 10px;
                margin: 10px 0;
            }
            
            .checkbox-group input[type="checkbox"] {
                width: 18px;
                height: 18px;
            }
            
            .checkbox-group label {
                color: #495057;
                font-weight: 500;
                cursor: pointer;
                display: flex;
                align-items: center;
                gap: 5px;
            }
            
            .preview-container {
                background: #2c3e50;
                border-radius: 10px;
                padding: 15px;
                margin-top: 20px;
                position: relative;
                overflow: hidden;
                height: 200px;
            }
            
            .preview-title {
                color: white;
                margin-bottom: 10px;
                font-size: 14px;
                text-align: center;
            }
            
            .preview-area {
                width: 100%;
                height: 150px;
                background: #34495e;
                border-radius: 8px;
                position: relative;
                overflow: hidden;
                border: 2px solid #4b6cb7;
            }
            
            .preview-main {
                position: absolute;
                top: 0;
                left: 0;
                right: 0;
                bottom: 0;
                background: linear-gradient(135deg, #3498db 0%, #2c3e50 100%);
            }
            
            .preview-blur {
                position: absolute;
                background: linear-gradient(135deg, #3498db 0%, #2c3e50 100%);
                border: 2px solid #ff6b6b;
                box-shadow: 0 0 0 1px rgba(0, 0, 0, 0.3);
                transition: all 0.3s ease;
                filter: blur(3px);
            }
            
            .preview-blur.circular {
                border-radius: 50%;
            }
            
            .preview-label {
                position: absolute;
                color: white;
                font-size: 10px;
                padding: 2px 4px;
                background: rgba(0, 0, 0, 0.7);
                border-radius: 3px;
                white-space: nowrap;
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
            
            .auto-apply-note {
                font-size: 12px;
                color: #28a745;
                padding: 10px;
                background: #e7f5ff;
                border-radius: 8px;
                text-align: center;
                margin-top: 10px;
                display: flex;
                align-items: center;
                justify-content: center;
                gap: 5px;
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
        <!-- Material Icons -->
        <link href="https://fonts.googleapis.com/icon?family=Material+Icons|Material+Icons+Outlined" rel="stylesheet">
    </head>
    <body>
        <div class="container">
            <div class="header">
                <h1>🎥 RTSP视频流模糊处理器</h1>
                <p>单路视频流 • 点击画面定位模糊区域 • 自动应用设置 • 实时预览</p>
            </div>
            
            <div class="content">
                <div class="main-content">
                    <div class="video-section">
                        <div class="video-title">
                            <span class="material-icons-outlined">videocam</span>
                            <span>视频预览画面</span>
                            <span style="font-size: 14px; color: #ff6b6b; margin-left: auto;">
                                <span class="material-icons-outlined" style="font-size: 16px;">touch_app</span>
                                点击画面定位模糊区域 (自动应用设置)
                            </span>
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
                            <div id="clickIndicator" class="click-indicator"></div>
                            <div id="clickHint" class="click-hint">点击画面设置模糊区域中心位置 (自动应用)</div>
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
                            <li><strong>点击视频画面</strong>来定位模糊区域的中心位置 (自动应用设置)</li>
                            <li>在右侧控制面板中调整模糊区域的大小和模糊程度</li>
                            <li>选择模糊形状：圆形或矩形</li>
                            <li>调整模糊半径来控制模糊的程度</li>
                            <li>可以启用或禁用模糊效果</li>
                            <li>所有设置修改后立即生效</li>
                            <li>右侧提供模糊区域的可视化预览</li>
                            <li>使用高斯模糊算法实现平滑的模糊效果</li>
                        </ul>
                    </div>
                </div>
                
                <div class="right-panel">
                    <div class="blur-controls">
                        <div class="panel-title">
                            <span class="material-icons">blur_on</span>
                            <span>模糊控制</span>
                        </div>
                        
                        <div class="preview-container">
                            <div class="preview-title">模糊区域预览</div>
                            <div class="preview-area">
                                <div class="preview-main"></div>
                                <div id="previewBlur" class="preview-blur"></div>
                                <div class="preview-label" style="top: 5px; left: 5px;">主画面</div>
                            </div>
                        </div>
                        
                        <div class="control-section">
                            <div class="control-section-title">
                                <span class="material-icons-outlined">crop_square</span>
                                <span>区域设置</span>
                            </div>
                            
                            <div class="shape-selector">
                                <div id="shapeCircle" class="shape-option selected" onclick="selectShape('circle')">
                                    <span class="shape-icon">●</span>
                                    <span>圆形</span>
                                </div>
                                <div id="shapeRectangle" class="shape-option" onclick="selectShape('rectangle')">
                                    <span class="shape-icon">■</span>
                                    <span>矩形</span>
                                </div>
                            </div>
                            
                            <div class="slider-group">
                                <div class="slider-container">
                                    <label>
                                        <span class="material-icons-outlined" style="font-size: 18px;">horizontal_distribute</span>
                                        <span>位置 X:</span>
                                    </label>
                                    <input type="range" id="blurX" min="0" max="600" value="100" step="10">
                                    <span id="blurXValue" class="slider-value">100</span>
                                </div>
                            </div>
                            
                            <div class="slider-group">
                                <div class="slider-container">
                                    <label>
                                        <span class="material-icons-outlined" style="font-size: 18px;">vertical_distribute</span>
                                        <span>位置 Y:</span>
                                    </label>
                                    <input type="range" id="blurY" min="0" max="450" value="100" step="10">
                                    <span id="blurYValue" class="slider-value">100</span>
                                </div>
                            </div>
                            
                            <div class="slider-group">
                                <div class="slider-container">
                                    <label>
                                        <span class="material-icons-outlined" style="font-size: 18px;">width_normal</span>
                                        <span>宽度:</span>
                                    </label>
                                    <input type="range" id="blurWidth" min="50" max="400" value="200" step="10">
                                    <span id="blurWidthValue" class="slider-value">200</span>
                                </div>
                            </div>
                            
                            <div class="slider-group">
                                <div class="slider-container">
                                    <label>
                                        <span class="material-icons-outlined" style="font-size: 18px;">height</span>
                                        <span>高度:</span>
                                    </label>
                                    <input type="range" id="blurHeight" min="50" max="300" value="150" step="10">
                                    <span id="blurHeightValue" class="slider-value">150</span>
                                </div>
                            </div>
                            
                            <div class="slider-group">
                                <div class="slider-container">
                                    <label>
                                        <span class="material-icons-outlined" style="font-size: 18px;">blur_on</span>
                                        <span>模糊半径:</span>
                                    </label>
                                    <input type="range" id="blurRadius" min="1" max="10" value="5" step="1">
                                    <span id="blurRadiusValue" class="slider-value">5</span>
                                </div>
                            </div>
                            
                            <div class="checkbox-group">
                                <input type="checkbox" id="blurEnabled" checked>
                                <label for="blurEnabled">
                                    <span class="material-icons-outlined" style="font-size: 18px;">visibility</span>
                                    <span>启用模糊</span>
                                </label>
                            </div>
                        </div>
                        
                        <div class="auto-apply-note">
                            <span class="material-icons-outlined" style="font-size: 14px;">autorenew</span>
                            <span>所有设置已启用自动应用</span>
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
                        
                        <div class="control-section" style="margin-top: 20px;">
                            <div class="control-section-title">
                                <span class="material-icons-outlined">speed</span>
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
            let currentShape = 'circle';
            let videoDisplayWidth = 0;
            let videoDisplayHeight = 0;
            let videoOffsetX = 0;
            let videoOffsetY = 0;
            let applyDebounceTimer = null; // 防抖定时器
            let pageJustLoaded = true; // 标记页面是否刚刚加载
            
            // 形状选择
            function selectShape(shape) {
                currentShape = shape;
                
                // 更新UI
                document.getElementById('shapeCircle').classList.remove('selected');
                document.getElementById('shapeRectangle').classList.remove('selected');
                document.getElementById('shape' + shape.charAt(0).toUpperCase() + shape.slice(1)).classList.add('selected');
                
                // 更新预览并自动应用
                updatePreviewAndApply();
            }
            
            // 初始化滑块事件 - 修改为自动应用
            const sliders = ['blurX', 'blurY', 'blurWidth', 'blurHeight', 'blurRadius'];
            sliders.forEach(sliderId => {
                const slider = document.getElementById(sliderId);
                const valueDisplay = document.getElementById(sliderId + 'Value');
                
                slider.addEventListener('input', function(e) {
                    valueDisplay.textContent = e.target.value;
                    updatePreview();
                    // 滑块拖动时实时更新预览，但不立即应用（使用防抖）
                    debounceApplySettings();
                });
                
                slider.addEventListener('change', function(e) {
                    valueDisplay.textContent = e.target.value;
                    updatePreview();
                    // 滑块释放时立即应用设置
                    applyBlurSettings();
                });
            });
            
            // 初始化复选框事件 - 修改为自动应用
            document.getElementById('blurEnabled').addEventListener('change', function() {
                updatePreview();
                // 复选框改变时立即应用
                debounceApplySettings(300);
            });
            
            // 防抖函数 - 防止频繁发送请求
            function debounceApplySettings(delay = 300) {
                if (applyDebounceTimer) {
                    clearTimeout(applyDebounceTimer);
                }
                applyDebounceTimer = setTimeout(() => {
                    applyBlurSettings();
                }, delay);
            }
            
            // 更新视频显示尺寸
            function updateVideoDisplaySize() {
                const video = document.getElementById('video');
                const videoContainer = document.getElementById('videoContainer');
                
                if (!video || !videoContainer) return;
                
                // 获取视频容器的尺寸
                const containerWidth = videoContainer.clientWidth;
                const containerHeight = videoContainer.clientHeight;
                
                // 获取视频的原始尺寸
                const videoWidth = video.videoWidth || 800;
                const videoHeight = video.videoHeight || 600;
                
                // 计算缩放比例
                const widthRatio = containerWidth / videoWidth;
                const heightRatio = containerHeight / videoHeight;
                const scale = Math.min(widthRatio, heightRatio);
                
                // 计算显示尺寸
                videoDisplayWidth = videoWidth * scale;
                videoDisplayHeight = videoHeight * scale;
                
                // 计算偏移量（居中显示）
                videoOffsetX = (containerWidth - videoDisplayWidth) / 2;
                videoOffsetY = (containerHeight - videoDisplayHeight) / 2;
            }
            
            // 处理视频点击事件 - 修改为自动应用
            function handleVideoClick(event) {
                if (!isStreaming) {
                    alert('请先开始播放视频流');
                    return;
                }
                
                const videoContainer = document.getElementById('videoContainer');
                const rect = videoContainer.getBoundingClientRect();
                
                // 计算点击位置相对于视频容器
                const clickX = event.clientX - rect.left;
                const clickY = event.clientY - rect.top;
                
                // 显示点击指示器
                showClickIndicator(clickX, clickY);
                
                // 计算点击位置在原始视频坐标（800x600）中的位置
                const originalX = Math.round((clickX - videoOffsetX) * (800 / videoDisplayWidth));
                const originalY = Math.round((clickY - videoOffsetY) * (600 / videoDisplayHeight));
                
                // 确保坐标在有效范围内
                const validX = Math.max(0, Math.min(800, originalX));
                const validY = Math.max(0, Math.min(600, originalY));
                
                // 根据形状计算新的位置
                const width = parseInt(document.getElementById('blurWidth').value);
                const height = parseInt(document.getElementById('blurHeight').value);
                
                let newX, newY;
                
                if (currentShape === 'circle') {
                    // 对于圆形，点击位置是圆心
                    // 左上角位置 = 圆心位置 - 半径
                    const radius = Math.min(width, height) / 2;
                    newX = validX - radius;
                    newY = validY - radius;
                } else {
                    // 对于矩形，点击位置是矩形中心
                    // 左上角位置 = 中心位置 - 宽度/2 和 高度/2
                    newX = validX - width / 2;
                    newY = validY - height / 2;
                }
                
                // 确保位置在有效范围内
                newX = Math.max(0, Math.min(800 - width, newX));
                newY = Math.max(0, Math.min(600 - height, newY));
                
                // 更新滑块值
                document.getElementById('blurX').value = Math.round(newX);
                document.getElementById('blurY').value = Math.round(newY);
                document.getElementById('blurXValue').textContent = Math.round(newX);
                document.getElementById('blurYValue').textContent = Math.round(newY);
                
                // 更新预览
                updatePreview();
                
                // 显示提示信息
                showClickMessage(`已设置模糊区域到 (${Math.round(newX)}, ${Math.round(newY)})，正在自动应用...`);
                
                // 自动应用设置（无需手动点击按钮）
                applyBlurSettings();
            }
            
            // 显示点击指示器
            function showClickIndicator(x, y) {
                const indicator = document.getElementById('clickIndicator');
                indicator.style.left = x + 'px';
                indicator.style.top = y + 'px';
                indicator.style.display = 'block';
                
                // 重置动画
                indicator.style.animation = 'none';
                setTimeout(() => {
                    indicator.style.animation = 'pulse 1.5s ease-out';
                }, 10);
                
                // 3秒后隐藏指示器
                setTimeout(() => {
                    indicator.style.display = 'none';
                }, 1500);
            }
            
            // 显示点击提示消息
            function showClickMessage(message) {
                const hint = document.getElementById('clickHint');
                hint.textContent = message;
                hint.style.display = 'block';
                
                // 2秒后隐藏提示
                setTimeout(() => {
                    hint.style.display = 'none';
                }, 2000);
            }
            
            // 更新预览并自动应用设置
            function updatePreviewAndApply() {
                updatePreview();
                // 添加一个小的延迟，确保UI已经更新
                setTimeout(applyBlurSettings, 100);
            }
            
            // 更新预览
            function updatePreview() {
                const blurX = parseInt(document.getElementById('blurX').value);
                const blurY = parseInt(document.getElementById('blurY').value);
                const blurWidth = parseInt(document.getElementById('blurWidth').value);
                const blurHeight = parseInt(document.getElementById('blurHeight').value);
                const blurRadius = parseInt(document.getElementById('blurRadius').value);
                const blurEnabled = document.getElementById('blurEnabled').checked;
                const shape = currentShape;
                
                const previewArea = document.querySelector('.preview-area');
                const previewBlur = document.getElementById('previewBlur');
                
                // 计算预览尺寸比例
                const containerWidth = previewArea.clientWidth;
                const containerHeight = previewArea.clientHeight;
                
                // 800x600 是实际视频尺寸，预览区域是等比例缩小
                const scaleX = containerWidth / 800;
                const scaleY = containerHeight / 600;
                
                // 更新模糊区域预览
                if (blurEnabled) {
                    const previewBlurX = blurX * scaleX;
                    const previewBlurY = blurY * scaleY;
                    const previewBlurWidth = blurWidth * scaleX;
                    const previewBlurHeight = blurHeight * scaleY;
                    
                    previewBlur.style.left = previewBlurX + 'px';
                    previewBlur.style.top = previewBlurY + 'px';
                    
                    if (shape === 'circle') {
                        // 圆形：使用宽度和高度中较小的作为直径
                        const diameter = Math.min(previewBlurWidth, previewBlurHeight);
                        previewBlur.style.width = diameter + 'px';
                        previewBlur.style.height = diameter + 'px';
                        previewBlur.style.borderRadius = '50%';
                        previewBlur.classList.add('circular');
                    } else {
                        // 矩形
                        previewBlur.style.width = previewBlurWidth + 'px';
                        previewBlur.style.height = previewBlurHeight + 'px';
                        previewBlur.style.borderRadius = '0';
                        previewBlur.classList.remove('circular');
                    }
                    
                    previewBlur.style.display = 'block';
                    
                    // 调整模糊效果预览
                    const blurAmount = blurRadius / 10;
                    previewBlur.style.filter = `blur(${blurAmount * 3}px)`;
                    
                    // 更新模糊区域标签
                    let blurLabel = document.getElementById('blurPreviewLabel');
                    if (!blurLabel) {
                        blurLabel = document.createElement('div');
                        blurLabel.id = 'blurPreviewLabel';
                        blurLabel.className = 'preview-label';
                        previewArea.appendChild(blurLabel);
                    }
                    blurLabel.textContent = `${shape === 'circle' ? '圆形' : '矩形'} ${blurWidth}×${blurHeight}`;
                    blurLabel.style.left = (previewBlurX + 5) + 'px';
                    blurLabel.style.top = (previewBlurY + 5) + 'px';
                    blurLabel.style.display = 'block';
                } else {
                    previewBlur.style.display = 'none';
                    const blurLabel = document.getElementById('blurPreviewLabel');
                    if (blurLabel) blurLabel.style.display = 'none';
                }
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
                        
                        // 显示点击提示
                        const hint = document.getElementById('clickHint');
                        hint.style.display = 'block';
                        
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
                
                // 每500ms更新视频显示尺寸
                setInterval(() => {
                    if (isStreaming) {
                        updateVideoDisplaySize();
                    }
                }, 500);
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
                    
                    // 更新视频显示尺寸
                    updateVideoDisplaySize();
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
                const hint = document.getElementById('clickHint');
                
                video.style.display = 'none';
                video.src = '';
                videoPlaceholder.style.display = 'flex';
                hint.style.display = 'none';
                
                updateStatus('已停止播放', false);
                updatePerformanceInfo(0, 0);
            }
            
            // 应用模糊设置 - 修改为显示状态提示
            function applyBlurSettings() {
                const blurX = parseInt(document.getElementById('blurX').value);
                const blurY = parseInt(document.getElementById('blurY').value);
                const blurWidth = parseInt(document.getElementById('blurWidth').value);
                const blurHeight = parseInt(document.getElementById('blurHeight').value);
                const blurRadius = parseInt(document.getElementById('blurRadius').value);
                const blurEnabled = document.getElementById('blurEnabled').checked;
                const borderSize = 2; // 固定边框大小
                const shape = currentShape;
                
                // 显示应用中的状态
                const autoApplyNote = document.querySelector('.auto-apply-note');
                const originalText = autoApplyNote.innerHTML;
                autoApplyNote.innerHTML = '<span class="material-icons-outlined" style="font-size: 14px;">sync</span> <span>正在应用设置...</span>';
                autoApplyNote.style.color = '#ff9800';
                
                fetch('/update_blur_settings', {
                    method: 'POST',
                    headers: {
                        'Content-Type': 'application/x-www-form-urlencoded',
                    },
                    body: `x=${blurX}&y=${blurY}&width=${blurWidth}&height=${blurHeight}&blur_radius=${blurRadius}&border_size=${borderSize}&enabled=${blurEnabled}&shape=${shape}`
                })
                .then(response => response.json())
                .then(data => {
                    if (data.success) {
                        // 显示成功提示
                        autoApplyNote.innerHTML = '<span class="material-icons-outlined" style="font-size: 14px;">check_circle</span> <span>设置已自动应用</span>';
                        autoApplyNote.style.color = '#28a745';
                        
                        setTimeout(() => {
                            autoApplyNote.innerHTML = '<span class="material-icons-outlined" style="font-size: 14px;">autorenew</span> <span>所有设置已启用自动应用</span>';
                            autoApplyNote.style.color = '#28a745';
                        }, 2000);
                    } else {
                        // 显示错误提示
                        autoApplyNote.innerHTML = '<span class="material-icons-outlined" style="font-size: 14px;">error</span> <span>应用失败: ' + data.error + '</span>';
                        autoApplyNote.style.color = '#f44336';
                        
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
                    
                    setTimeout(() => {
                        autoApplyNote.innerHTML = originalText;
                        autoApplyNote.style.color = '#28a745';
                    }, 3000);
                    
                    console.error('Error:', error);
                });
            }
            
            // 窗口大小变化时更新预览和视频尺寸
            window.addEventListener('resize', function() {
                updatePreview();
                updateVideoDisplaySize();
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
            
            // 视频点击事件
            document.getElementById('videoContainer').addEventListener('click', handleVideoClick);
            
            // 页面加载时检查当前状态
            window.onload = function() {
                fetch('/stream_status')
                    .then(response => response.json())
                    .then(data => {
                        if (data.is_streaming && data.current_url) {
                            document.getElementById('rtspUrl').value = data.current_url;
                            currentStreamUrl = data.current_url;
                            isStreaming = true;
                            
                            const video = document.getElementById('video');
                            const videoPlaceholder = document.getElementById('videoPlaceholder');
                            const hint = document.getElementById('clickHint');
                            
                            video.style.display = 'block';
                            videoPlaceholder.style.display = 'none';
                            hint.style.display = 'block';
                            
                            startVideoStream();
                            updateStatus('正在播放', true);
                        }
                        
                        // 获取模糊设置
                        fetch('/get_blur_settings')
                            .then(response => response.json())
                            .then(settings => {
                                if (settings) {
                                    document.getElementById('blurX').value = settings.x;
                                    document.getElementById('blurY').value = settings.y;
                                    document.getElementById('blurWidth').value = settings.width;
                                    document.getElementById('blurHeight').value = settings.height;
                                    document.getElementById('blurRadius').value = settings.blur_radius;
                                    document.getElementById('blurEnabled').checked = settings.enabled;
                                    
                                    document.getElementById('blurXValue').textContent = settings.x;
                                    document.getElementById('blurYValue').textContent = settings.y;
                                    document.getElementById('blurWidthValue').textContent = settings.width;
                                    document.getElementById('blurHeightValue').textContent = settings.height;
                                    document.getElementById('blurRadiusValue').textContent = settings.blur_radius;
                                    
                                    // 设置形状
                                    selectShape(settings.shape);
                                    
                                    updatePreview();
                                    
                                    // 页面加载后，如果有流在播放，自动应用设置
                                    if (isStreaming) {
                                        setTimeout(() => {
                                            applyBlurSettings();
                                            pageJustLoaded = false;
                                        }, 1000);
                                    }
                                }
                            });
                    })
                    .catch(error => {
                        console.error('检查状态时出错:', error);
                    });
                
                // 初始化预览
                updatePreview();
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