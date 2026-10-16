# ClaudeLight 固件烧录脚本 (Windows PowerShell)
#
# 功能:
#   1. 自动检测是否有 ESP32 通过 USB 接入 (按常见 USB 桥芯片 VID 识别)
#   2. 若有接入, 提示是否烧录
#   3. 用户确认后, 用 arduino-cli 编译并向该 ESP32 烧录 ClaudeLight 固件
#   4. 烧录结束后发出提醒(蜂鸣 + 气泡通知), 然后自动回到检测状态继续监视
#
# 前置要求:
#   - 已安装 arduino-cli  (下载: https://arduino.github.io/arduino-cli/install/)
#   - 已安装 esp32 core   (arduino-cli core install esp32:esp32)
#
# 用法:
#   .\flash.ps1                 # 持续监视: 检测到接入 -> 确认 -> 烧录 -> 提醒 -> 回到检测
#   .\flash.ps1 -Once           # 只检测一次, 烧录后退出
#   .\flash.ps1 -Port COM5      # 跳过检测, 直接指定串口烧录一次
#   .\flash.ps1 -ListPorts      # 仅列出所有串口及其设备名(排查用)
#   .\flash.ps1 -Fqbn "esp32:esp32:esp32"   # 指定其它开发板

param(
    # arduino-cli 可执行文件路径。留空则自动查找仓内 arduino-cli.exe, 再回退到 PATH
    [string]$ArduinoCli,

    # 开发板 FQBN。ESP32-C3 SuperMini 用 esp32:esp32:esp32c3 (ESP32C3 Dev Module)
    [string]$Fqbn = "esp32:esp32:esp32c3",

    # 待烧录的 sketch 路径 (默认指向仓库内的固件)
    [string]$Sketch = (Join-Path $PSScriptRoot "..\ESP32_C3_ClaudeLight\ESP32_C3_ClaudeLight.ino"),

    # 编译时的板卡菜单选项, 与 README 推荐设置一致
    [string[]]$BuildProperty = @(
        "USB CDC On Boot=Enabled"
    ),

    # 手动指定串口 (如 COM5), 指定后跳过自动检测
    [string]$Port,

    # 只检测/烧录一次后退出 (默认为持续监视循环)
    [switch]$Once,

    # 仅列出所有串口设备名后退出
    [switch]$ListPorts
)

# 常见 ESP32 开发板的 USB 串口桥芯片 VID
#   303A = Espressif (C3/S3 原生 USB)
#   1A86 = WCH CH340/CH343
#   10AC = Silicon Labs CP2102/CP2102N
#   0403 = FTDI FT232
$EspVids = @("VID_303A", "VID_1A86", "VID_10AC", "VID_0403")

# =====================================================
# 工具函数
# =====================================================

function Test-ArduinoCli {
    if ($ArduinoCli -eq "arduino-cli") {
        if (-not (Get-Command arduino-cli -ErrorAction SilentlyContinue)) {
            Write-Host "错误: 未找到 arduino-cli (PATH 中或未指定 -ArduinoCli)。" -ForegroundColor Red
            Write-Host "请传入 -ArduinoCli \"C:\path\to\arduino-cli.exe\", 或将 arduino-cli 加入 PATH。" -ForegroundColor Yellow
            return $false
        }
    } else {
        if (-not (Test-Path $ArduinoCli)) {
            Write-Host "错误: 找不到 arduino-cli: $ArduinoCli" -ForegroundColor Red
            return $false
        }
    }
    return $true
}

function Test-Esp32Core {
    $list = (& $ArduinoCli core list) 2>&1 | Out-String
    if ($list -notmatch "(?m)^esp32\s+esp32:esp32") {
        Write-Host "警告: 似乎尚未安装 esp32 core。" -ForegroundColor Yellow
        Write-Host "如编译失败, 请执行: arduino-cli core install esp32:esp32" -ForegroundColor Yellow
    }
}

# 返回形如 @{Port=..; Vid=..; Device=..} 的候选 ESP32 串口列表
function Get-EspCandidatePorts {
    $found = @()
    foreach ($p in [System.IO.Ports.SerialPort]::GetPortNames()) {
        $cim = Get-CimInstance Win32_PnPEntity -Filter "Name like '%($p)'" -ErrorAction SilentlyContinue
        if (-not $cim) { continue }
        $devId = [string]$cim.DeviceID
        $hitVid = $null
        foreach ($v in $EspVids) {
            if ($devId -match $v) { $hitVid = $v; break }
        }
        if ($hitVid) {
            $found += [pscustomobject]@{
                Port   = $p
                Vid    = $hitVid
                Device = [string]$cim.Name
            }
        }
    }
    return $found
}

