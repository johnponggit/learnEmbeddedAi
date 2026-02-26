#!/bin/sh
#
# 人体检测技能 Shell 脚本
# 用于与 demo_5_aiAgentDetect HTTP API 交互
#

# 配置
DETECT_BIN="/userdata/tmp/human_detect/demo_5_aiAgentDetect"
DEFAULT_WEB_PORT=38080
DEFAULT_FLV_PORT=38081
PID_FILE="/tmp/human_detect.pid"
OUTPUT_DIR="/tmp/detect_results"
JPEG_SAVE_DIR="/userdata/tmp/picoclaw/workspace/detect_images"
DEBUG="${DEBUG:-0}"  # 设置 DEBUG=1 启用调试输出

# 创建输出目录
mkdir -p "$OUTPUT_DIR"

# 检查是否在运行
is_running() {
    if [ -f "$PID_FILE" ]; then
        pid=$(cat "$PID_FILE" 2>/dev/null)
        if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
            return 0
        else
            rm -f "$PID_FILE" 2>/dev/null
        fi
    fi
    return 1
}

# 获取基础 URL
get_base_url() {
    web_port="${1:-$DEFAULT_WEB_PORT}"
    echo "http://localhost:$web_port"
}

# 等待服务启动
wait_for_service() {
    base_url="$1"
    max_wait="${2:-10}"
    count=0
    [ "$DEBUG" = "1" ] && echo "[DEBUG] 等待服务启动: $base_url (最长 $max_wait 秒)" >&2
    while [ $count -lt $max_wait ]; do
        wget -q -O /dev/null "$base_url/stream_status" 2>/dev/null
        http_code=$?
        [ "$DEBUG" = "1" ] && echo "[DEBUG] 尝试 $((count + 1))/$max_wait: HTTP $http_code" >&2
        [ "$http_code" = "0" ] && return 0
        sleep 1
        count=$((count + 1))
    done
    [ "$DEBUG" = "1" ] && echo "[DEBUG] 服务启动超时" >&2
    return 1
}

