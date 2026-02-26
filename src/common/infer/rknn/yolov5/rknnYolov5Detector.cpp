#include "util.h"
#include "rknnPostprocess.h"
#include "rknnYolov5Detector.h"

#include "jpegEncoderFfmpegCpu.h"

#include <errno.h>
#include <unistd.h>

namespace emai
{
    
RknnYolov5Detector::~RknnYolov5Detector()
{
    LOG_INFO("RknnYolov5Detector::~RknnYolov5Detector");
    deinit();
}

int RknnYolov5Detector::init()
{
    LOG_INFO("RknnYolov5Detector::init");

    if (initOk)
    {
        LOG_WARN("Detector is already initialized");
        return -1;
    }

    // 记录当前工作目录和模型路径
    char cwd[256];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        LOG_INFO("Current working directory: " << cwd);
    }
    LOG_INFO("Model path: " << model_path);

    memset(&rga_ctx, 0, sizeof(rga_context));

    /* Create the neural network */
    LOG_INFO("Loading model...");
    int model_data_size = 0;
    model_data = load_model(model_path.c_str(), &model_data_size);
    if (!model_data) {
        LOG_ERROR("Failed to load model data");
        return -1;
    }
    auto ret = rknn_init(&ctx, model_data, model_data_size, 0);
    if (ret < 0)
    {
        LOG_ERROR("rknn_init error ret=" << ret);
        return -1;
    }

    rknn_sdk_version version;
    ret = rknn_query(ctx, RKNN_QUERY_SDK_VERSION, &version,
                     sizeof(rknn_sdk_version));
    if (ret < 0)
    {
        LOG_ERROR("rknn_init error ret=" << ret);
        return -1;
    }
    LOG_INFO("sdk version: " << version.api_version << " driver version: " << version.drv_version);    

    ret = rknn_query(ctx, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));
    if (ret < 0)
    {
        LOG_ERROR("rknn_init error ret=" << ret);
        return -1;
    }
    LOG_INFO("model input num: " << io_num.n_input << ", output num: " << io_num.n_output);

    rknn_tensor_attr input_attrs[io_num.n_input];
    memset(input_attrs, 0, sizeof(input_attrs));
    for (int i = 0; i < io_num.n_input; i++)
    {
        input_attrs[i].index = i;
        ret = rknn_query(ctx, RKNN_QUERY_INPUT_ATTR, &(input_attrs[i]),
                         sizeof(rknn_tensor_attr));
        if (ret < 0)
        {
            LOG_ERROR("rknn_init error ret=" << ret);
            return -1;
        }
        dump_tensor_attr(&(input_attrs[i]));

        if (i == 0)
        {
            input_attr = input_attrs[i];
        }
    }

    output_attrs = (rknn_tensor_attr *)malloc(sizeof(rknn_tensor_attr) * io_num.n_output);
    if (!output_attrs)
    {
        LOG_ERROR("Failed to allocate memory for output_attrs");
        return -1;
    }
    memset(output_attrs, 0, sizeof(output_attrs));
    for (int i = 0; i < io_num.n_output; i++)
    {
        output_attrs[i].index = i;
        ret = rknn_query(ctx, RKNN_QUERY_OUTPUT_ATTR, &(output_attrs[i]),
                         sizeof(rknn_tensor_attr));
        dump_tensor_attr(&(output_attrs[i]));
    }

    if (input_attrs[0].fmt == RKNN_TENSOR_NCHW)
    {
        LOG_INFO("model is NCHW input fmt\n");
        width = input_attrs[0].dims[0];
        height = input_attrs[0].dims[1];
    }
    else
    {
        LOG_INFO("model is NHWC input fmt\n");
        width = input_attrs[0].dims[1];
        height = input_attrs[0].dims[2];
    }

    RGA_init(&rga_ctx);
    initOk = true;

    if (!jpegEncoder_) 
    {
        jpegEncoder_ = std::make_shared<emai::JpegEncoderFfmpegCpu>();

        if (!jpegEncoder_ || !jpegEncoder_->Init(width, height))
        {
            jpegEncoder_.reset();
            LOG_ERROR("jpegEncoder_ init failed, width:" << width << ", height:" << height);
        }
    } 

    LOG_INFO("init successfully, model input height=" << height << ", width=" << width << ", channel=" << channel);

    return 0;
}

int RknnYolov5Detector::deinit()
{
    LOG_INFO("RknnYolov5Detector::deinit");
    
    if (!initOk)
    {
        LOG_WARN("Detector is not initialized");
        return -1;
    }

    RGA_deinit(&rga_ctx);

    if (model_data)
    {
        free(model_data);
    }

    if (ctx)
    {
        rknn_destroy(ctx);
    }

    if (output_attrs)
    {
        free(output_attrs);
    }

    initOk = false;
    return 0;
}

