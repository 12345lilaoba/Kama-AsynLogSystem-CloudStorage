#!/bin/bash
# start.sh —— 启动 Cloud Storage 服务
# 用法: bash scripts/start.sh [mysql_password] [server_conf] [log_conf]
#
#   mysql_password  MySQL 密码（默认从 CLOUD_STORAGE_MYSQL_PASSWORD 环境变量读取）
#   server_conf     服务配置文件路径（默认 Storage.conf）
#   log_conf        日志配置文件路径（默认 ../../log_system/logs_code/config.conf）

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
SERVER_DIR="$PROJECT_ROOT/src/server"

# ---- 参数 ----
MYSQL_PASSWORD="${1:-${CLOUD_STORAGE_MYSQL_PASSWORD:-}}"
SERVER_CONF="${2:-Storage.conf}"
LOG_CONF="${3:-../../log_system/logs_code/config.conf}"

echo "========================================"
echo "  Cloud Storage 启动脚本"
echo "========================================"
echo "服务端目录 : $SERVER_DIR"
echo "服务配置   : $SERVER_CONF"
echo "日志配置   : $LOG_CONF"
echo "----------------------------------------"

# 检查可执行文件
if [ ! -f "$SERVER_DIR/test" ]; then
    echo "[ERROR] 未找到可执行文件 $SERVER_DIR/test，请先运行 bash scripts/build.sh"
    exit 1
fi

# 检查密码
if [ -z "$MYSQL_PASSWORD" ]; then
    echo "[ERROR] MySQL 密码未设置，请传入参数或设置环境变量："
    echo "  export CLOUD_STORAGE_MYSQL_PASSWORD='your_password'"
    echo "  bash scripts/start.sh your_password"
    exit 1
fi

export CLOUD_STORAGE_MYSQL_PASSWORD="$MYSQL_PASSWORD"

echo "[INFO] 正在启动服务 ..."
echo "[INFO] 按 Ctrl+C 停止服务"
echo "========================================"

cd "$SERVER_DIR"
exec ./test "$SERVER_CONF" "$LOG_CONF"
