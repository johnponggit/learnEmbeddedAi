#pragma once

#include <httplib.h>
#include <memory>
#include <string>
#include <atomic>

class IHttFlvStreamHandler {
public:
    virtual int  register_flv_client() = 0;
    virtual bool get_flv_stream_data(std::vector<uint8_t>& data, int client_id) = 0;
    virtual void unregister_flv_client(int client_id) = 0;
    virtual bool get_sps_pps_data(std::vector<uint8_t>& sps, std::vector<uint8_t>& pps) = 0;

    virtual bool get_encoder_config(int& width, int& height, int& fps) = 0;
    
    virtual bool validate_h264_data(const std::vector<uint8_t>& data) {
        if (data.empty()) return false;
        
        // 检查是否有有效的start code
        for (size_t i = 0; i < std::min(data.size(), size_t(100)); i++) {
            if (i + 3 >= data.size()) break;
            
            if (data[i] == 0x00 && data[i+1] == 0x00) {
                if (data[i+2] == 0x01) return true;
                if (i + 4 < data.size() && data[i+2] == 0x00 && data[i+3] == 0x01) return true;
            }
        }
        
        return false;
    }
};

class HttpFlvStreamer {
public:
    HttpFlvStreamer(int port = 8080);
    ~HttpFlvStreamer();
    
    bool Start(std::shared_ptr<IHttFlvStreamHandler> handler);
    void Stop();
    
    int GetPort() const { return port_; }
    bool IsRunning() const { return running_; }
    
private:
    void setup_routes();
    
private:
    int port_;
    std::atomic<bool> running_{false};
    std::unique_ptr<httplib::Server> server_;
    std::shared_ptr<IHttFlvStreamHandler> handler_;
    std::thread server_thread_;
};