int RknnYolov5Detector::detect(const YUVFrame &frame, detect_result_group_t &out_detect_result_group, int &org_width, int &org_height)
{
    LOG_INFO("RknnYolov5Detector::detect");

    if (!initOk)
    {
        LOG_ERROR("Detector is not initialized");
        return -1;
    }

    int org_ch = 0;

    if (debugEn_)
    {
        LOG_DEBUG("debug mode enabled");
    }
    unsigned char *input_data = load_image_rga(frame, &input_attr, &rga_ctx, &org_height, &org_width, &org_ch);
    if (!input_data)
    {
        LOG_ERROR("Failed to load image data");
        return -1;
    }

    if (debugEn_ && jpegEncoder_) 
    {
        std::string inputfilename = "input_yuv.jpg";
        std::string outputfilename = "output_rgb.jpg";
        std::string fileData;
        AVFrame* avFrame = frame.to_avframe();

        if (!jpegEncoder_->saveFrameAsJpeg(avFrame, inputfilename, fileData)) 
        {
            LOG_ERROR("Failed to save frame as JPEG");
        }
        if (!jpegEncoder_->saveRgbAsJpeg(input_data, width, height, outputfilename, fileData)) 
        {
            LOG_ERROR("Failed to save frame as JPEG");
        }

        if (avFrame)
        {
            av_frame_free(&avFrame);
            avFrame = nullptr;
        }
    }
    LOG_INFO("Image data loaded successfully, org_height=" << org_height << ", org_width=" << org_width << ", org_ch=" << org_ch);

    // for non zero copy
    rknn_input inputs[1];
    rknn_output outputs[io_num.n_output];

    memset(inputs, 0, sizeof(inputs));    
    memset(outputs, 0, sizeof(outputs));
    for (int i = 0; i < io_num.n_output; i++)
    {
        outputs[i].want_float = 0;
    }

    inputs[0].index = 0;
    inputs[0].type = RKNN_TENSOR_UINT8;
    inputs[0].size = width * height * channel;
    inputs[0].fmt = RKNN_TENSOR_NHWC;
    inputs[0].pass_through = 0;
    inputs[0].buf = input_data;

    rknn_inputs_set(ctx, io_num.n_input, inputs);
    auto ret = rknn_run(ctx, NULL);
    if (ret < 0)
    {
        LOG_ERROR("rknn_run error ret=" << ret);
        return -1;
    }

    ret = rknn_outputs_get(ctx, io_num.n_output, outputs, NULL);

    //post process
    float scale_w = (float)width / org_width;
    float scale_h = (float)height / org_height;

    std::vector<float> out_scales;
    std::vector<uint32_t> out_zps;
    for (int i = 0; i < io_num.n_output; ++i)
    {
        out_scales.push_back(output_attrs[i].scale);
        out_zps.push_back(output_attrs[i].zp);
    }

    post_process((uint8_t *)outputs[0].buf, (uint8_t *)outputs[1].buf, (uint8_t *)outputs[2].buf, height, width,
                 box_conf_threshold, nms_threshold, scale_w, scale_h, out_zps, out_scales, &out_detect_result_group);

    ret = rknn_outputs_release(ctx, io_num.n_output, outputs);
    if (ret < 0)
    {
        LOG_ERROR("rknn_outputs_release error ret=" << ret);
        return -1;
    }

    if (input_data)
    {
        free(input_data);
    }

    return 0;
}


unsigned char *RknnYolov5Detector::load_data(FILE *fp, size_t ofst, size_t sz)
{
    unsigned char *data;
    int ret;

    data = NULL;

    if (NULL == fp)
    {
        return NULL;
    }

    ret = fseek(fp, ofst, SEEK_SET);
    if (ret != 0)
    {
        printf("blob seek failure.\n");
        return NULL;
    }

    data = (unsigned char *)malloc(sz);
    if (data == NULL)
    {
        printf("buffer malloc failure.\n");
        return NULL;
    }
    ret = fread(data, 1, sz, fp);
    return data;
}

unsigned char *RknnYolov5Detector::load_model(const char *filename, int *model_size)
{
    FILE *fp;
    unsigned char *data;

    LOG_INFO("Attempting to load model from: " << filename);

    // 检查文件是否存在
    if (access(filename, F_OK) == 0) {
        LOG_INFO("Model file exists: " << filename);
    } else {
        LOG_ERROR("Model file does NOT exist: " << filename);
        char cwd[256];
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            LOG_ERROR("Current working directory: " << cwd);
        }
        return NULL;
    }

    fp = fopen(filename, "rb");
    if (NULL == fp)
    {
        LOG_ERROR("Failed to open file: " << filename << ", errno: " << strerror(errno));
        return NULL;
    }
    LOG_INFO("Successfully opened model file: " << filename);

    fseek(fp, 0, SEEK_END);
    int size = ftell(fp);
    LOG_INFO("Model file size: " << size << " bytes");

    data = load_data(fp, 0, size);

    fclose(fp);

    *model_size = size;
    LOG_INFO("Model loaded successfully");
    return data;
}

const char *RknnYolov5Detector::get_type_string(rknn_tensor_type type)
{
    switch (type)
    {
    case RKNN_TENSOR_FLOAT32:
        return "FP32";
    case RKNN_TENSOR_FLOAT16:
        return "FP16";
    case RKNN_TENSOR_INT8:
        return "INT8";
    case RKNN_TENSOR_UINT8:
        return "UINT8";
    case RKNN_TENSOR_INT16:
        return "INT16";
    default:
        return "UNKNOW";
    }
}

