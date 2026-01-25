
#pragma once

#include <atomic>
#include <chrono>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "util.h"
#include "mediaDataStruct.h"



namespace emai {

class IJpegEncoder {
public:
    using Ptr =  std::shared_ptr<IJpegEncoder>;
    using uPtr =  std::unique_ptr<IJpegEncoder>;

    virtual ~IJpegEncoder() = default;

    virtual bool Init(const int width, const int height) = 0;
    virtual int  unInit() = 0;
    
    virtual bool saveFrameAsJpeg(AVFrame* frame, const std::string& filename, std::string& outFileData) = 0;
    virtual bool saveRgbAsJpeg(const uint8_t* rgb_data, int width, int height, const std::string& filename, std::string& outFileData) = 0;   

    virtual std::vector<uint8_t> encode(const YUVFrame& yuv_frame) = 0;
    virtual std::vector<uint8_t> encodeRgb(const uint8_t* rgb_data, int width, int height) = 0;
    virtual AVFrame* convertYUV420PtoRGB24(AVFrame* yuv_frame) = 0;    


};       

}
 
 