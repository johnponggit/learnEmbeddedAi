
#pragma once

#include <iostream>
#include <sstream>
#include <string>

namespace emai {
 
// 日志级别枚举
enum LogLevel {
    LOG_LEVEL_ERROR,
    LOG_LEVEL_WARN,
    LOG_LEVEL_INFO,
    LOG_LEVEL_DEBUG
};




// 日志类
class Logger {
public:
    Logger(LogLevel level, const char* file, int line) 
        : level_(level), file_(file), line_(line) {
        // 添加时间戳和日志级别前缀
        stream_ << "[" << getCurrentTime() << "] ";
        
        switch(level_) {
            case LOG_LEVEL_ERROR:
                stream_ << "[ERROR] ";
                break;
            case LOG_LEVEL_WARN:
                stream_ << "[WARN] ";
                break;
            case LOG_LEVEL_INFO:
                stream_ << "[INFO] ";
                break;
            case LOG_LEVEL_DEBUG:
                stream_ << "[DEBUG] ";
                break;
        }
        
        // 添加文件名和行号（可选）
        stream_ << file_ << ":" << line_ << " - ";
    }
    
    ~Logger() {
        // 在析构时输出到 std::cout（或 std::cerr）
        stream_ << std::endl;
        std::cout << stream_.str();
        // 如果需要输出到标准错误，使用 std::cerr
        // std::cerr << stream_.str();
    }
    
    // 支持流式输出的模板函数
    template<typename T>
    Logger& operator<<(const T& value) {
        stream_ << value;
        return *this;
    }
    
private:
    std::string getCurrentTime() {
        time_t now = time(nullptr);
        char buf[80];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&now));
        return std::string(buf);
    }

    LogLevel level_;
    const char* file_;
    int line_;
    std::ostringstream stream_;
};

// 定义日志宏
#define LOG_ERROR(...) emai::Logger(emai::LOG_LEVEL_ERROR, __FILE__, __LINE__) << __VA_ARGS__
#define LOG_WARN(...)  emai::Logger(emai::LOG_LEVEL_WARN, __FILE__, __LINE__) << __VA_ARGS__
#define LOG_INFO(...)  emai::Logger(emai::LOG_LEVEL_INFO, __FILE__, __LINE__) << __VA_ARGS__
#define LOG_DEBUG(...) emai::Logger(emai::LOG_LEVEL_DEBUG, __FILE__, __LINE__) << __VA_ARGS__

}