# 启动检测
start_detect() {
    rtsp_url="$1"
    web_port="${2:-$DEFAULT_WEB_PORT}"
    flv_port="${3:-$DEFAULT_FLV_PORT}"

    [ "$DEBUG" = "1" ] && echo "[DEBUG] 开始启动检测" >&2
    [ "$DEBUG" = "1" ] && echo "[DEBUG] RTSP URL: $rtsp_url" >&2
    [ "$DEBUG" = "1" ] && echo "[DEBUG] Web 端口: $web_port, FLV 端口: $flv_port" >&2

    # 检查 RTSP URL
    if [ -z "$rtsp_url" ]; then
        echo '{"success": false, "error": "缺少 RTSP URL"}'
        return 1
    fi

    # 检查是否已运行
    if is_running; then
        [ "$DEBUG" = "1" ] && echo "[DEBUG] 检测进程已在运行" >&2
        echo '{"success": false, "error": "检测进程已在运行"}'
        return 1
    fi

    # 检查可执行文件
    if [ ! -x "$DETECT_BIN" ]; then
        [ "$DEBUG" = "1" ] && echo "[DEBUG] 可执行文件检查失败: $DETECT_BIN" >&2
        echo "{\"success\": false, \"error\": \"可执行文件不存在或无执行权限: $DETECT_BIN\"}"
        return 1
    fi
    [ "$DEBUG" = "1" ] && echo "[DEBUG] 可执行文件存在: $DETECT_BIN" >&2

    # 启动进程
    [ "$DEBUG" = "1" ] && echo "[DEBUG] 启动进程: $DETECT_BIN $web_port $flv_port" >&2
    nohup "$DETECT_BIN" "$web_port" "$flv_port" \
        >"$OUTPUT_DIR/stdout.log" 2>"$OUTPUT_DIR/stderr.log" &

    pid=$!
    echo "$pid" > "$PID_FILE"
    [ "$DEBUG" = "1" ] && echo "[DEBUG] 进程 PID: $pid" >&2

    # 等待进程启动
    sleep 1
    if ! kill -0 "$pid" 2>/dev/null; then
        [ "$DEBUG" = "1" ] && echo "[DEBUG] 进程启动失败，查看日志..." >&2
        [ "$DEBUG" = "1" ] && echo "[DEBUG] STDERR:" >&2 && cat "$OUTPUT_DIR/stderr.log" >&2
        [ "$DEBUG" = "1" ] && echo "[DEBUG] STDOUT:" >&2 && cat "$OUTPUT_DIR/stdout.log" >&2
        echo '{"success": false, "error": "进程启动失败，请查看日志"}'
        return 1
    fi
    [ "$DEBUG" = "1" ] && echo "[DEBUG] 进程已启动" >&2

    # 等待服务启动
    if ! wait_for_service "http://localhost:$web_port" 10; then
        [ "$DEBUG" = "1" ] && echo "[DEBUG] 查看日志文件..." >&2
        [ "$DEBUG" = "1" ] && echo "[DEBUG] STDERR:" >&2 && cat "$OUTPUT_DIR/stderr.log" >&2
        [ "$DEBUG" = "1" ] && echo "[DEBUG] STDOUT:" >&2 && cat "$OUTPUT_DIR/stdout.log" >&2
        [ "$DEBUG" = "1" ] && echo "[DEBUG] 检查端口监听..." >&2
        [ "$DEBUG" = "1" ] && netstat -an | grep "$web_port" | head -5 >&2
        echo '{"success": false, "error": "服务启动超时"}'
        stop_detect >/dev/null 2>&1
        return 1
    fi

    # 启动 RTSP 流
    [ "$DEBUG" = "1" ] && echo "[DEBUG] 启动 RTSP 流: $rtsp_url" >&2
    encoded_url=$(echo "$rtsp_url" | sed 's/ /%20/g')
    wget -q -O /dev/null --post-data="" "http://localhost:$web_port/start_stream?rtsp_url=$encoded_url" 2>/dev/null
    http_code=$?
    [ "$DEBUG" = "1" ] && echo "[DEBUG] RTSP 流启动 HTTP 状态码: $http_code" >&2
    if [ "$http_code" != "0" ]; then
        response=$(wget -q -O - "http://localhost:$web_port/start_stream?rtsp_url=$encoded_url" 2>/dev/null)
        [ "$DEBUG" = "1" ] && echo "[DEBUG] RTSP 响应: $response" >&2
        echo '{"success": false, "error": "无法启动 RTSP 流"}'
        stop_detect >/dev/null 2>&1
        return 1
    fi

    [ "$DEBUG" = "1" ] && echo "[DEBUG] 启动成功！" >&2
    # 返回成功信息
    cat <<EOF
{
    "success": true,
    "task_id": "$pid",
    "web_port": $web_port,
    "flv_port": $flv_port,
    "flv_url": "http://localhost:$flv_port/live",
    "web_url": "http://localhost:$web_port"
}
EOF
}

# 停止检测
stop_detect() {
    if [ -f "$PID_FILE" ]; then
        pid=$(cat "$PID_FILE" 2>/dev/null)
        if [ -n "$pid" ]; then
            kill "$pid" 2>/dev/null
            sleep 2
            kill -9 "$pid" 2>/dev/null
        fi
        rm -f "$PID_FILE"
    fi

    # 强制杀死所有同名进程
    pkill -9 -f "$(basename "$DETECT_BIN")" 2>/dev/null

    echo '{"success": true}'
}

# 获取流状态
get_stream_status() {
    web_port="${1:-$DEFAULT_WEB_PORT}"
    base_url=$(get_base_url "$web_port")
    wget -q -O - "$base_url/stream_status" 2>/dev/null || echo '{"is_streaming": false}'
}

# 获取检测结果
get_detection_result() {
    web_port="${1:-$DEFAULT_WEB_PORT}"
    base_url=$(get_base_url "$web_port")
    wget -q -O - "$base_url/get_detection_result" 2>/dev/null || echo '{"count": 0, "detected": false, "confidence": 0}'
}

# 获取性能统计
get_perf_stats() {
    web_port="${1:-$DEFAULT_WEB_PORT}"
    base_url=$(get_base_url "$web_port")
    wget -q -O - "$base_url/get_perf_stats" 2>/dev/null || echo '{}'
}

