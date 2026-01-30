// main.cpp

#include "decoderManager.h"
#include "html_page.h"
#include "httpFlvStreamer.h"  // 新增头文件
#include <memory>

using json = nlohmann::json;

// 创建通用 JSON 响应
std::string create_json_response(bool success, const std::string& error = "") {
    json response;
    response["success"] = success;
    
    if (!error.empty()) {
        response["error"] = error;
    }
    
    return response.dump();
}

// 创建状态 JSON
std::string create_status_json(bool is_streaming, const std::string& current_url = "") {
    json response;
    response["is_streaming"] = is_streaming;
    
    if (!current_url.empty()) {
        response["current_url"] = current_url;
    }
    
    return response.dump();
}

void print_usage(int web_port, int flv_port) {
    LOG_INFO("RTSP视频流马赛克处理器服务器 - FLV流模式");
    LOG_INFO("===============================================");
    LOG_INFO("Web服务器端口: " << web_port);
    LOG_INFO("FLV流服务器端口: " << flv_port);
    LOG_INFO("在浏览器中打开: http://localhost:" << web_port);
    LOG_INFO("FLV流地址: http://localhost:" << flv_port << "/live");
    LOG_INFO("");
    LOG_INFO("可用端点:");
    LOG_INFO("  GET  /                      - 网页界面");
    LOG_INFO("  POST /start_stream          - 启动RTSP流");
    LOG_INFO("  POST /stop_stream           - 停止RTSP流");
    LOG_INFO("  GET  /stream_status         - 获取流状态");
    LOG_INFO("  POST /update_mosaic_settings - 更新马赛克设置");
    LOG_INFO("  GET  /get_mosaic_settings    - 获取当前马赛克设置");
    LOG_INFO("  GET  /get_perf_stats         - 获取性能统计数据");
    LOG_INFO("");
    LOG_INFO("FLV端点:");
    LOG_INFO("  GET  /live                  - FLV视频流");
    LOG_INFO("");
    LOG_INFO("功能说明:");
    LOG_INFO("  1. 硬件H.264编码，低延迟视频流");
    LOG_INFO("  2. 自动检测画面人体位置打上马赛克");
    LOG_INFO("  3. 支持多客户端同时观看");
    LOG_INFO("  4. 实时显示各环节性能统计");
    LOG_INFO("  5. 支持启用/禁用马赛克效果和人体检测");
}

