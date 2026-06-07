# ClaudeLight 安装脚本 (Windows PowerShell) - Node.js 版

Write-Host "=== ClaudeLight 安装脚本 (Node.js 版) ===" -ForegroundColor Cyan
Write-Host ""

# 创建目录
$installDir = "$env:USERPROFILE\.claude\claude-light"
Write-Host "创建安装目录: $installDir"
New-Item -ItemType Directory -Force -Path $installDir | Out-Null

# 复制文件
Write-Host "复制文件..."
Copy-Item "$PSScriptRoot\package.json" "$installDir\" -Force
Copy-Item "$PSScriptRoot\claude_light_ble.js" "$installDir\" -Force
Copy-Item "$PSScriptRoot\claude_light_hook.js" "$installDir\" -Force

# 检查 Node.js
Write-Host ""
Write-Host "检查 Node.js..."
try {
    $nodeVersion = node --version 2>&1
    Write-Host "Node.js: $nodeVersion" -ForegroundColor Green
} catch {
    Write-Host "错误: 未找到 Node.js，请先安装 Node.js 18+" -ForegroundColor Red
    exit 1
}

# 安装依赖
Write-Host ""
Write-Host "安装 Node.js 依赖..."
Push-Location $installDir
npm install --production
Pop-Location

# 配置 Claude Code hooks
Write-Host ""
Write-Host "配置 Claude Code hooks..."
$settingsFile = "$env:USERPROFILE\.claude\settings.json"

if (Test-Path $settingsFile) {
    Write-Host "警告: settings.json 已存在" -ForegroundColor Yellow
    Write-Host "请手动合并以下配置到 $settingsFile" -ForegroundColor Yellow
    Write-Host ""
    Get-Content "$PSScriptRoot\settings.json.snippet"
} else {
    Copy-Item "$PSScriptRoot\settings.json.snippet" $settingsFile -Force
    Write-Host "已创建 settings.json" -ForegroundColor Green
}

# 测试
Write-Host ""
Write-Host "=== 安装完成 ===" -ForegroundColor Green
Write-Host ""
Write-Host "请先将固件烧录到 ESP32，然后运行以下命令测试:" -ForegroundColor Cyan
Write-Host "  node $installDir\claude_light_ble.js green"
Write-Host "  node $installDir\claude_light_ble.js thinking"
Write-Host "  node $installDir\claude_light_ble.js success"
Write-Host ""