function Show-AllPorts {
    $ports = [System.IO.Ports.SerialPort]::GetPortNames()
    if (-not $ports) {
        Write-Host "当前系统没有检测到任何串口。" -ForegroundColor Yellow
        return
    }
    Write-Host "当前所有串口:" -ForegroundColor Cyan
    foreach ($p in $ports) {
        $cim = Get-CimInstance Win32_PnPEntity -Filter "Name like '%($p)'" -ErrorAction SilentlyContinue
        $name = if ($cim) { $cim.Name } else { "(未知设备)" }
        $mark = ""
        if ($cim) {
            foreach ($v in $EspVids) {
                if ([string]$cim.DeviceID -match $v) { $mark = "  <== 疑似 ESP32"; break }
            }
        }
        Write-Host ("  {0,-8} {1}{2}" -f $p, $name, $mark)
    }
}

function Invoke-Flash([string]$targetPort) {
    if (-not (Test-Path $Sketch)) {
        Write-Host "错误: 找不到 sketch: $Sketch" -ForegroundColor Red
        return 1
    }

    Write-Host ""
    Write-Host "开始编译并烧录到 $targetPort ..." -ForegroundColor Cyan
    Write-Host "  FQBN  : $Fqbn"
    Write-Host "  Sketch: $Sketch"

    $cliArgs = @("compile", "--fqbn", $Fqbn)
    foreach ($bp in $BuildProperty) {
        $cliArgs += @("--build-property", $bp)
    }
    $cliArgs += @("--warnings", "none",
                  "--upload", "--port", $targetPort, "--verbose",
                  $Sketch)

    # 拼成命令行参数 (含空格的项自动加引号)
    $argStr = ($cliArgs | ForEach-Object {
        if ($_ -match '[\s"]') { '"' + ($_ -replace '"', '\"') + '"' } else { $_ }
    }) -join ' '

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $ArduinoCli
    $psi.Arguments = $argStr
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    try {
        $psi.StandardOutputEncoding = [System.Text.Encoding]::UTF8
        $psi.StandardErrorEncoding = [System.Text.Encoding]::UTF8
    } catch { }

    # 共享状态 (事件线程写入, 主轮询线程读取)
    $global:FlashState = [hashtable]::Synchronized(@{
        phase = '准备中'; pct = 0; last = ''; start = (Get-Date)
        out = New-Object System.Collections.ArrayList
    })

    $proc = [System.Diagnostics.Process]::Start($psi)

    $outAction = {
        $line = $EventArgs.Data
        if ($null -eq $line) { return }
        $st = $global:FlashState
        [void]$st.out.Add($line)
        $t = $line.Trim()
        if ($t) { $st.last = $t }
        if ($t -match '(?i)detecting|resolving|libraries|Looking') { $st.phase = '检测/解析库'; $st.pct = [Math]::Max($st.pct, 8) }
        elseif ($t -match '(?i)compil|\.cpp|\.c |\.S ') { $st.phase = '编译中'; $st.pct = [Math]::Max($st.pct, 20) }
        elseif ($t -match '(?i)linking|\.elf') { $st.phase = '链接中'; $st.pct = [Math]::Max($st.pct, 80) }
        elseif ($t -match '(?i)upload|esptool|Connecting|Writing|Hash|Leaving|Hard resetting') { $st.phase = '上传中' }
        # 解析 esptool 形如 "37.5%" 的百分比 -> 映射到 82~99
        if ($t -match '(\d{1,3}(?:\.\d+)?)\s*%') {
            $p = [double]$Matches[1]
            if ($p -ge 0 -and $p -le 100) {
                $st.pct = [Math]::Max($st.pct, 82 + [int]($p * 0.17))
                $st.phase = '上传中'
            }
        }
    }
    $errAction = {
        $line = $EventArgs.Data
        if ($null -eq $line) { return }
        $st = $global:FlashState
        [void]$st.out.Add($line)
        $t = $line.Trim(); if ($t) { $st.last = $t }
    }

    $evOut = Register-ObjectEvent -InputObject $proc -EventName OutputDataReceived -Action $outAction
    $evErr = Register-ObjectEvent -InputObject $proc -EventName ErrorDataReceived -Action $errAction
    $proc.BeginOutputReadLine()
    $proc.BeginErrorReadLine()

    $i = 0
    while (-not $proc.WaitForExit(150)) {
        $st = $global:FlashState
        $elapsed = [int]((Get-Date) - $st.start).TotalSeconds
        $i += 1
        if ($st.pct -lt 80) {
            # 编译阶段无明确总进度, 用 20~77 往复的跑马灯 + 计时
            $dispPct = 20 + (($i * 2) % 58)
            $activity = "编译中... 已用时 ${elapsed}s"
        } else {
            $dispPct = [Math]::Min(99, $st.pct)
            $activity = "{0} {1}%" -f $st.phase, $dispPct
        }
        $short = $st.last
        if ($short.Length -gt 70) { $short = $short.Substring(0, 67) + '...' }
        Write-Progress -Activity "ClaudeLight 烧录 -> $targetPort" `
                      -Status ("[{0} | {1}s] {2}" -f $st.phase, $elapsed, $short) `
                      -PercentComplete $dispPct
    }

    $proc.WaitForExit()          # 等待异步输出读完
    Start-Sleep -Milliseconds 200
    Write-Progress -Activity "ClaudeLight 烧录 -> $targetPort" -Completed
    Unregister-Event -SourceIdentifier $evOut.Name -ErrorAction SilentlyContinue
    Unregister-Event -SourceIdentifier $evErr.Name -ErrorAction SilentlyContinue

    $code = $proc.ExitCode
    $allLines = @($global:FlashState.out)
    $proc.Dispose()

    if ($code -eq 0) {
        # 展示 RAM/Flash 占用摘要
        $allLines | Where-Object { $_ -match 'uses|Sketch uses|Global variables' } |
            ForEach-Object { Write-Host ("  " + $_.Trim()) -ForegroundColor DarkGray }
        Write-Host ""
        Write-Host "烧录成功! 设备将自动重启。" -ForegroundColor Green
    } else {
        Write-Host ""
        Write-Host "----- 构建输出 (末尾 40 行) -----" -ForegroundColor DarkGray
        ($allLines | Select-Object -Last 40) | ForEach-Object { Write-Host $_ }
        Write-Host ""
        Write-Host "烧录失败 (退出码 $code)。可尝试:" -ForegroundColor Red
        Write-Host "  - 按住板上 BOOT 键 -> 点按 RESET -> 松开 BOOT 进入下载模式后重试"
        Write-Host "  - 用 -ListPorts 核对串口号, 或用 -Port COMx 手动指定"
        Write-Host "  - 确认 FQBN 是否与你的开发板一致 (当前: $Fqbn)"
    }
    return $code
}