# 获取完整状态
get_status() {
    web_port="${1:-$DEFAULT_WEB_PORT}"
    base_url=$(get_base_url "$web_port")

    if ! is_running; then
        echo '{"success": false, "error": "检测进程未运行", "is_running": false}'
        return 1
    fi

    # 获取各个端点的数据
    stream_status=$(get_stream_status "$web_port")
    detection_result=$(get_detection_result "$web_port")
    perf_stats=$(get_perf_stats "$web_port")

    # 解析并合并结果
    is_streaming=$(echo "$stream_status" | grep -o '"is_streaming":[^,}]*' | cut -d: -f2)
    current_url=$(echo "$stream_status" | grep -o '"current_url":"[^"]*"' | cut -d'"' -f4)
    detect_count=$(echo "$detection_result" | grep -o '"count":[0-9]*' | cut -d: -f2)
    detected=$(echo "$detection_result" | grep -o '"detected":[^,}]*' | cut -d: -f2)
    confidence=$(echo "$detection_result" | grep -o '"confidence":[0-9.]*' | cut -d: -f2)
    timestamp=$(date -u +"%Y-%m-%dT%H:%M:%SZ")

    # 获取 bbox 信息
    bbox_x=$(echo "$detection_result" | grep -o '"x":[0-9]*' | head -1 | cut -d: -f2 || echo 0)
    bbox_y=$(echo "$detection_result" | grep -o '"y":[0-9]*' | head -1 | cut -d: -f2 || echo 0)
    bbox_w=$(echo "$detection_result" | grep -o '"width":[0-9]*' | head -1 | cut -d: -f2 || echo 0)
    bbox_h=$(echo "$detection_result" | grep -o '"height":[0-9]*' | head -1 | cut -d: -f2 || echo 0)

    cat <<EOF
{
    "success": true,
    "is_streaming": ${is_streaming:-false},
    "current_url": "${current_url:-}",
    "detection": {
        "enabled": true,
        "count": ${detect_count:-0},
        "detected": ${detected:-false},
        "confidence": ${confidence:-0.0},
        "bbox": {
            "x": ${bbox_x:-0},
            "y": ${bbox_y:-0},
            "width": ${bbox_w:-0},
            "height": ${bbox_h:-0}
        }
    },
    "performance": $perf_stats,
    "timestamp": "$timestamp"
}
EOF
}

# 列出运行状态
list_status() {
    web_port="${1:-$DEFAULT_WEB_PORT}"

    if is_running; then
        stream_status=$(get_stream_status "$web_port")
        is_streaming=$(echo "$stream_status" | grep -o '"is_streaming":[^,}]*' | cut -d: -f2)
        current_url=$(echo "$stream_status" | grep -o '"current_url":"[^"]*"' | cut -d'"' -f4)
        status_val="idle"
        if [ "$is_streaming" = "true" ]; then
            status_val="running"
        fi

        cat <<EOF
[
    {
        "task_id": "1",
        "rtsp_url": "${current_url:-}",
        "status": "$status_val"
    }
]
EOF
    else
        echo '[]'
    fi
}

# 启用/禁用自动保存 JPEG
enable_jpeg_save() {
    enabled="${1:-true}"
    interval="${2:-30}"
    web_port="${3:-$DEFAULT_WEB_PORT}"
    base_url=$(get_base_url "$web_port")

    [ "$DEBUG" = "1" ] && echo "[DEBUG] 设置JPEG自动保存: enabled=$enabled, interval=$interval" >&2

    wget -q -O /dev/null --post-data="" "http://localhost:$web_port/enable_jpeg_save?enabled=$enabled&interval=$interval&output_dir=$JPEG_SAVE_DIR" 2>/dev/null
    if [ "$?" = "0" ]; then
        cat <<EOF
{
    "success": true,
    "enabled": $enabled,
    "interval": $interval,
    "output_dir": "$JPEG_SAVE_DIR"
}
EOF
        LOG_INFO "JPEG自动保存已设置: enabled=$enabled, interval=$interval"
    else
        echo '{"success": false, "error": "无法设置JPEG自动保存"}'
    fi
}

# 获取最新 JPEG 信息
get_jpeg_status() {
    web_port="${1:-$DEFAULT_WEB_PORT}"
    base_url=$(get_base_url "$web_port")
    wget -q -O - "$base_url/jpeg_status" 2>/dev/null || echo '{"has_jpeg": false}'
}

