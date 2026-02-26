
#pragma once

#include <memory>
#include "rknnRgaFunc.h"
#include "rknn_api.h"
#include "rknnPostprocess.h"
#include "mediaDataStruct.h"

#include "IJpegEncoder.h"

namespace emai
{

class RknnYolov5Detector
{
public:
    using Ptr  = std::shared_ptr<RknnYolov5Detector>;
    using uPtr = std::unique_ptr<RknnYolov5Detector>;

    RknnYolov5Detector() = default;
    ~RknnYolov5Detector();

    int init();
    int deinit();
    int detect(const YUVFrame &frame, detect_result_group_t &out_detect_result_group, int &org_width, int &org_height);

private:
    const char *get_type_string(rknn_tensor_type type);
    const char *get_format_string(rknn_tensor_format fmt);
    const char *get_qnt_type_string(rknn_tensor_qnt_type type);

    unsigned char *load_data(FILE *fp, size_t ofst, size_t sz);
    unsigned char *load_model(const char *filename, int *model_size);
    unsigned char *load_image_rga(const YUVFrame& yuv_frame, rknn_tensor_attr* input_attr,
                                  rga_context* rga_ctx, int* org_height, int* org_width, int* org_ch);
    void dump_tensor_attr(rknn_tensor_attr *attr);


private:
    rga_context                  rga_ctx;
    rknn_context                 ctx;
    float                        nms_threshold = NMS_THRESH;
    float                        box_conf_threshold = BOX_THRESH;
    std::string                  model_path = "/userdata/tmp/human_detect/model/yolov5s_relu_rv1109_rv1126_out_opt.rknn";  
    unsigned char               *model_data = nullptr;

    rknn_input_output_num        io_num;
    rknn_tensor_attr             input_attr;
    rknn_tensor_attr            *output_attrs;

    int                           width = 0;
    int                           height = 0;
    int                           channel = 0;

    bool                          initOk = false;

    bool                          debugEn_ = false; 
    IJpegEncoder::Ptr             jpegEncoder_{nullptr};

};
}