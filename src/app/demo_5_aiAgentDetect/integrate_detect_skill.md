# 人体检测技能集成指南

本文档详细说明如何将 demo_5_aiAgentDetect 的 C++ 人体检测程序集成到 PicoClaw 的 Skill 系统中。

## 一、整体架构

```
┌─────────────────┐     ┌─────────────────┐     ┌─────────────────┐
│    PicoClaw     │────▶│  detect_cli.sh  │────▶│  demo_5_detect  │
│  (AI助手/Agent) │     │   (Skill脚本)   │     │   (C++检测进程) │
└─────────────────┘     └─────────────────┘     └─────────────────┘
                             │                              │
                             │  HTTP API                    │  JSON输出
                             └──────────────────────────────┘
                                        ↓
                              检测结果 / FLV流（带检测框）
```

## 系统组件

### C++ 检测进程 (demo_5_aiAgentDetect)

核心组件：
- `DecoderManager` - 视频流管理和解码
- `RknnYolov5Detector` - YOLOv5 RKNN 检测
- `BBoxDrawer` - 检测框绘制器
- `MppH264Encoder` - H.264 硬件编码
- `HttpFlvStreamer` - FLV 流推送

### BBoxDrawer 功能

`BBoxDrawer` 负责在 YUV420P 帧上绘制检测框：
- 红色矩形框（Y=76, U=84, V=255）
- 可配置线条粗细
- 支持启用/禁用绘制
- 不做缩放，由 MppH264Encoder 处理

## 二、详细实施步骤

### 步骤 1: 构建 C++ 检测程序

在开发主机环境交叉编译 `demo_5_aiAgentDetect`：

```bash
# 克隆learnEmbeddedAi工程
cd /work/tmp
git clone https://github.com/johnponggit/learnEmbeddedAi.git
cd learnEmbeddedAi

# 注意：在文件toolchains/rv1126.cmake配置了当前环境的交叉编译工具链，需根据实际情况修改
# 创建构建目录并配置交叉编译
mkdir -p build
cd build
cmake ..

# 编译
make demo_5_aiAgentDetect -j4
```

构建成功后，将可执行文件部署到设备：

```bash
# 复制可执行文件和模型到目标设备（假设设备IP为192.168.31.139）
cd ..
scp build/src/app/demo_5_aiAgentDetect/demo_5_aiAgentDetect root@192.168.31.139:/userdata/tmp/human_detect/
scp -r model/rockchip/* root@192.168.31.139:/userdata/tmp/human_detect/model/
```

**交叉编译工具链说明**：
- 工具链路径: 在文件toolchains/rv1126.cmake中配置：`/work/onvif/crosscompilation/toolchain/gcc-arm-8.3-2019.03-x86_64-arm-linux-gnueabihf/`
- 目标平台: ARMv7 (Cortex-A7, 32-bit)
- FFmpeg 版本: rv1126_4.1.3_rkmpp (支持硬件加速)

**设备安装目录**：
- 可执行文件: `/userdata/tmp/human_detect/demo_5_aiAgentDetect`
- 模型文件: `/userdata/tmp/human_detect/model/`

### 步骤 2: 创建 CLI 模式支持

由于 demo_5 已经有 HTTP 接口，我们直接使用它。不需要额外修改 C++ 代码。

确认程序的功能：
- 启动 RTSP 流检测：`POST /start_stream?rtsp_url=xxx`
- 停止检测：`POST /stop_stream`
- 获取检测状态：`GET /stream_status`
- 获取检测结果：`GET /get_detection_result`
- 更新检测框设置：`POST /update_bbox_settings?enabled=true&thickness=4`
- 获取性能统计：`GET /get_perf_stats`

### 步骤 3: 创建 Skill 目录结构

```bash
# 在 picoclaw workspace 创建技能目录
# 进入目标板设备
mkdir -p /userdata/tmp/picoclaw/workspace/skills/human-detect
cd /userdata/tmp/picoclaw/workspace/skills/human-detect
```

### 步骤 4: 创建 SKILL.md 文档

在主机环境拷贝目录demo_5_aiAgentDetect下的SKILL.md到目标板
```bash
scp src/app/demo_5_aiAgentDetect/SKILL.md root@192.168.31.139:/userdata/tmp/picoclaw/workspace/skills/human-detect/
```

### 步骤 5: 创建 Shell Skill 脚本

在主机环境拷贝目录demo_5_aiAgentDetect下的脚本到目标板
```bash
scp src/app/demo_5_aiAgentDetect/detect_cli.sh root@192.168.31.139:/userdata/tmp/picoclaw/workspace/skills/human-detect/
```

**detect_cli.sh 功能**：
- 使用 wget 进行 HTTP 请求（目标板可能没有 curl）
- 支持启动/停止/状态查询命令
- 自动管理进程 PID
- JSON 格式输出


### 步骤 6: 设置权限

```bash
#进入目标板
chmod +x /userdata/tmp/picoclaw/workspace/skills/human-detect/detect_cli.sh
chmod +x /userdata/tmp/human_detect/demo_5_aiAgentDetect
```

### 步骤 7: 测试 Shell Skill 脚本

准备一个可用的rtsp地址，在终端中测试：

```bash
# 测试启动
/userdata/tmp/picoclaw/workspace/skills/human-detect/detect_cli.sh start rtsp://192.168.13.72:554/live/test

# 测试状态查询
/userdata/tmp/picoclaw/workspace/skills/human-detect/detect_cli.sh status

# 测试停止
/userdata/tmp/picoclaw/workspace/skills/human-detect/detect_cli.sh stop

# 调试模式（查看详细日志）
DEBUG=1 /userdata/tmp/picoclaw/workspace/skills/human-detect/detect_cli.sh start rtsp://192.168.13.72:554/live/test
```