# 获取最新 JPEG 数据
get_latest_jpeg() {
    web_port="${1:-$DEFAULT_WEB_PORT}"
    base_url=$(get_base_url "$web_port")
    wget -q -O - "$base_url/latest_jpeg" 2>/dev/null
}

# 检查是否有新的 JPEG 图片
check_new_jpeg() {
    web_port="${1:-$DEFAULT_WEB_PORT}"
    output_file="${2:-/tmp/latest_jpeg.jpg}"

    [ "$DEBUG" = "1" ] && echo "[DEBUG] 检查新JPEG图片" >&2

    status=$(get_jpeg_status "$web_port")

    # 解析 JSON 检查 has_jpeg
    has_jpeg=$(echo "$status" | grep -o '"has_jpeg":[^,}]*' | cut -d: -f2)
    jpeg_path=$(echo "$status" | grep -o '"path":"[^"]*"' | cut -d'"' -f4)

    [ "$DEBUG" = "1" ] && echo "[DEBUG] has_jpeg=$has_jpeg, path=$jpeg_path" >&2

    if [ "$has_jpeg" = "true" ] && [ -n "$jpeg_path" ] && [ -f "$jpeg_path" ]; then
        # 检查文件是否在最近30秒内创建
        file_age=$(( $(date +%s) - $(stat -c %Y "$jpeg_path" 2>/dev/null || echo "0") ))
        [ "$DEBUG" = "1" ] && echo "[DEBUG] 文件年龄: $file_age 秒" >&2

        if [ "$file_age" -lt 30 ]; then
            # 文件很新，返回文件路径
            echo "$jpeg_path"
            return 0
        fi
    fi

    return 1
}

# 显示帮助
show_help() {
    cat <<EOF
人体检测技能 Shell 脚本

用法: detect_cli.sh <命令> [参数]

命令:
    start <rtsp_url> [web_port] [flv_port]  启动检测
    stop                                    停止检测
    status [web_port]                       获取检测状态
    list [web_port]                         列出运行状态
    enable_jpeg [enabled] [interval]        启用/禁用自动保存JPEG
    jpeg_status [web_port]                  获取JPEG状态
    check_jpeg [web_port]                   检查是否有新图片
    help                                    显示此帮助信息

环境变量:
    DEBUG=1                                 启用调试输出（显示详细日志）

示例:
    detect_cli.sh start rtsp://192.168.1.100:554/live
    detect_cli.sh status
    DEBUG=1 detect_cli.sh start rtsp://192.168.1.100:554/live  # 调试模式
    detect_cli.sh stop

默认端口:
    Web 端口: 38080
    FLV 端口: 38081
EOF
}

# 主程序
main() {
    action="${1:-}"
    web_port="${DEFAULT_WEB_PORT}"
    flv_port="${DEFAULT_FLV_PORT}"

    case "$action" in
        start)
            rtsp_url="$2"
            web_port="${3:-$DEFAULT_WEB_PORT}"
            flv_port="${4:-$DEFAULT_FLV_PORT}"
            start_detect "$rtsp_url" "$web_port" "$flv_port"
            ;;
        stop)
            stop_detect
            ;;
        status)
            web_port="${2:-$DEFAULT_WEB_PORT}"
            get_status "$web_port"
            ;;
        list)
            web_port="${2:-$DEFAULT_WEB_PORT}"
            list_status "$web_port"
            ;;
        enable_jpeg)
            enabled="${2:-true}"
            interval="${3:-30}"
            web_port="${4:-$DEFAULT_WEB_PORT}"
            enable_jpeg_save "$enabled" "$interval" "$web_port"
            ;;
        jpeg_status)
            web_port="${2:-$DEFAULT_WEB_PORT}"
            get_jpeg_status "$web_port"
            ;;
        check_jpeg)
            web_port="${2:-$DEFAULT_WEB_PORT}"
            check_new_jpeg "$web_port"
            ;;
        help|--help|-h)
            show_help
            ;;
        *)
            echo '{"success": false, "error": "Unknown command"}'
            exit 1
            ;;
    esac
}

# 执行主程序
main "$@"