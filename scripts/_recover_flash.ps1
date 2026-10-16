$esptool = 'C:\Users\Jinzhou\AppData\Local\Arduino15\packages\esp32\tools\esptool_py\5.3.0\esptool.exe'
$port   = 'COM15'
$merged = 'F:\Coding\claude-light\build_out\ESP32_C3_ClaudeLight.ino.merged.bin'

# free the port from Arduino helpers
foreach ($n in @('serial-monitor','serial-discovery')) {
    Get-Process -Name $n -ErrorAction SilentlyContinue | ForEach-Object {
        Write-Host ('stopping ' + $_.ProcessName + ' PID=' + $_.Id)
        Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue
    }
}
Start-Sleep -Seconds 2

Write-Host '--- esptool --no-stub write_flash (full 4MB image at 0x0) ---'
& $esptool --chip esp32c3 --port $port --no-stub --before default_reset --after hard_reset `
    write_flash --flash_size 4MB 0x0 $merged

Write-Host ('exit code: ' + $LASTEXITCODE)
