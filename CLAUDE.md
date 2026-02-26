# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Learn Embedded AI is a C++17 project for RV1126 devices that demonstrates real-time AI video processing with privacy protection features. It combines hardware-accelerated video decoding/encoding with YOLOv5 object detection using Rockchip's NPU.

Key features:
- RTSP video stream processing with hardware acceleration
- Real-time object detection (YOLOv5 on RKNN)
- Bounding box drawing for detected objects
- HTTP web interface for control
- FLV video streaming for multiple clients
- Performance monitoring

## Build and Development Commands

```bash
# Cross-compile for RV1126 using toolchain
cd /work/tmp/learnEmbeddedAi
rm -rf build
mkdir -p build
cd build
cmake -DCMAKE_TOOLCHAIN_FILE=../toolchains/rv1126.cmake ..
make -j4

# Build specific demo
make demo_5_aiAgentDetect -j4

# Clean build
rm -rf build
mkdir -p build
cd build
cmake -DCMAKE_TOOLCHAIN_FILE=../toolchains/rv1126.cmake ..
make -j4

# Run demo applications on target device
./demo_5_aiAgentDetect [web_port] [flv_port]
```

### Cross-Compilation Toolchain

- **Toolchain File**: `toolchains/rv1126.cmake`
- **Toolchain Path**: `/work/onvif/crosscompilation/toolchain/gcc-arm-8.3-2019.03-x86_64-arm-linux-gnueabihf/`
- **Target Platform**: ARMv7 (Cortex-A7, 32-bit)
- **Compiler**: arm-linux-gnueabihf-gcc/g++ (version 8.3.0)
- **FFmpeg Version**: rv1126_4.1.3_rkmpp (hardware acceleration supported)

## Architecture Overview

### Core Components

1. **Media Processing** (`src/common/media/`)
   - `VideoDecoder` - Abstract decoder interface
   - `VideoDecoderFfmpegRkmpp` - H.264 hardware decoder using Rockchip MPP
   - `VideoDecoderFfmpegCpu` - Software decoder fallback
   - `MppH264Encoder` - H.264 hardware encoder
   - `RgaManager` - 2D graphics acceleration (format conversion, scaling)
   - `IJpegEncoder` - JPEG encoding interface

2. **AI Inference** (`src/common/infer/rknn/`)
   - `RknnYolov5Detector` - YOLOv5 object detection on RKNN
   - `rknnRgaFunc` - RGA image preprocessing for inference
   - `rknnPostprocess` - NMS and result post-processing

3. **Data Structures** (`src/common/media/mediaDataStruct.h`)
   - `YUVFrame` - Smart pointer wrapper for AVFrame with copy/move semantics
   - `YUVFrameBuffer` - Thread-safe frame queue with condition variable

4. **Utilities** (`src/common/util/`)
   - Logging macros (`LOG_INFO`, `LOG_ERROR`, etc.)

### Demo Applications

```
src/app/
├── demo_1_blur/              # Manual blur with RTSP input
├── demo_2_autoblur/          # Auto blur based on detection
├── demo_3_autoMosaic/        # Auto mosaic on detected persons
├── demo_4_autoMosaicPerf/    # Performance-optimized with FLV streaming
└── demo_5_aiAgentDetect/     # Person detection with bbox drawing
```

### Technology Stack

| Component | Technology |
|-----------|------------|
| Language | C++17 |
| Video Decode | FFmpeg + Rockchip RKMPP (hardware) |
| Video Encode | Rockchip MPP H.264 |
| Image Processing | RGA (2D graphics acceleration) |
| AI Inference | RKNN + YOLOv5 (NPU) |
| BBox Drawing | Custom YUV renderer |
| Web Server | cpp-httplib |
| JSON | nlohmann/json |
| Build System | CMake 3.1+ |

### Cross-Compilation Toolchain

The project uses the RV1126 cross-compilation toolchain:
- **Toolchain File**: `toolchains/rv1126.cmake`
- **Toolchain Path**: `/work/onvif/crosscompilation/toolchain/gcc-arm-8.3-2019.03-x86_64-arm-linux-gnueabihf/`
- **Target**: ARMv7 (Cortex-A7, 32-bit)
- **Compiler**: gcc-arm-8.3-2019.03-x86_64-arm-linux-gnueabihf
- **FFmpeg Type**: rv1126_4.1.3_rkmpp (hardware acceleration)

## Directory Structure

```
/work/tmp/learnEmbeddedAi/
├── 3rdparty/              # Third-party libraries (static)
│   ├── ffmpeg/           # FFmpeg headers and examples
│   ├── cpp-httplib/      # Single-header HTTP library
│   └── nlohmann/         # Single-header JSON library
├── src/
│   ├── app/              # Demo applications
│   ├── common/
│   │   ├── infer/        # RKNN inference (YOLOv5)
│   │   ├── media/        # Video encode/decode
│   │   └── util/         # Utilities
├── model/
│   └── rockchip/         # RKNN models (.rknn files)
├── toolchains/           # Cross-compilation configs
├── config/               # Configuration files
└── build/                # Build output (gitignored)
```