const char *RknnYolov5Detector::get_qnt_type_string(rknn_tensor_qnt_type type)
{
    switch (type)
    {
    case RKNN_TENSOR_QNT_NONE:
        return "NONE";
    case RKNN_TENSOR_QNT_DFP:
        return "DFP";
    case RKNN_TENSOR_QNT_AFFINE_ASYMMETRIC:
        return "AFFINE";
    default:
        return "UNKNOW";
    }
}

const char *RknnYolov5Detector::get_format_string(rknn_tensor_format fmt)
{
    switch (fmt)
    {
    case RKNN_TENSOR_NCHW:
        return "NCHW";
    case RKNN_TENSOR_NHWC:
        return "NHWC";
    default:
        return "UNKNOW";
    }
}

void RknnYolov5Detector::dump_tensor_attr(rknn_tensor_attr *attr)
{
    LOG_INFO("  index=" << attr->index << ", name=" << attr->name << ", n_dims=" << attr->n_dims << 
             ", dims=[" << attr->dims[3] << ", " << attr->dims[2] << ", " << attr->dims[1] << ", " << attr->dims[0] << 
             "], n_elems=" << attr->n_elems << ", size=" << attr->size << ", fmt=" << get_format_string(attr->fmt) << 
             ", type=" << get_type_string(attr->type) << ", qnt_type=" << get_qnt_type_string(attr->qnt_type) << 
             ", zp=" << attr->zp << ", scale=" << attr->scale);   
}

unsigned char* RknnYolov5Detector::load_image_rga(const YUVFrame& yuv_frame, rknn_tensor_attr* input_attr,
                                    rga_context* rga_ctx, int* org_height, int* org_width, int* org_ch) 
{
    
    if (yuv_frame.empty()) {
        LOG_ERROR("YUVFrame is empty!");
        return nullptr;
    }
    
    if (yuv_frame.format != AV_PIX_FMT_YUV420P) {
        LOG_ERROR("Error: Only YUV420P format is supported, got format:" << yuv_frame.format);
        return nullptr;
    }
    
    #if 0
    if (!rga_ctx || !rga_ctx->rga_handle) {
        LOG_ERROR("Error: RGA context not initialized");
        return nullptr;
    }
    #endif
    
    // 获取原始图像的宽高和通道数
    *org_width = yuv_frame.width;
    *org_height = yuv_frame.height;
    *org_ch = 3; // RGB
    
    // 获取模型需要的输入尺寸
    int target_height = 0;
    int target_width = 0;
    int target_channel = 0;
    
    switch (input_attr->fmt) {
    case RKNN_TENSOR_NHWC:
        target_height = input_attr->dims[2];
        target_width = input_attr->dims[1];
        target_channel = input_attr->dims[0];
        break;
    case RKNN_TENSOR_NCHW:
        target_height = input_attr->dims[1];
        target_width = input_attr->dims[0];
        target_channel = input_attr->dims[2];
        break;
    default:
        LOG_ERROR("meet unsupported layout");
        return nullptr;
    }
    
    if (target_channel != 3) {
        LOG_ERROR("Error: Model expects " << target_channel << " channels, but RGB conversion requires 3 channels");
        return nullptr;
    }

    if (!jpegEncoder_)
    {
        LOG_ERROR("Error: JpegEncoder not initialized");
        return nullptr;
    }
    
    AVFrame* av_yuv_frame = yuv_frame.to_avframe();
    if (!av_yuv_frame) {
        LOG_ERROR("Error: YUV frame is null");
        return nullptr;
    }

    AVFrame* rgb_frame = jpegEncoder_->convertYUV420PtoRGB24(av_yuv_frame);
    if (!rgb_frame) {
        LOG_ERROR("Error: Failed to convert YUV420P to RGB24");
        return nullptr;
    }

    int rgb_size = rgb_frame->width * rgb_frame->height * 3;
    std::vector<uint8_t> rgb_data(rgb_size);
    
    for (int y = 0; y < rgb_frame->height; y++) {
        memcpy(rgb_data.data() + y * rgb_frame->width * 3,
               rgb_frame->data[0] + y * rgb_frame->linesize[0],
               rgb_frame->width * 3);
    }


    unsigned char* rgb_resize_buf = (unsigned char*)malloc(target_height * target_width * 3);
    if (!rgb_resize_buf) {
        LOG_ERROR("Error: Failed to allocate memory for RGB resize buffer");
        return nullptr;
    }

    RGA_resize(rga_ctx, -1, rgb_data.data(), rgb_frame->width, rgb_frame->height, -1, rgb_resize_buf, target_width, target_height);

    av_frame_free(&rgb_frame);
    av_frame_free(&av_yuv_frame);

    LOG_INFO("Using RGA for YUV420P->RGB conversion and resizing: " << yuv_frame.width << "x" << yuv_frame.height << " -> " << target_width << "x" << target_height);
         
    
    return rgb_resize_buf;

}



}