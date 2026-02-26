---
name: human-detect
description: RTSP video stream real-time human detection based on YOLOv5 and RKNN with hardware acceleration and bounding box drawing. Supports auto-save JPEG images.
homepage: https://github.com/johnponggit/learnEmbeddedAi
metadata: {"nanobot":{"emoji":"🎥","requires":{"bins":["wget"]}}}
---

# Human Detection Skill

基于 YOLOv5 的人体检测技能，用于实时检测 RTSP 视频流中的人员。

## 功能

- 启动/停止 RTSP 视频流人体检测
- 实时获取检测结果（人数、位置、置信度）
- 在检测到的人体区域绘制检测框
- 获取检测性能统计
- FLV 视频流输出（多人同时观看，带检测框显示）
- **自动保存检测到的人体图片为 JPEG**

## 使用方法

### 基础功能

1. **启动检测**:
   ```
   启动人体检测，监控 rtsp://192.168.1.100/stream
   ```

2. **获取结果**:
   ```
   查看检测结果
   现在检测到几个人？
   ```

3. **停止检测**:
   ```
   停止人体检测
   ```

4. **获取性能**:
   ```
   查看检测性能
   当前帧率是多少？
   ```

### 自动保存图片功能（检测到人时自动保存）

**启用自动保存 JPEG**:
```bash
sh /userdata/tmp/picoclaw/workspace/skills/human-detect/detect_cli.sh enable_jpeg true

# 自定义间隔（秒）
sh /userdata/tmp/picoclaw/workspace/skills/human-detect/detect_cli.sh enable_jpeg true 60
```

**检查 JPEG 状态**:
```bash
sh /userdata/tmp/picoclaw/workspace/skills/human-detect/detect_cli.sh jpeg_status
```

**检查是否有新图片**:
```bash
sh /userdata/tmp/picoclaw/workspace/skills/human-detect/detect_cli.sh check_jpeg
```

### 通过 PicoClaw 自动管理

PicoClaw 可以自动启动检测、启用自动保存，并在检测到人时通知你：

```
你: 启动人体检测，监控 rtsp://192.168.1.100/stream
我: 检测已启动！已启用自动保存功能

[后台检测到人，保存了图片]
我: 检测到 1 人！
    - 位置: (200, 150) 大小: 100x200
    - 置信度: 0.95
    - Web预览: http://192.168.31.139:38080
    - 保存图片: /userdata/tmp/picoclaw/workspace/detect_images/detect_xxx.jpg

你: 停止检测
我: 检测已停止
```

技能脚本位于：`/userdata/tmp/picoclaw/workspace/skills/human-detect/detect_cli.sh`

## 命令说明

```bash
# 启动检测
sh /userdata/tmp/picoclaw/workspace/skills/human-detect/detect_cli.sh start <rtsp_url>

# 查看状态
sh /userdata/tmp/picoclaw/workspace/skills/human-detect/detect_cli.sh status

# 停止检测
sh /userdata/tmp/picoclaw/workspace/skills/human-detect/detect_cli.sh stop

# 调试模式
DEBUG=1 sh /userdata/tmp/picoclaw/workspace/skills/human-detect/detect_cli.sh start <rtsp_url>
```

## 配置

- 检测程序路径: `/userdata/tmp/human_detect/demo_5_aiAgentDetect`
- 模型路径: `/userdata/tmp/human_detect/model/`
- Picoclaw Workspace: `/userdata/tmp/picoclaw/workspace`
- 默认 Web 端口: `38080`
- 默认 FLV 端口: `38081`

## 输出格式

检测结果包含：
- 人数 (count): 检测到的人员数量
- 检测状态 (detected): 是否检测到人员
- 人员位置 (x, y, width, height): 检测框坐标
- 置信度 (confidence): 检测置信度 (0.0-1.0)
- 性能指标 (fps, detect_ms): 帧率和检测耗时

启动成功返回 JSON：
```json
{
  "success": true,
  "task_id": "<pid>",
  "web_port": 38080,
  "flv_port": 38081,
  "flv_url": "http://localhost:38081/live",
  "web_url": "http://localhost:38080"
}
```

## 网络接口

- Web 界面: `http://<设备IP>:38080`
- FLV 视频流: `http://<设备IP>:38081/live` (带检测框显示)

## 技术特点

- 使用 Rockchip RKNN 进行硬件加速推理
- 支持 RKMPP 硬件解码和编码
- 低延迟视频处理（< 100ms）
- 最多支持 10 人同时观看 FLV 流
- 实时绘制检测框（红色矩形框 Y=76, U=84, V=255）
- 检测框可配置（启用/禁用、线条粗细）
- Shell 脚本使用 wget（无需 Python）

## 故障排查

### 检查进程

```bash
ps aux | grep demo_5_aiAgentDetect
```

### 查看日志

```bash
cat /tmp/detect_results/stdout.log
cat /tmp/detect_results/stderr.log
```

### 检查端口

```bash
netstat -an | grep 38080
netstat -an | grep 38081
```

### 手动测试 API

```bash
wget -q -O - http://localhost:38080/stream_status
```

## 示例对话

```
你: 启动人体检测，监控 rtsp://192.168.1.100/stream
我: 检测已启动！
    - Web界面: http://192.168.31.139:38080
    - FLV流: http://192.168.31.139:38081/live

你: 现在的检测结果是什么？
我: 当前检测状态：
    - 状态: 检测中
    - 检测到 1 人
    - 位置: (200, 150) 大小: 100x200
    - 置信度: 0.95
    - FPS: 25
    - 检测耗时: 45ms

你: 停止检测
我: 检测已停止。
```

## 文件结构

```
/userdata/tmp/human_detect/
├── demo_5_aiAgentDetect        # C++ 检测程序
└── model**                     # RKNN 模型相关文件

/userdata/tmp/picoclaw/workspace/skills/human-detect/
├── SKILL.md                    # 技能文档（本文件）
└── detect_cli.sh               # Shell 脚本

/tmp/
├── human_detect.pid            # 进程 PID
└── detect_results/
    ├── stdout.log              # 标准输出
    └── stderr.log              # 错误日志
```