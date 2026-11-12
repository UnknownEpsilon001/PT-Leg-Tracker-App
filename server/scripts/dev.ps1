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
function Start-ServerProcess {
    $stamp = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss-fff'
    $logFile = Join-Path $logDir "server-$stamp.log"
    $outFile = Join-Path $logDir "server-$stamp.out.log"
    $process = Start-Process -FilePath $python `
        -ArgumentList '-m', 'uvicorn', 'app.main:app', '--host', '0.0.0.0', '--port', $Port `
        -WorkingDirectory $serverDir -WindowStyle Hidden -PassThru `
        -RedirectStandardError $logFile -RedirectStandardOutput $outFile
    return [pscustomobject]@{
        Process = $process
        LogFile = $logFile
        OutFile = $outFile
        Started = Get-Date
    }
}

function Show-Log($run, [int]$lines) {
    foreach ($f in $run.LogFile, $run.OutFile) {
        if (Test-Path $f) { Get-Content $f -Tail $lines | ForEach-Object { Write-Host $_ } }
    }
}

function Wait-ForServer($run, [string]$base) {
    $up = $false
    # 60 s, not 15: a slow PC or a first start after an update can take longer.
    foreach ($i in 1..120) {
        Start-Sleep -Milliseconds 500
        if ($run.Process.HasExited) { break }
        try {
            Invoke-RestMethod "$base/api/sessions/current" -TimeoutSec 2 | Out-Null
            $up = $true
            break
        } catch {
            # Keep waiting: a health check is healthy only when this route succeeds.
        }
    }
    return $up
}

function Show-Addresses([string]$base, [string]$logFile) {
    Write-Host "server up: $base" -ForegroundColor Green
    Write-Host "log file: $logFile"
    # Device (ESP32) URL: this PC's LAN address, on the same Wi-Fi as the machine.
    # App URL: the Tailscale address works from anywhere the phone runs Tailscale.
    # Only adapters with a gateway: that skips VirtualBox and Hyper-V adapters,
    # whose addresses look like a LAN but reach nothing.
    Get-NetIPConfiguration -ErrorAction SilentlyContinue 2>$null | Where-Object { $_.IPv4DefaultGateway } |
        ForEach-Object { $_.IPv4Address.IPAddress } |
        ForEach-Object { Write-Host "device + phone on this Wi-Fi: http://${_}:$Port" -ForegroundColor Cyan }
    Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue 2>$null |
        Where-Object { $_.IPAddress -like '100.*' -and $_.InterfaceAlias -match 'Tailscale' } |
        ForEach-Object { Write-Host "phone over Tailscale: http://$($_.IPAddress):$Port" -ForegroundColor Cyan }
}

$base = "http://127.0.0.1:$Port"
$run = $null
$shortFailures = 0

try {
    while ($true) {
        $run = Start-ServerProcess
        if (-not (Wait-ForServer $run $base)) {
            Write-Host 'server did not come up. Its last output:' -ForegroundColor Red
            Show-Log $run 30
            if (-not $run.Process.HasExited) { Stop-Process -Id $run.Process.Id -Force }
            $shortFailures++
            if ($shortFailures -ge 5) {
                Stop-WithMessage "server failed 5 times; latest log: $($run.LogFile)"
            }
            $delay = [Math]::Min(2 * $shortFailures, 10)
            Write-Host "restarting server in $delay seconds..." -ForegroundColor Yellow
            Start-Sleep -Seconds $delay
            continue
        }

        Show-Addresses $base $run.LogFile

        if (-not $NoMock) {
            node (Join-Path $PSScriptRoot 'mock-esp32.mjs') $base
            break
        }

        Write-Host 'server only (-NoMock) - Ctrl+C or close this window to stop'
        Write-Host 'watchdog checks 127.0.0.1 every 10 seconds and restarts after 3 failed checks.' -ForegroundColor DarkGray
        # Echo the server's log here as it grows, and supervise the hidden process.
        $seen = 0
        $healthFailures = 0
        $nextHealth = (Get-Date).AddSeconds(10)
        $nextAlive = (Get-Date).AddMinutes(1)
        while (-not $run.Process.HasExited) {
            Start-Sleep -Seconds 1
            if (Test-Path $run.LogFile) {
                $all = @(Get-Content $run.LogFile)
                if ($all.Count -gt $seen) {
                    $all[$seen..($all.Count - 1)] | ForEach-Object { Write-Host $_ }
                    $seen = $all.Count
                }
            }

            $now = Get-Date
            if ($now -ge $nextHealth) {
                try {
                    Invoke-RestMethod "$base/api/sessions/current" -TimeoutSec 2 | Out-Null
                    $healthFailures = 0
                } catch {
                    $healthFailures++
                    Write-Host "watchdog health check failed ($healthFailures/3)" -ForegroundColor Yellow
                    if ($healthFailures -ge 3) {
                        Write-Host 'server is unresponsive; restarting it now.' -ForegroundColor Red
                        Stop-Process -Id $run.Process.Id -Force
                        $run.Process.WaitForExit(5000) | Out-Null
                        break
                    }
                }
                $nextHealth = $now.AddSeconds(10)
            }
            if ($now -ge $nextAlive) {
                Write-Host "[$($now.ToString('HH:mm:ss'))] server alive (PID $($run.Process.Id))" -ForegroundColor DarkGray
                $nextAlive = $now.AddMinutes(1)
            }
        }

        $run.Process.Refresh()
        Write-Host "server exited with code $($run.Process.ExitCode). Its last output:" -ForegroundColor Red
        Show-Log $run 30
        Write-Host "full log: $($run.LogFile)"

        $runtime = ((Get-Date) - $run.Started).TotalSeconds
        if ($runtime -ge 60) { $shortFailures = 0 } else { $shortFailures++ }
        if ($shortFailures -ge 5) {
            Stop-WithMessage "server stopped 5 times in quick succession; latest log: $($run.LogFile)"
        }
        $delay = [Math]::Min(2 * [Math]::Max($shortFailures, 1), 10)
        Write-Host "restarting server in $delay seconds..." -ForegroundColor Yellow
        Start-Sleep -Seconds $delay
    }
} finally {
    if ($run -and -not $run.Process.HasExited) { Stop-Process -Id $run.Process.Id -Force }
    Write-Host 'server stopped'
}
