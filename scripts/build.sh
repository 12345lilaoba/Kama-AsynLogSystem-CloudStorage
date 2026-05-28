#!/bin/bash
# build.sh —— 编译 Cloud Storage 服务端
# 用法: bash scripts/build.sh [debug]
#   不带参数：正常编译，输出 src/server/test
#   debug    ：Debug 编译，输出 src/server/gdb_test（含调试符号）

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
SERVER_DIR="$PROJECT_ROOT/src/server"

echo "========================================"
echo "  Cloud Storage 编译脚本"
echo "========================================"
echo "项目根目录 : $PROJECT_ROOT"
echo "服务端目录 : $SERVER_DIR"

# 检查依赖工具
check_dep() {
    if ! command -v "$1" &>/dev/null; then
        echo "[ERROR] 未找到命令: $1，请先安装相应依赖"
        exit 1
    fi
}
check_dep g++
check_dep make
check_dep mysql_config

echo "----------------------------------------"

cd "$SERVER_DIR"

if [ "${1:-}" = "debug" ]; then
    echo "[BUILD] Debug 模式（带 -g 调试符号）..."
    make gdb_test
    echo ""
    echo "[OK] Debug 编译完成：$SERVER_DIR/gdb_test"
else
    echo "[BUILD] Release 模式..."
    make
    echo ""
    echo "[OK] 编译完成：$SERVER_DIR/test"
fi
