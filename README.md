# ClaudeLight

[English](#claudelight-english)

一个基于 **ESP-32E + 共阴红绿灯模块 + BLE 蓝牙** 的桌面状态灯项目，用红绿灯模块直观显示 Claude Code Agent 的工作状态。

> A BLE-powered status light for Claude Code Agent, using ESP-32E with a common-cathode traffic-light module to visualize AI coding states.

---

## 1. 项目简介

ClaudeLight 将一个共阴红绿灯模块改造成可由电脑控制的桌面状态灯。

核心思路：

- 使用 **ESP-32E 开发板** 作为主控。
- 使用 **共阴红绿灯模块**（丝印 G/Y/R/GND）。
- 通过 **BLE 蓝牙** 接收电脑端脚本发送的状态指令。
- 结合 Claude Code Hooks，让 Agent 的工作状态自动映射到灯效。

本项目不依赖 Wi-Fi，电脑可以继续连接 5GHz 网络。ESP-32E 只负责 BLE 通信和灯效控制。

---

## 2. 效果预览

典型状态映射：

| 场景 | 模式 | 灯效 |
|---|---|---|
| 开机展示 | `demo` | 自动展示多种灯效 |
| Agent 正在分析 | `thinking` | 连贯跑马灯 |
| Agent 正在生成 | `ai` | 柔和慢速跑马灯 |
| 正在执行命令 | `busy` | 黄灯慢闪 |
| 任务成功 | `success` | 绿灯常亮 |
| 任务失败 | `error` | 红灯快闪 |
| 需要用户确认 | `alarm` | 红黄交替警灯 |
| 展示模式 | `traffic` | 模拟红绿灯 |
| 关闭 | `off` | 全灭 |

---

## 3. 硬件清单

| 类别 | 物料 | 数量 | 说明 |
|---|---|---:|---|
| 主体 | 共阴红绿灯模块 (丝印 G/Y/R/GND) | 1 个 | 淘宝搜"红绿灯模块""交通灯模块" |
| 主控 | ESP-32E 开发板 | 1 块 | USB-C 接口，建议购买已焊针版本 |
| 限流 | 220Ω 1/4w 电阻 | 3 只 | 建议买 10 只装备用 |
| 连线 | 细导线 / 飞线 | 若干 | 推荐 30AWG 硅胶线或漆包线 |
| 供电 | USB-C 数据线 | 1 条 | 必须支持数据传输 |
| 绝缘 | 热缩管 / 绝缘胶带 | 少量 | 用于保护焊点 |
| 工具 | 电烙铁、焊锡丝、镊子、剪线钳 | 若干 | 需要基础焊接工具 |
| 检测 | 万用表 | 可选 | 推荐用于确认焊点和短路 |
| 外壳 | 3D 打印外壳 | 1 套 | 见第 15 节

说明：

- 本方案使用共阴红绿灯模块（丝印 G/Y/R/GND），GPIO HIGH 点亮、LOW 熄灭。
- 每路灯建议串联 220Ω 电阻，用于保护 ESP-32E 和 LED 模块。
- 改装后使用 USB 供电。

---

## 4. 硬件接线

本项目适配的是 **共阴红绿灯模块**（丝印 G/Y/R/GND）。

接线表：

| 灯位 | 模块丝印 | ESP32 引脚 |
|---|---|---|
| 绿灯 | G | IO25 |
| 黄灯 | Y | IO26 |
| 红灯 | R | IO27 |

接线方式：

```text
ESP32 GND   -> 模块 GND
ESP32 IO25  -> 220Ω -> 模块 G = 绿灯
ESP32 IO26  -> 220Ω -> 模块 Y = 黄灯
ESP32 IO27  -> 220Ω -> 模块 R = 红灯
```

共阴模块逻辑：

```text
GPIO HIGH = 灯亮
GPIO LOW  = 灯灭
```

固件中已直接输出 PWM 值（共阴无需反转），正常使用时不需要手动关心高低电平。

---

## 5. 固件说明

固件文件：

```text
ESP32_C3_ClaudeLight/ESP32_C3_ClaudeLight.ino
```

固件特性：

- BLE 广播名：`ClaudeLight`
- 通信方式：BLE GATT 写入字符串
- 默认开机模式：`demo`
- 支持多种状态灯效
- 内置自动超时，避免灯长时间高亮

BLE 参数：

```text
Device Name: ClaudeLight
Service UUID: b8b7e001-7a6b-4f4f-9a8b-11c0ffee0001
Mode Characteristic UUID: b8b7e002-7a6b-4f4f-9a8b-11c0ffee0001
```

---

## 6. 烧录固件

### 6.1 安装 Arduino IDE

前往 Arduino 官方页面下载 Arduino IDE 2.x：

```text
https://www.arduino.cc/en/software
```

### 6.2 安装 ESP32 开发板包

打开 Arduino IDE 后：

1. 进入左侧 **Boards Manager**。
2. 搜索 `esp32`。
3. 安装 **esp32 by Espressif Systems**。
4. 安装完成后重启 Arduino IDE。

#### 中国地区加速

如果下载缓慢，可使用国内镜像源：

1. 打开 Arduino IDE → **文件** → **首选项** (macOS: **Arduino IDE** → **Preferences**)。
2. 在 **附加开发板管理器网址** 中添加以下地址：

```text
https://github.com/espressif/arduino-esp32/releases/download/package_esp32_index_cn.json
```

3. 点击确定后，重新在 Boards Manager 中搜索并安装 `esp32`。

> 也可访问 https://github.com/espressif/arduino-esp32/releases 查看最新索引地址。

### 6.3 选择开发板和端口

连接 ESP-32E 开发板后，在 Arduino IDE 中选择：

```text
Board: ESP32 Dev Module
Port: 选择带 USB 标识的串口
```

推荐设置：

| 设置项 | 建议值 |
|---|---|
| USB CDC On Boot | Enabled |
| Upload Speed | 921600 或默认值 |
| Flash Size | 4MB 或默认值 |

### 6.4 上传固件

1. 用 Arduino IDE 打开 `.ino` 文件。
2. 确认 Board 和 Port。
3. 点击左上角 **Upload** 按钮。

如果出现 `Connecting...` 后失败，可尝试：

```text
按住 BOOT -> 点击 Upload -> 开始 Writing 后松开 BOOT
```

---

## 7. 安装控制脚本

### Windows

```powershell
# 运行安装脚本
.\scripts_node\install.ps1
```

### macOS / Linux

```bash
# 运行安装脚本
chmod +x scripts_node/install.sh
./scripts_node/install.sh
```

### 手动安装

1. 安装 Node.js 18+ 和 npm。

2. 安装依赖：

```bash
cd scripts_node
npm install
```

3. 复制脚本到 Claude Code 配置目录：

```bash
# Windows
mkdir %USERPROFILE%\.claude\claude-light
copy scripts_node\package.json %USERPROFILE%\.claude\claude-light\
copy scripts_node\claude_light_ble.js %USERPROFILE%\.claude\claude-light\
copy scripts_node\claude_light_hook.js %USERPROFILE%\.claude\claude-light\

# macOS/Linux
mkdir -p ~/.claude/claude-light
cp scripts_node/package.json ~/.claude/claude-light/
cp scripts_node/claude_light_ble.js ~/.claude/claude-light/
cp scripts_node/claude_light_hook.js ~/.claude/claude-light/
```

4. 在安装目录运行 `npm install --production` 安装依赖。

5. 配置 Claude Code hooks：

将 `scripts_node/settings.json.snippet` 的内容合并到 `~/.claude/settings.json`。

---

## 8. 手动测试

安装完成后，可以手动测试灯效：

```bash
# Windows
node ~/.claude/claude-light/claude_light_ble.js demo
node ~/.claude/claude-light/claude_light_ble.js thinking
node ~/.claude/claude-light/claude_light_ble.js success
node ~/.claude/claude-light/claude_light_ble.js error
node ~/.claude/claude-light/claude_light_ble.js alarm

# macOS/Linux
node ~/.claude/claude-light/claude_light_ble.js demo
node ~/.claude/claude-light/claude_light_ble.js thinking
node ~/.claude/claude-light/claude_light_ble.js success
```

BLE 诊断：

```bash
# 扫描附近所有 BLE 设备 (用于排查找不到设备的问题)
node ~/.claude/claude-light/claude_light_ble.js --scan
```

---

## 9. Claude Code Hooks 集成

ClaudeLight 通过 Claude Code 的 hooks 系统自动工作。

### 支持的事件

| Claude Code 事件 | 灯效模式 | 说明 |
|---|---|---|
| `PreToolUse` | `busy` | Agent 准备调用工具 |
| `PostToolUse` | `success` | 工具调用完成 |
| `Notification` | `alarm` | 需要用户确认权限 |
| `Stop` | `success` | Agent 完成任务 |
| `SubagentStop` | `success` | 子 Agent 完成任务 |

### 状态映射

| 场景 | 灯效 |
|---|---|
| Agent 开始分析 | 黄灯慢闪 |
| Agent 正在生成代码 | 柔和跑马灯 |
| 执行命令 / 构建 / 测试 | 黄灯慢闪 |
| 任务成功 | 绿灯常亮 |
| 任务失败 | 红灯快闪 |
| 需要用户确认权限 | 红黄交替警灯 |

### 自动工作流程

```text
普通任务：thinking -> busy -> success
等待确认：thinking -> alarm
严重异常：busy -> error
```

---

## 10. 固件模式

| mode | 灯效说明 | 典型用途 |
|---|---|---|
| `demo` | 默认开机展示，循环展示多种灯效 | 演示、待机展示 |
| `thinking` | 连贯跑马灯：L1 绿 -> L2 黄 -> L3 红 | Agent 分析、规划中 |
| `ai` | 柔和慢速跑马灯 | Agent 生成内容、长任务处理中 |
| `busy` | 黄灯慢闪 | 构建、测试、安装依赖 |
| `success` | 绿灯常亮 | 任务成功 |
| `error` | 红灯快闪 | 普通失败或报错 |
| `alarm` | 红黄交替警灯，带短渐变 | 需要用户确认 / 严重异常 |
| `traffic` | 红灯闪变绿，绿灯闪变黄，循环 | 展示或自动过渡 |
| `off` | 全部关闭 | 关闭灯效 |
| `red` | 红灯常亮 | 单灯测试 |
| `yellow` | 黄灯常亮 | 等待人工处理 / 单灯测试 |
| `green` | 绿灯常亮 | 空闲 / 单灯测试 |

---

## 11. 自动超时规则

固件内置自动超时，避免状态灯长时间保持高亮。

| 当前模式 | 自动行为 |
|---|---|
| `demo` / `thinking` / `ai` / `busy` / `success` / `error` / `alarm` / `red` / `yellow` / `green` | 最多运行 5 分钟，然后自动进入 `traffic` |
| `traffic` | 最多运行 10 分钟，然后自动 `off` |
| `off` | 不自动切换 |

---

## 12. 日志与调试

日志文件位置：

```text
~/.claude/claude-light/claude_light.log
```

查看日志：

```bash
# Windows
Get-Content "$env:USERPROFILE\.claude\claude-light\claude_light.log" -Wait

# macOS/Linux
tail -f ~/.claude/claude-light/claude_light.log
```

常见问题：

| 现象 | 优先检查 |
|---|---|
| 找不到设备 | ESP-32E 是否供电、BLE 广播名是否为 ClaudeLight、距离是否过远、系统蓝牙是否开启；尝试 `--scan` 诊断 |
| 写入失败 | GATT UUID 是否一致 |
| Claude Code 没有触发 | settings.json 是否正确配置，是否重启 Claude Code |
| 灯色不对应 | 确认 IO25=G 绿，IO26=Y 黄，IO27=R 红 |

---

## 13. 常见问题

### Arduino IDE 搜不到 ESP32 Dev Module

确认已安装：

```text
esp32 by Espressif Systems
```

安装后重启 Arduino IDE。

### 上传失败

尝试：

```text
按住 BOOT -> 点击 Upload -> 开始 Writing 后松开 BOOT
```

同时关闭 Serial Monitor，避免串口被占用。

### 找不到 BLE 设备 ClaudeLight

检查：

- ESP-32E 是否已通电。
- 固件是否已成功运行。
- BLE 名称是否为 `ClaudeLight`。
- 电脑蓝牙是否打开。
- 运行 `--scan` 诊断命令查看附近所有 BLE 设备及匹配状态。

### Windows 蓝牙权限问题

Windows 可能需要在设置中允许应用访问蓝牙：

```text
设置 -> 隐私 -> 蓝牙 -> 允许应用访问蓝牙
```

---

## 14. 卸载

Windows PowerShell：

```powershell
Remove-Item -Recurse -Force "$env:USERPROFILE\.claude\claude-light"
```

macOS / Linux：

```bash
rm -rf ~/.claude/claude-light
```

然后从 `~/.claude/settings.json` 中删除 ClaudeLight 相关的 hooks 配置。

---

## 15. 3D 打印外壳

项目提供了一个 OpenSCAD 参数化外壳模型，可将 ESP-32E 开发板和红绿灯模块组装在一起。

文件位置：

```text
enclosure/claude_light_case.scad
```

外壳为两件式设计：

| 部件 | 说明 |
|---|---|
| 底托 (tray) | 容纳 ESP-32E 开发板，四角贴墙 1/4 圆柱支撑 PCB，USB-C 侧面开口 |
| 顶板 (cap) | 实心平板，中央插槽夹持红绿灯模块，弹片卡扣固定 |

使用方法：

1. 用 OpenSCAD 打开 `claude_light_case.scad`。
2. 修改 `PART` 变量切换渲染部件：`"tray"` / `"cap"` / `"exploded"` / `"all"`。
3. 分别导出 STL 文件后 3D 打印。
4. 将 ESP-32E 放入底托，红绿灯模块插入顶板插槽，顶板盖在底托上即可。

关键参数（在文件顶部修改）：

| 参数 | 默认值 | 说明 |
|---|---|---|
| `WALL` | 1.2 | 壁厚 (mm) |
| `TRAY_TARGET_H` | 30 | 底托目标高度 (mm) |
| `SLOT_D` | 3.3 | 插槽缝隙宽度 (mm) |
| `CORNER_PAD_R` | 2 | 四角支撑圆柱半径 (mm) |

---

## 16. 参考链接

- Arduino IDE 下载页：`https://www.arduino.cc/en/software`
- Arduino-ESP32 安装文档：`https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html`
- Claude Code Hooks 文档：`https://docs.anthropic.com/en/docs/claude-code/hooks`


---

# ClaudeLight (English)

[中文](#claudelight)

A desktop status light project based on **ESP-32E + Common-Cathode Traffic-Light Module + BLE Bluetooth**. It uses a traffic-light module to visually indicate Claude Code Agent states.

> A BLE-powered status light for Claude Code Agent, using ESP-32E with a common-cathode traffic-light module to visualize AI coding states.

---

## 1. Project Overview

ClaudeLight uses a common-cathode traffic-light module (silkscreen: G/Y/R/GND) as a computer-controlled status lamp.

Core ideas:

- Use **ESP-32E dev board** as the main controller.
- Use a **common-cathode traffic-light module** (silkscreen: G/Y/R/GND).
- Receive status commands from a host script over **BLE Bluetooth**.
- Integrate with Claude Code Hooks so Agent activity maps automatically to light patterns.

---

## 2. Preview

| Scenario | Mode | Effect |
|---|---|---|
| Power-on demo | `demo` | Cycles through multiple effects |
| Agent analyzing | `thinking` | Smooth chasing lights |
| Agent generating | `ai` | Soft, slow chasing lights |
| Running commands | `busy` | Yellow slow blink |
| Task succeeded | `success` | Green solid |
| Task failed | `error` | Red fast blink |
| Waiting for user | `alarm` | Alternating red/yellow warning |
| Display mode | `traffic` | Simulated traffic light |
| Off | `off` | All off |

---

## 3. Hardware Bill of Materials

| Category | Item | Qty | Notes |
|---|---|---:|---|
| Body | Common-cathode traffic-light module (G/Y/R/GND) | 1 | Search Taobao for "traffic light module" |
| MCU | ESP-32E dev board | 1 | USB-C, pre-soldered headers recommended |
| Current limit | 220Ω resistors | 3 | Buy ~10 for spares |
| Wire | Thin wire / jumper wire | some | 30 AWG silicone recommended |
| Power | USB-C data cable | 1 | Must support data |
| Tools | Soldering iron, solder, tweezers | — | Basic soldering kit |
| Enclosure | 3D printed case | 1 set | See 3D Enclosure section

---

## 4. Wiring

This project uses a **common-cathode traffic-light module** (silkscreen: G/Y/R/GND).

| Silkscreen | Color | ESP32 pin |
|---|---|---|
| G | Green | IO25 |
| Y | Yellow | IO26 |
| R | Red | IO27 |

```text
ESP32 GND   -> Module GND
ESP32 IO25  -> 220Ω -> Module G = green
ESP32 IO26  -> 220Ω -> Module Y = yellow
ESP32 IO27  -> 220Ω -> Module R = red
```

Common-cathode logic:

```text
GPIO HIGH = LED on
GPIO LOW  = LED off
```

---

## 5. Installation

### Windows

```powershell
.\scripts_node\install.ps1
```

### macOS / Linux

```bash
chmod +x scripts_node/install.sh
./scripts_node/install.sh
```

### Manual

```bash
# Install Node.js 18+, then:
cd scripts_node
npm install
# Copy files to ~/.claude/claude-light/
# Merge settings.json.snippet into ~/.claude/settings.json
```

---

## 6. Manual Test

```bash
node ~/.claude/claude-light/claude_light_ble.js demo
node ~/.claude/claude-light/claude_light_ble.js thinking
node ~/.claude/claude-light/claude_light_ble.js success

# BLE diagnostic scan
node ~/.claude/claude-light/claude_light_ble.js --scan
```

---

## 7. Claude Code Hooks Integration

ClaudeLight works automatically through Claude Code's hooks system.

| Claude Code Event | Light Mode | Description |
|---|---|---|
| `PreToolUse` | `busy` | Agent preparing to use tool |
| `PostToolUse` | `success` | Tool use completed |
| `Notification` | `alarm` | User confirmation needed |
| `Stop` | `success` | Agent finished |

---

## 8. References

- Arduino IDE: `https://www.arduino.cc/en/software`
- Arduino-ESP32: `https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html`
- Claude Code Hooks: `https://docs.anthropic.com/en/docs/claude-code/hooks`