# 烧录结束后的提醒: 蜂鸣 + Windows 气泡通知
function Play-Alert([bool]$ok) {
    try {
        if ($ok) {
            [Console]::Beep(880, 150); [Console]::Beep(1175, 220)
        } else {
            [Console]::Beep(440, 250); [Console]::Beep(300, 380)
        }
    } catch { }
}

function Show-Balloon([string]$title, [string]$text, [bool]$ok) {
    try {
        Add-Type -AssemblyName System.Windows.Forms -ErrorAction SilentlyContinue
        Add-Type -AssemblyName System.Drawing -ErrorAction SilentlyContinue
        $ni = New-Object System.Windows.Forms.NotifyIcon
        $ni.Icon = [System.Drawing.SystemIcons]::Application
        $ni.Visible = $true
        $icon = if ($ok) { [System.Windows.Forms.ToolTipIcon]::Info } else { [System.Windows.Forms.ToolTipIcon]::Error }
        $ni.ShowBalloonTip(6000, $title, $text, $icon)
        Start-Sleep -Seconds 6
        $ni.Visible = $false
        $ni.Dispose()
    } catch { }
}

function Notify-FlashResult([string]$port, [bool]$ok) {
    $title = if ($ok) { "ClaudeLight 烧录成功" } else { "ClaudeLight 烧录失败" }
    $text = if ($ok) { "$port 烧录完成, 已回到检测状态。" } else { "$port 烧录失败, 请检查后重试。" }
    Play-Alert $ok
    Show-Balloon $title $text $ok
    $color = if ($ok) { "Green" } else { "Red" }
    Write-Host ""
    Write-Host ">>> $title ($port) <<<" -ForegroundColor $color
}

# 轮询直到出现至少一个疑似 ESP32 (阻塞); $block=$false 时只查一次
function Wait-ForEsp32([bool]$block) {
    while ($true) {
        $c = @(Get-EspCandidatePorts)
        if ($c.Count -gt 0) { return $c }
        if (-not $block) { return $c }
        Start-Sleep -Seconds 2
    }
}

