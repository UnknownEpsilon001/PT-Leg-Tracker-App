# Dev launcher: starts the FastAPI server (new window) and the mock ESP32
# (this window, interactive: "b" = button, "q" = quit). Stopping the mock
# (or Ctrl+C) also stops the server.
#
# Usage, from anywhere:
#   .\server\scripts\dev.ps1            # server + mock
#   .\server\scripts\dev.ps1 -NoMock    # server only (real device instead)
#   .\server\scripts\dev.ps1 -Port 9000
param(
    [switch]$NoMock,
    [int]$Port = 8000
)

$serverDir = Split-Path $PSScriptRoot -Parent
$python = Join-Path $serverDir '.venv\Scripts\python.exe'
if (-not (Test-Path $python)) {
    Write-Error "venv not found at $python - run setup from server/README.md first"
    exit 1
}

$server = Start-Process -FilePath $python `
    -ArgumentList '-m', 'uvicorn', 'app.main:app', '--host', '0.0.0.0', '--port', $Port `
    -WorkingDirectory $serverDir -PassThru

try {
    $base = "http://localhost:$Port"
    $up = $false
    foreach ($i in 1..30) {
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
        Write-Error 'server did not come up - check the uvicorn window for errors'
        exit 1
    }

    Write-Host "server up: $base" -ForegroundColor Green
    Get-NetIPAddress -AddressFamily IPv4 |
        Where-Object { $_.IPAddress -like '192.168.*' } |
        ForEach-Object { Write-Host "phone URL: http://$($_.IPAddress):$Port" -ForegroundColor Cyan }

    if ($NoMock) {
        Write-Host 'server only (-NoMock) - Ctrl+C or close this window to stop'
        Wait-Process -Id $server.Id
    } else {
        node (Join-Path $PSScriptRoot 'mock-esp32.mjs') $base
    }
} finally {
    if (-not $server.HasExited) { Stop-Process -Id $server.Id -Force }
    Write-Host 'server stopped'
}
