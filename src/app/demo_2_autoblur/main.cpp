
#include "decoderManager.h"
#include "html_page.h"

using json = nlohmann::json;

// 创建通用 JSON 响应
std::string create_json_response(bool success, const std::string& error = "") {
    json response;
    response["success"] = success;
    
    if (!error.empty()) {
        response["error"] = error;
    }
    
    return response.dump(); // 返回 JSON 字符串
}

// 创建状态 JSON
std::string create_status_json(bool is_streaming, const std::string& current_url = "") {
    json response;
    response["is_streaming"] = is_streaming;
    
    if (!current_url.empty()) {
        response["current_url"] = current_url;
    }
    
    return response.dump(); // 返回 JSON 字符串
}



void print_usage(int port) {
    LOG_INFO("RTSP视频流模糊处理器服务器");
    LOG_INFO("============================================");
    LOG_INFO("服务器启动在端口 " << port);
    LOG_INFO("在浏览器中打开 http://localhost:" << port);
    LOG_INFO("可用端点:");
    LOG_INFO("  GET  /                      - 网页界面");
    LOG_INFO("  GET  /video_frame           - 获取处理后的视频帧");
    LOG_INFO("  POST /start_stream          - 启动RTSP流");
    LOG_INFO("  GET  /stream_status         - 获取流状态");
    LOG_INFO("  POST /update_blur_settings  - 更新模糊设置");
    LOG_INFO("  GET  /get_blur_settings     - 获取当前模糊设置");
    LOG_INFO("功能说明:");
    LOG_INFO("  1. 只使用一路RTSP流");
    LOG_INFO("  2. 支持圆形和矩形两种模糊区域");
    LOG_INFO("  3. 支持鼠标点击视频画面定位模糊区域");
    LOG_INFO("  4. 页面刷新时会自动应用当前设置");
    LOG_INFO("  5. 可实时调整模糊区域的位置、大小和模糊半径");
    LOG_INFO("  6. 可启用或禁用模糊效果");
    LOG_INFO("  7. 使用高斯模糊算法实现平滑的模糊效果");
}

BlurProcessor::BlurSettings global_blur_settings;

int httpServer(int port) {
    // 创建HTTP服务器
    httplib::Server server;
    DecoderManager::Ptr decoder_manager = std::make_shared<DecoderManager>();

    // 主页
    server.Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(HTML_PAGE, "text/html");
    });
    
    // 获取处理后的视频帧
    server.Get("/video_frame", [&decoder_manager](const httplib::Request& req, httplib::Response& res) {
        std::vector<uint8_t> frame;
        if (decoder_manager->get_processed_frame(frame)) {
            res.set_content(reinterpret_cast<char*>(frame.data()), frame.size(), "image/jpeg");
        } else {
            // 返回空白图像
            res.set_content(reinterpret_cast<const char*>(BLANK_JPEG), sizeof(BLANK_JPEG), "image/jpeg");
        }
    });
    
    // 启动流
    server.Post("/start_stream", [&decoder_manager](const httplib::Request& req, httplib::Response& res) {
        std::string rtsp_url = req.get_param_value("rtsp_url");
        
        if (rtsp_url.empty()) {
            res.set_content(create_json_response(false, "RTSP URL is required"), "application/json");
            return;
        }
        
        if (decoder_manager->start_stream(rtsp_url)) {
            res.set_content(create_json_response(true), "application/json");
        } else {
            res.set_content(create_json_response(false, "Failed to connect to RTSP stream"), "application/json");
        }
    });
    
    // 停止流
    server.Post("/stop_stream", [&decoder_manager](const httplib::Request&, httplib::Response& res) {
        decoder_manager->stop_all();
        res.set_content(create_json_response(true), "application/json");
    });
    
    // 获取流状态
    server.Get("/stream_status", [&decoder_manager](const httplib::Request&, httplib::Response& res) {
        res.set_content(create_status_json(
            decoder_manager->is_streaming(),
            decoder_manager->get_current_url()
        ), "application/json");
    });
    
    // 更新模糊设置 
    server.Post("/update_blur_settings", [&decoder_manager, &global_blur_settings](const httplib::Request& req, httplib::Response& res) {
        std::string enabled_str = req.get_param_value("enabled");
        
        if (enabled_str.empty()) {
            res.set_content(create_json_response(false, "Enabled parameter is required"), "application/json");
            return;
        }
        
        try {
            bool enabled = (enabled_str == "true");
            
            // 更新全局设置
            global_blur_settings = decoder_manager->getBlurSettings();
            global_blur_settings.enabled = enabled;
            
            // 调用decoder_manager更新设置
            if (decoder_manager->update_blur_settings(
                global_blur_settings.x, global_blur_settings.y,
                global_blur_settings.width, global_blur_settings.height,
                global_blur_settings.blur_radius, global_blur_settings.border_size,
                enabled, global_blur_settings.shape)) {
                res.set_content(create_json_response(true), "application/json");
            } else {
                res.set_content(create_json_response(false, "Failed to update blur settings"), "application/json");
            }
        } catch (const std::exception& e) {
            res.set_content(create_json_response(false, "Invalid parameter format"), "application/json");
        }
    });
    
    // 获取当前模糊设置  
    server.Get("/get_blur_settings", [&decoder_manager, &global_blur_settings](const httplib::Request&, httplib::Response& res) {
        json j;
        j["x"] = global_blur_settings.x;
        j["y"] = global_blur_settings.y;
        j["width"] = global_blur_settings.width;
        j["height"] = global_blur_settings.height;
        j["blur_radius"] = global_blur_settings.blur_radius;
        j["border_size"] = global_blur_settings.border_size;
        j["enabled"] = global_blur_settings.enabled;
        j["shape"] = global_blur_settings.shape;
        
        res.set_content(j.dump(), "application/json");
    });
    
    // 设置HTTP服务器选项
    server.set_keep_alive_max_count(100);
    server.set_read_timeout(10, 0); // 10秒读超时
    server.set_write_timeout(10, 0); // 10秒写超时
    
    // 启动服务器
    LOG_INFO("按 Ctrl+C 停止服务器");
    
    if (!server.listen("0.0.0.0", port)) {
        LOG_ERROR("无法在端口 " << port << " 启动服务器");
        return 1;
    }
    return 0;
}

int main(int argc, char* argv[]) {
    
    int port = 38080;
    
    if (argc >= 2) {
        port = std::stoi(argv[1]);
    }    
    
    print_usage(port);
    return httpServer(port);       
}