
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

class JpegEncoderImpl {
public:
    using Ptr =  std::shared_ptr<JpegEncoderImpl>;
    using uPtr =  std::unique_ptr<JpegEncoderImpl>;

    virtual ~JpegEncoderImpl() = default;

    virtual bool Init(const int width, const int height) = 0;
    virtual int  unInit() = 0;
    virtual bool saveFrameAsJpeg(AVFrame* frame, const std::string& filename, std::string& outFileData) = 0;
    virtual std::vector<uint8_t> encode(const YUVFrame& yuv_frame) = 0;


};       

}
 
 