int httpServer(int web_port, int flv_port) {
    MosaicProcessor::MosaicSettings global_mosaic_settings;

    // 创建解码管理器
    auto decoder_manager = std::make_shared<DecoderManager>();
    
    // 创建FLV流服务器
    auto flv_streamer = std::make_unique<HttpFlvStreamer>(flv_port);
    if (!flv_streamer->Start(decoder_manager)) {
        LOG_ERROR("无法启动FLV流服务器，端口: " << flv_port);
        return 1;
    }
    
    LOG_INFO("FLV流服务器启动成功，端口: " << flv_port);

    // 创建Web服务器
    httplib::Server server;
    
    // 主页
    server.Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(HTML_PAGE, "text/html");
    });
    
    // 保持兼容性 - JPEG单帧接口（可选）
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
            res.set_content(create_json_response(false, "RTSP URL不能为空"), "application/json");
            return;
        }
        
        LOG_INFO("收到启动流请求: " << rtsp_url);
        
        if (decoder_manager->start_stream(rtsp_url)) {
            res.set_content(create_json_response(true), "application/json");
            LOG_INFO("RTSP流启动成功");
        } else {
            res.set_content(create_json_response(false, "无法连接到RTSP流"), "application/json");
            LOG_ERROR("RTSP流启动失败");
        }
    });
    
    // 停止流
    server.Post("/stop_stream", [&decoder_manager](const httplib::Request&, httplib::Response& res) {
        LOG_INFO("收到停止流请求");
        decoder_manager->stop_all();
        res.set_content(create_json_response(true), "application/json");
        LOG_INFO("RTSP流已停止");
    });
    
    // 获取流状态
    server.Get("/stream_status", [&decoder_manager](const httplib::Request&, httplib::Response& res) {
        res.set_content(create_status_json(
            decoder_manager->is_streaming(),
            decoder_manager->get_current_url()
        ), "application/json");
    });
    
    // 更新马赛克设置 
    server.Post("/update_mosaic_settings", [&decoder_manager, &global_mosaic_settings](const httplib::Request& req, httplib::Response& res) {
        std::string enabled_str = req.get_param_value("enabled");
        
        if (enabled_str.empty()) {
            res.set_content(create_json_response(false, "enabled参数不能为空"), "application/json");
            return;
        }
        
        try {
            bool enabled = (enabled_str == "true" || enabled_str == "1");
            
            LOG_INFO("更新马赛克设置: enabled=" << enabled);
            
            // 更新全局设置
            global_mosaic_settings = decoder_manager->getMosaicSettings();
            global_mosaic_settings.enabled = enabled;
            
            // 调用decoder_manager更新设置
            if (decoder_manager->update_mosaic_settings(
                global_mosaic_settings.x, global_mosaic_settings.y,
                global_mosaic_settings.width, global_mosaic_settings.height,
                global_mosaic_settings.block_size, global_mosaic_settings.border_size,
                global_mosaic_settings.enabled)) {
                res.set_content(create_json_response(true), "application/json");
                LOG_INFO("马赛克设置更新成功");
            } else {
                res.set_content(create_json_response(false, "更新马赛克设置失败"), "application/json");
                LOG_ERROR("马赛克设置更新失败");
            }
        } catch (const std::exception& e) {
            res.set_content(create_json_response(false, "参数格式错误"), "application/json");
            LOG_ERROR("更新马赛克设置参数错误: " << e.what());
        }
    });
    
    // 更新检测设置（新增接口）
    server.Post("/update_detect_settings", [&decoder_manager](const httplib::Request& req, httplib::Response& res) {
        std::string enabled_str = req.get_param_value("enabled");
        
        if (enabled_str.empty()) {
            res.set_content(create_json_response(false, "enabled参数不能为空"), "application/json");
            return;
        }
        
        try {
            bool enabled = (enabled_str == "true" || enabled_str == "1");
            
            LOG_INFO("更新检测设置: enabled=" << enabled);
            
            // 这里可以实现具体的检测设置更新逻辑
            // 例如，通过设置检测框为0来禁用检测
            MosaicProcessor::MosaicSettings settings = decoder_manager->getMosaicSettings();
            
            if (!enabled) {
                // 禁用检测时，将马赛克区域设为0
                settings.x = 0;
                settings.y = 0;
                settings.width = 0;
                settings.height = 0;
            }
            
            if (decoder_manager->update_mosaic_settings(
                settings.x, settings.y,
                settings.width, settings.height,
                settings.block_size, settings.border_size,
                settings.enabled)) {
                res.set_content(create_json_response(true), "application/json");
                LOG_INFO("检测设置更新成功");
            } else {
                res.set_content(create_json_response(false, "更新检测设置失败"), "application/json");
                LOG_ERROR("检测设置更新失败");
            }
        } catch (const std::exception& e) {
            res.set_content(create_json_response(false, "参数格式错误"), "application/json");
            LOG_ERROR("更新检测设置参数错误: " << e.what());
        }
    });
    
    // 获取当前马赛克设置  
    server.Get("/get_mosaic_settings", [&decoder_manager, &global_mosaic_settings](const httplib::Request&, httplib::Response& res) {
        // 从decoder_manager获取最新设置
        global_mosaic_settings = decoder_manager->getMosaicSettings();
        
        json j;
        j["x"] = global_mosaic_settings.x;
        j["y"] = global_mosaic_settings.y;
        j["width"] = global_mosaic_settings.width;
        j["height"] = global_mosaic_settings.height;
        j["block_size"] = global_mosaic_settings.block_size;
        j["border_size"] = global_mosaic_settings.border_size;
        j["enabled"] = global_mosaic_settings.enabled;
        j["shape"] = "rectangle";
        
        res.set_content(j.dump(), "application/json");
    });
    
    // 获取性能统计
    server.Get("/get_perf_stats", [&decoder_manager](const httplib::Request&, httplib::Response& res) {
        res.set_content(decoder_manager->get_perf_stats_json(), "application/json");
    });
    
    // 获取FLV流状态（新增接口）
    server.Get("/flv_status", [&flv_streamer](const httplib::Request&, httplib::Response& res) {
        json j;
        j["running"] = flv_streamer->IsRunning();
        j["port"] = flv_streamer->GetPort();
        res.set_content(j.dump(), "application/json");
    });
    
    // 设置HTTP服务器选项
    server.set_keep_alive_max_count(100);
    server.set_read_timeout(10, 0);
    server.set_write_timeout(10, 0);
    server.set_idle_interval(0, 100000); // 100ms
    server.set_payload_max_length(1024 * 1024 * 10); // 10MB
    
    // 启动服务器
    LOG_INFO("Web服务器启动在端口 " << web_port);
    LOG_INFO("按 Ctrl+C 停止服务器");
    
    if (!server.listen("0.0.0.0", web_port)) {
        LOG_ERROR("无法在端口 " << web_port << " 启动Web服务器");
        flv_streamer->Stop();
        return 1;
    }
    
    // 服务器停止时清理FLV流服务器
    flv_streamer->Stop();
    
    return 0;
}

int main(int argc, char* argv[]) {
    // 默认端口配置
    int web_port = 38080;
    int flv_port = 38081;
    
    // 解析命令行参数
    if (argc >= 2) {
        web_port = std::stoi(argv[1]);
    }
    if (argc >= 3) {
        flv_port = std::stoi(argv[2]);
    }
    
    // 显示使用说明
    print_usage(web_port, flv_port);
    
    // 运行服务器
    return httpServer(web_port, flv_port);       
}