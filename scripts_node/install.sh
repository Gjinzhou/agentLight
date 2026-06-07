#!/bin/bash
# ClaudeLight 安装脚本 (macOS/Linux) - Node.js 版

set -e

echo "=== ClaudeLight 安装脚本 (Node.js 版) ==="
echo ""

# 创建目录
INSTALL_DIR="$HOME/.claude/claude-light"
echo "创建安装目录: $INSTALL_DIR"
mkdir -p "$INSTALL_DIR"

# 复制文件
echo "复制文件..."
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cp "$SCRIPT_DIR/package.json" "$INSTALL_DIR/"
cp "$SCRIPT_DIR/claude_light_ble.js" "$INSTALL_DIR/"
cp "$SCRIPT_DIR/claude_light_hook.js" "$INSTALL_DIR/"

# 检查 Node.js
echo ""
echo "检查 Node.js..."
if command -v node &> /dev/null; then
    NODE_VERSION=$(node --version 2>&1)
    echo "Node.js: $NODE_VERSION"
else
    echo "错误: 未找到 Node.js，请先安装 Node.js 18+"
    exit 1
fi

# 安装依赖
echo ""
echo "安装 Node.js 依赖..."
cd "$INSTALL_DIR"
npm install --production
cd - > /dev/null

# 配置 Claude Code hooks
echo ""
echo "配置 Claude Code hooks..."
SETTINGS_FILE="$HOME/.claude/settings.json"

if [ -f "$SETTINGS_FILE" ]; then
    echo "警告: settings.json 已存在"
    echo "请手动合并以下配置到 $SETTINGS_FILE"
    echo ""
    cat "$SCRIPT_DIR/settings.json.snippet"
else
    cp "$SCRIPT_DIR/settings.json.snippet" "$SETTINGS_FILE"
    echo "已创建 settings.json"
fi

# 测试
echo ""
echo "=== 安装完成 ==="
echo ""
echo "请先将固件烧录到 ESP32，然后运行以下命令测试:"
echo "  node $INSTALL_DIR/claude_light_ble.js green"
echo "  node $INSTALL_DIR/claude_light_ble.js thinking"
echo "  node $INSTALL_DIR/claude_light_ble.js success"
echo ""