# 等待指定串口从总线消失 (设备被拔出或重启离线), 用于避免对同一块板重复触发
function Wait-ForPortGone([string]$port) {
    Start-Sleep -Seconds 1
    while ($true) {
        $still = @(Get-EspCandidatePorts | Where-Object { $_.Port -eq $port })
        if ($still.Count -eq 0) { return }
        Start-Sleep -Seconds 2
    }
}

# =====================================================
# 主流程
# =====================================================

Write-Host "=== ClaudeLight 固件烧录 ===" -ForegroundColor Cyan
Write-Host ""

if ($ListPorts) {
    Show-AllPorts
    return
}

# 解析 arduino-cli 路径: 未指定则自动查找仓内 exe, 否则回退到 PATH
if (-not $ArduinoCli) {
    $candidates = @(
        (Join-Path $PSScriptRoot "..\ESP32_C3_ClaudeLight\arduino-cli.exe"),
        (Join-Path $PSScriptRoot "arduino-cli.exe"),
        (Join-Path $PSScriptRoot "arduino-cli")
    )
    foreach ($c in $candidates) {
        if (Test-Path $c) { $ArduinoCli = (Resolve-Path $c).Path; break }
    }
}
if (-not $ArduinoCli) { $ArduinoCli = "arduino-cli" }

Write-Host "arduino-cli: $ArduinoCli" -ForegroundColor DarkGray

if (-not (Test-ArduinoCli)) { return 1 }
Test-Esp32Core

# 用户已手动指定端口 -> 直接确认并烧录一次 (带提醒)
if ($Port) {
    Write-Host "已指定串口: $Port" -ForegroundColor Green
    $ans = Read-Host "是否向 $Port 烧录固件? [Y/n]"
    if ($ans -and $ans -notmatch "^[Yy]") {
        Write-Host "已取消。" -ForegroundColor Yellow
        return 0
    }
    $code = Invoke-Flash $Port
    Notify-FlashResult $Port ($code -eq 0)
    return $code
}

# 自动检测: 默认持续监视循环, -Once 则只跑一轮
$monitor = -not $Once

while ($true) {
    if ($monitor) {
        Write-Host "等待 ESP32 接入... (Ctrl+C 退出)" -ForegroundColor Cyan
        $candidates = @(Wait-ForEsp32 $true)
    } else {
        $candidates = @(Wait-ForEsp32 $false)
    }

    if ($candidates.Count -eq 0) {
        Write-Host "未检测到已接入的 ESP32。" -ForegroundColor Yellow
        Write-Host "提示: 用 .\flash.ps1 -ListPorts 查看全部串口; 或 -Port COMx 手动指定。" -ForegroundColor Yellow
        if ($monitor) { continue } else { return 2 }
    }

    Write-Host ""
    Write-Host "检测到 $($candidates.Count) 个疑似 ESP32 设备:" -ForegroundColor Green
    for ($i = 0; $i -lt $candidates.Count; $i++) {
        $c = $candidates[$i]
        Write-Host ("  [{0}] {1,-8} {2} ({3})" -f ($i + 1), $c.Port, $c.Device, $c.Vid)
    }
    Write-Host ""

    $chosenPort = $null
    if ($candidates.Count -eq 1) {
        $chosenPort = $candidates[0].Port
    } else {
        $sel = Read-Host "选择要烧录的设备编号 [1-$($candidates.Count)]"
        $idx = 0
        if ([int]::TryParse($sel, [ref]$idx) -and $idx -ge 1 -and $idx -le $candidates.Count) {
            $chosenPort = $candidates[$idx - 1].Port
        } else {
            Write-Host "无效编号, 已取消。" -ForegroundColor Red
            if ($monitor) { continue } else { return 1 }
        }
    }

    $ans = Read-Host "是否向 $chosenPort 烧录固件? [Y/n]"
    if ($ans -and $ans -notmatch "^[Yy]") {
        Write-Host "已取消。" -ForegroundColor Yellow
        if ($monitor) {
            Write-Host "等待 $chosenPort 拔出后重新检测..." -ForegroundColor Cyan
            Wait-ForPortGone $chosenPort
            continue
        } else {
            return 0
        }
    }

    $code = Invoke-Flash $chosenPort
    Notify-FlashResult $chosenPort ($code -eq 0)

    if ($monitor) {
        Write-Host "等待 $chosenPort 拔出后重新检测... (Ctrl+C 退出)" -ForegroundColor Cyan
        Wait-ForPortGone $chosenPort
    } else {
        return $code
    }
}
