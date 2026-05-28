#!/bin/bash
# setup_mysql.sh —— 初始化 Cloud Storage 所需的 MySQL 数据库和用户
# 用法: bash scripts/setup_mysql.sh [mysql_root_user] [db_password]
#   mysql_root_user 默认为 root
#   db_password     默认为 cloud123456（建议生产环境修改）

set -e

MYSQL_ROOT="${1:-root}"
DB_PASSWORD="${2:-cloud123456}"
DB_NAME="cloud_storage"
DB_USER="cloud_user"

echo "========================================"
echo "  Cloud Storage MySQL 初始化脚本"
echo "========================================"
echo "数据库名  : $DB_NAME"
echo "数据库用户: $DB_USER"
echo "----------------------------------------"
echo "请输入 MySQL $MYSQL_ROOT 用户的密码："

mysql -u "$MYSQL_ROOT" -p <<SQL
CREATE DATABASE IF NOT EXISTS \`$DB_NAME\` DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
CREATE USER IF NOT EXISTS '$DB_USER'@'localhost' IDENTIFIED BY '$DB_PASSWORD';
GRANT ALL PRIVILEGES ON \`$DB_NAME\`.* TO '$DB_USER'@'localhost';
FLUSH PRIVILEGES;
SQL

echo ""
echo "[OK] 数据库 '$DB_NAME' 和用户 '$DB_USER' 初始化完成。"
echo ""
echo "启动服务前请设置环境变量："
echo "  export CLOUD_STORAGE_MYSQL_PASSWORD='$DB_PASSWORD'"