## 三、在 PicoClaw 中使用

- ***注意：要修改PicoClaw的配置文件/oem/.picoclaw/config.json内容，将"restrict_to_workspace"设置为false*

### 方式 1: 通过命令行直接调用

```bash
# 启动检测
./picoclaw agent -m "启动人体检测，监控 rtsp://192.168.13.72:554/live/test"

# 查看结果
./picoclaw agent -m "当前检测结果如何？"

# 查看性能
./picoclaw agent -m "检测性能怎么样？"

# 停止检测
./picoclaw agent -m "停止人体检测"
```

### 方式 2: 通过聊天频道（QQ等）

```
#首先在目标板启动picoclaw gateway
./picoclaw gateway


#然后打开手机QQ进行文字沟通

你: 启动人体检测，监控 rtsp://192.168.1.100/stream
我: 检测已启动！
    - 任务ID: 12345
    - Web界面: http://192.168.31.139:38080
    - FLV流: http://192.168.31.139:38081/live (带检测框)

你: 现在的检测结果是什么？
我: 当前检测状态：
    - 状态: 检测中
    - 检测到 1 人
    - 位置: (200, 150) 大小: 100x200
    - 置信度: 0.95
    - FPS: 25
    - 检测耗时: 45ms

你: 调整检测框
我: 检测框已更新，线条粗细为 4
```

## 四、detect_cli.sh 命令说明

### 基本用法

```bash
./detect_cli.sh <命令> [参数]
```

### 可用命令

| 命令 | 参数 | 说明 |
|------|------|------|
| `start` | `<rtsp_url> [web_port] [flv_port]` | 启动检测 |
| `stop` | 无 | 停止检测 |
| `status` | `[web_port]` | 获取检测状态 |
| `list` | `[web_port]` | 列出运行状态 |
| `help` | 无 | 显示帮助信息 |

### 环境变量

| 变量 | 值 | 说明 |
|------|-----|------|
| `DEBUG` | `1` | 启用调试输出（显示详细日志） |

### 示例

```bash
# 启动检测
./detect_cli.sh start rtsp://192.168.1.100:554/live

# 指定端口启动
./detect_cli.sh start rtsp://192.168.1.100:554/live 38080 38081

# 查看状态
./detect_cli.sh status

# 调试模式启动
DEBUG=1 ./detect_cli.sh start rtsp://192.168.1.100:554/live

# 停止检测
./detect_cli.sh stop
```


## 六、故障排查

### 问题 1: 无法启动 C++ 程序

```bash
# 检查可执行文件
ls -la /userdata/tmp/human_detect/demo_5_aiAgentDetect

# 检查依赖
ldd /userdata/tmp/human_detect/demo_5_aiAgentDetect

# 查看错误日志
cat /tmp/detect_results/stderr.log

# 调试模式启动查看详细信息
DEBUG=1 ./detect_cli.sh start <rtsp_url>
```

### 问题 2: HTTP API 无响应

```bash
# 检查端口是否监听
netstat -an | grep 38080

# 手动测试 API（使用 wget）
wget -q -O - http://localhost:38080/stream_status
```

### 问题 3: RTSP 流连接失败

```bash
# 测试 RTSP 连接
ffprobe rtsp://192.168.1.100/stream

# 检查网络连通性
ping 192.168.1.100
```

### 问题 4: 模型加载失败

```bash
# 检查模型文件
ls -la /userdata/tmp/human_detect/

# 检查文件权限
chmod 644 /userdata/tmp/human_detect/*.rknn
```


## 七、性能优化建议

1. **降低检测频率**: 修改 C++ 代码中的 `detectFrameSkipNum_` 参数
2. **使用硬件解码**: 确保使用 RKMPP 而非 CPU 解码
3. **限制帧率**: 控制输入流帧率在 15-25 FPS
4. **调整输出分辨率**: 降低编码输出分辨率以节省带宽
5. **减少检测框厚度**: 使用偶数厚度（如 2、4）避免 UV 对齐问题


## 八、安全考虑

1. **访问控制**: 为 HTTP API 添加认证机制
2. **RTSP 安全**: 使用 RTSP over TLS (rtsps://)
3. **数据加密**: 检测结果存储时加密敏感信息
4. **日志脱敏**: 避免在日志中记录原始 RTSP URL
5. **隐私保护**: 保存的图片包含人脸等敏感信息，需妥善管理

## 九、文件清单

部署到设备后的完整文件列表：

```
/userdata/tmp/human_detect/
├── demo_5_aiAgentDetect                    # C++ 检测程序
├── yolov5s_relu_rv1109_rv1126_out_opt.rknn  # RKNN 模型
└── coco_80_labels_list.txt                  # 标签文件

/userdata/tmp/picoclaw/workspace/skills/human-detect/
├── SKILL.md                                 # 技能文档
└── detect_cli.sh                            # Shell 脚本

/tmp/
├── human_detect.pid                         # 进程 PID
└── detect_results/
    ├── stdout.log                           # 标准输出日志
    └── stderr.log                           # 错误日志
```

## 十、参考资源

- PicoClaw 文档: https://github.com/anthropics/picoclaw
- RKNN 文档: https://github.com/airockchip/rknn-toolkit2
- YOLOv5: https://github.com/ultralytics/yolov5