## Data Flow

```
RTSP Stream
    ↓
FFmpeg/RKMPP Decoder
    ↓
YUVFrameBuffer (thread-safe queue)
    ↓
RknnYolov5Detector (detect persons)
    ↓
Detection Result (bbox, confidence)
    ↓
BBoxDrawer (draw bbox on frame)
    ↓
MppH264Encoder (hardware encode)
    ↓
HttpFlvStreamer (broadcast to clients)
```
    ↓
MosaicProcessor/BlurProcessor (apply effect)
    ↓
MppH264Encoder (hardware encode)
    ↓
HttpFlvStreamer (HTTP FLV streaming)
    ↓
Web Browser / FLV Player
```

## Model Files

The project uses a pre-compiled YOLOv5 RKNN model:
- **Location**: `model/rockchip/yolov5s_relu_rv1109_rv1126_out_opt.rknn`
- **Labels**: `model/rockchip/coco_80_labels_list.txt` (COCO 80-class labels)
- **Target**: Person detection (class ID 0)

## Configuration

### Web Server Endpoints

**demo_1_blur / demo_4_autoMosaicPerf:**

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/` | GET | Web UI page |
| `/start_stream` | POST | Start RTSP stream (param: `rtsp_url`) |
| `/stop_stream` | POST | Stop RTSP stream |
| `/stream_status` | GET | Get stream status |
| `/video_frame` | GET | Get processed frame as JPEG |
| `/update_blur_settings` | POST | Update blur settings |
| `/get_blur_settings` | GET | Get current blur settings |

**demo_5_aiAgentDetect (person detection with bbox):**

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/get_detection_result` | GET | Get detection results (person count, bbox, confidence) |
| `/update_bbox_settings` | POST | Update bbox drawing settings |
| `/get_perf_stats` | GET | Get performance statistics |
| `/flv_status` | GET | Get FLV streaming status |

### FLV Streaming

For `demo_4_autoMosaicPerf` and `demo_5_aiAgentDetect`:
- **FLV Stream URL**: `http://<ip>:<flv_port>/live`
- **Default Ports**: Web=38080, FLV=38081
- **Format**: HTTP-based FLV (Flash Video) streaming
- **demo_5_aiAgentDetect** includes bounding box overlay on the video stream

## Development Guidelines

### Thread Safety

- **YUVFrameBuffer**: Uses `std::mutex` and `std::condition_variable` for thread-safe frame queue
- **Decoder/Encoder**: Each operates in its own thread
- **RTSP Reconnection**: Automatic reconnection with exponential backoff

### Memory Management

- **YUVFrame**: Uses `std::shared_ptr` for AVFrame management
- **Copy vs Reference**: Support both copy (`own_data=true`) and reference modes
- **Move Semantics**: Implemented for efficient frame transfer

### Performance Considerations

- **Hardware Acceleration**: Always prefer RKMPP for decode/encode on RV1126
- **RGA**: Use RGA for format conversion instead of CPU
- **Frame Rate Control**: Built-in frame rate limiting (~8-10 FPS for demo)
- **Buffer Size**: Default 100 frames max in YUVFrameBuffer

### Adding a New Demo

1. Create directory in `src/app/demo_X_newdemo/`
2. Add `main.cpp`, decoder manager, processor, and `html_page.h`
3. Update `src/app/CMakeLists.txt` to add the demo
4. Rebuild in `build/` directory

## Common Issues

### RTSP Reconnection

The RTSP decoder includes automatic reconnection logic:
- Detects consecutive EOFs (MAX_CONSECUTIVE_EOF = 5)
- Attempts reconnection after 5 second interval
- Falls back to full reinitialization if simple reconnection fails

### RKNN Model Loading

- Ensure `.rknn` model file is in the same directory as executable
- Model path is relative to working directory: `./yolov5s_relu_rv1109_rv1126_out_opt.rknn`
- For demo_5_aiAgentDetect, recommended install directory: `/userdata/tmp/human_detect/`
- If model loading fails, check file permissions and path

### Hardware Acceleration

- **RKMPP**: Requires Rockchip Media Process Platform libraries
- **RGA**: Requires Rockchip 2D Graphics Acceleration libraries
- **Fallback**: If hardware unavailable, falls back to CPU (slower)

## Third-Party Libraries

### FFmpeg
- **Version**: Custom build for RV1126
- **Location**: `3rdparty/ffmpeg/include/rv1126_5.1.2_cpu/`
- **Features**: H.264 decode/encode, RTSP, various pixel formats

### cpp-httplib
- **Type**: Single-header library
- **Location**: `3rdparty/cpp-httplib/include/httplib.h`
- **Usage**: HTTP/HTTPS server with keep-alive support

### nlohmann/json
- **Type**: Single-header library
- **Location**: `3rdparty/nlohmann/include/json.hpp`
- **Usage**: JSON parsing and generation

## License

See LICENSE file for project license information.