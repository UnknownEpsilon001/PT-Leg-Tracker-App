# Dev launcher: starts the FastAPI server in the background and the mock ESP32
# (this window, interactive: "b" = button, "q" = quit). Stopping the mock
# (or Ctrl+C) also stops the server.
#
# Usage, from anywhere:
#   .\server\scripts\dev.ps1            # server + mock
#   .\server\scripts\dev.ps1 -NoMock    # server only (real device instead)
#   .\server\scripts\dev.ps1 -Port 9000
#
# The server's own output goes to server\logs\, so a crash leaves its reason
# behind instead of closing a window before anyone can read it.
# [TH] ข้อความของเซิร์ฟเวอร์ถูกเก็บไว้ใน server\logs\ ถ้าเซิร์ฟเวอร์พังจะยังอ่านสาเหตุได้ ไม่หายไปพร้อมหน้าต่าง
param(
    [switch]$NoMock,
    [int]$Port = 8000
)

function Stop-WithMessage([string]$message) {
    Write-Host $message -ForegroundColor Red
    exit 1
}

$serverDir = Split-Path $PSScriptRoot -Parent
$python = Join-Path $serverDir '.venv\Scripts\python.exe'
if (-not (Test-Path $python)) {
    Stop-WithMessage "venv not found at $python - run start-server.cmd, or the setup in server/README.md"
}

# A second copy fails to bind and exits at once, which used to look like a crash.
# [TH] ถ้ามีเซิร์ฟเวอร์ตัวเก่าใช้พอร์ตนี้อยู่ ตัวใหม่จะเปิดไม่ขึ้น จึงตรวจก่อน
$busy = Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue | Select-Object -First 1
if ($busy) {
    $owner = Get-Process -Id $busy.OwningProcess -ErrorAction SilentlyContinue
    Stop-WithMessage ("port $Port is already in use by $($owner.ProcessName) (PID $($busy.OwningProcess)). " +
        "An old server is probably still running: close its window, or end that process in Task Manager.")
}

$logDir = Join-Path $serverDir 'logs'
New-Item -ItemType Directory -Force $logDir | Out-Null
$stamp = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'
$logFile = Join-Path $logDir "server-$stamp.log"
$outFile = Join-Path $logDir "server-$stamp.out.log"

$server = Start-Process -FilePath $python `
    -ArgumentList '-m', 'uvicorn', 'app.main:app', '--host', '0.0.0.0', '--port', $Port `
    -WorkingDirectory $serverDir -WindowStyle Hidden -PassThru `
    -RedirectStandardError $logFile -RedirectStandardOutput $outFile

function Show-Log([int]$lines) {
    foreach ($f in $logFile, $outFile) {
        if (Test-Path $f) { Get-Content $f -Tail $lines | ForEach-Object { Write-Host $_ } }
    }
}

try {
    $base = "http://localhost:$Port"
    $up = $false
    # 60 s, not 15: a slow PC or a first start after an update can take longer.
    foreach ($i in 1..120) {
        Start-Sleep -Milliseconds 500
        if ($server.HasExited) { break }
        try {
            Invoke-RestMethod "$base/api/sessions/current" -TimeoutSec 2 | Out-Null
            $up = $true
            break
        } catch {
            if ($_.Exception.Response) { $up = $true; break }  # any HTTP reply means it's up
        }
    }
    if (-not $up) {
        Write-Host 'server did not come up. Its last output:' -ForegroundColor Red
        Show-Log 30
        Stop-WithMessage "full log: $logFile"
    }

    Write-Host "server up: $base" -ForegroundColor Green
    Write-Host "log file: $logFile"
    # Device (ESP32) URL: this PC's LAN address, on the same Wi-Fi as the machine.
    # App URL: the Tailscale address works from anywhere the phone runs Tailscale.
    # Only adapters with a gateway: that skips VirtualBox and Hyper-V adapters,
    # whose addresses look like a LAN but reach nothing.
    Get-NetIPConfiguration | Where-Object { $_.IPv4DefaultGateway } |
        ForEach-Object { $_.IPv4Address.IPAddress } |
        ForEach-Object { Write-Host "device + phone on this Wi-Fi: http://${_}:$Port" -ForegroundColor Cyan }
    Get-NetIPAddress -AddressFamily IPv4 |
        Where-Object { $_.IPAddress -like '100.*' -and $_.InterfaceAlias -match 'Tailscale' } |
        ForEach-Object { Write-Host "phone over Tailscale: http://$($_.IPAddress):$Port" -ForegroundColor Cyan }

    if ($NoMock) {
        Write-Host 'server only (-NoMock) - Ctrl+C or close this window to stop'
        # Echo the server's log here as it grows, so this window shows what it does.
        $seen = 0
        while (-not $server.HasExited) {
            Start-Sleep -Seconds 1
            if (Test-Path $logFile) {
                $all = @(Get-Content $logFile)
                if ($all.Count -gt $seen) { $all[$seen..($all.Count - 1)] | ForEach-Object { Write-Host $_ }; $seen = $all.Count }
            }
        }
        Write-Host "server exited with code $($server.ExitCode). Its last output:" -ForegroundColor Red
        Show-Log 30
        Write-Host "full log: $logFile"
    } else {
        node (Join-Path $PSScriptRoot 'mock-esp32.mjs') $base
    }
} finally {
    if (-not $server.HasExited) { Stop-Process -Id $server.Id -Force }
    Write-Host 'server stopped'
}
