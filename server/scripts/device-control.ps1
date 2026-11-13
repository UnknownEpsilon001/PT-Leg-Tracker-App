# Interactive app-side controller for testing a real therapy device through the server.
# LEO WAS HERE ;3
param(
    [string]$ServerUrl = 'https://med-flex-server-audio-room.tail325fc2.ts.net',
    [string]$DeviceId = 'default'
)

$ErrorActionPreference = 'Stop'
$ServerUrl = $ServerUrl.TrimEnd('/')
$DeviceId = $DeviceId.Trim()

if (-not $ServerUrl) { throw 'ServerUrl cannot be empty.' }
if (-not $DeviceId) { throw 'DeviceId cannot be empty.' }

$encodedDeviceId = [Uri]::EscapeDataString($DeviceId)

function Get-ApiError([System.Management.Automation.ErrorRecord]$record) {
    if ($record.ErrorDetails.Message) {
        try {
            $body = $record.ErrorDetails.Message | ConvertFrom-Json
            if ($body.detail) { return [string]$body.detail }
        } catch {
            return $record.ErrorDetails.Message
        }
    }
    return $record.Exception.Message
}

function Invoke-DeviceApi(
    [string]$Path,
    [ValidateSet('GET', 'POST')][string]$Method = 'GET',
    [hashtable]$Body
) {
    $separator = if ($Path.Contains('?')) { '&' } else { '?' }
    $uri = "$ServerUrl$Path${separator}deviceId=$encodedDeviceId"
    $params = @{
        Uri        = $uri
        Method     = $Method
        TimeoutSec = 8
    }
    if ($null -ne $Body) {
        $params.ContentType = 'application/json'
        $params.Body = $Body | ConvertTo-Json -Compress
    }
    Invoke-RestMethod @params
}

function Show-Status {
    $s = Invoke-DeviceApi '/api/sessions/current'
    $online = if ($s.deviceOnline) { 'YES' } else { 'NO' }
    $target = if ($s.targetReps -eq 0) { 'unlimited' } else { [string]$s.targetReps }
    Write-Host ''
    Write-Host "State   : $($s.state)" -ForegroundColor Cyan
    Write-Host "Online  : $online"
    Write-Host "Time    : $($s.elapsedSec) sec"
    Write-Host "Reps    : $($s.reps)"
    Write-Host "Target  : $target"
    Write-Host "Session : $($s.sessionId)"
}

function Send-Start {
    $raw = Read-Host 'Target reps for this session (blank = device setting, 0 = unlimited)'
    $body = $null
    if ($raw.Trim()) {
        $target = 0
        if (-not [int]::TryParse($raw, [ref]$target) -or $target -lt 0 -or $target -gt 99) {
            throw 'Target must be 0 to 99.'
        }
        $body = @{ targetReps = $target }
    }
    Invoke-DeviceApi '/api/sessions/start' 'POST' $body | Out-Null
    Write-Host 'START queued. The device should fetch it within about one second.' -ForegroundColor Green
}

function Send-Stop {
    Invoke-DeviceApi '/api/sessions/stop' 'POST' | Out-Null
    Write-Host 'STOP queued.' -ForegroundColor Yellow
}

function Set-Target {
    $raw = Read-Host 'New target reps (0 = unlimited, 1-99 = stop automatically)'
    $target = 0
    if (-not [int]::TryParse($raw, [ref]$target) -or $target -lt 0 -or $target -gt 99) {
        throw 'Target must be 0 to 99.'
    }
    Invoke-DeviceApi '/api/sessions/target' 'POST' @{ targetReps = $target } | Out-Null
    Write-Host "Target $target queued." -ForegroundColor Green
}

function Set-Timings {
    Write-Host 'Leave a value blank to keep the current setting.'
    $fields = @{
        liftSec      = Read-Host 'Lift seconds (1-20)'
        lowerSec     = Read-Host 'Lower seconds (1-20)'
        holdSec      = Read-Host 'Hold seconds (1-60)'
        restSec      = Read-Host 'Rest seconds (1-60)'
        maxTravelSec = Read-Host 'Maximum travel seconds (1-20)'
    }
    $body = @{}
    foreach ($entry in $fields.GetEnumerator()) {
        if (-not $entry.Value.Trim()) { continue }
        $value = 0
        if (-not [int]::TryParse($entry.Value, [ref]$value)) {
            throw "$($entry.Key) must be a whole number."
        }
        $body[$entry.Key] = $value
    }
    if ($body.Count -eq 0) {
        Write-Host 'No timing changes entered.' -ForegroundColor DarkYellow
        return
    }
    Invoke-DeviceApi '/api/sessions/timings' 'POST' $body | Out-Null
    Write-Host 'Timing changes queued.' -ForegroundColor Green
}

function Watch-Status {
    Write-Host 'Watching once per second. Press Q to return to the menu.' -ForegroundColor Cyan
    while ($true) {
        try {
            $s = Invoke-DeviceApi '/api/sessions/current'
            $line = 'state={0,-8} online={1,-3} time={2,5}s reps={3,3} target={4,3}' -f `
                $s.state, $(if ($s.deviceOnline) { 'yes' } else { 'no' }), $s.elapsedSec, $s.reps, $s.targetReps
            Write-Host "`r$line" -NoNewline
        } catch {
            Write-Host "`rstatus error: $(Get-ApiError $_)" -NoNewline -ForegroundColor Red
        }
        for ($i = 0; $i -lt 10; $i++) {
            Start-Sleep -Milliseconds 100
            if ([Console]::KeyAvailable) {
                $key = [Console]::ReadKey($true)
                if ($key.Key -eq [ConsoleKey]::Q) {
                    Write-Host ''
                    return
                }
            }
        }
    }
}

Write-Host 'Med-Flex device control test' -ForegroundColor Cyan
Write-Host "Server : $ServerUrl"
Write-Host "Device : $DeviceId"

while ($true) {
    Write-Host ''
    Write-Host '1  Start device'
    Write-Host '2  Stop device'
    Write-Host '3  Show current status'
    Write-Host '4  Set target reps'
    Write-Host '5  Set cycle timings'
    Write-Host '6  Watch live status'
    Write-Host '7  Show server version'
    Write-Host '0  Exit'
    $choice = Read-Host 'Select'

    try {
        switch ($choice.Trim()) {
            '1' { Send-Start }
            '2' { Send-Stop }
            '3' { Show-Status }
            '4' { Set-Target }
            '5' { Set-Timings }
            '6' { Watch-Status }
            '7' {
                $version = Invoke-RestMethod "$ServerUrl/api/version" -TimeoutSec 8
                $version | Format-List
            }
            '0' { exit 0 }
            default { Write-Host 'Unknown selection.' -ForegroundColor DarkYellow }
        }
    } catch {
        Write-Host "Request failed: $(Get-ApiError $_)" -ForegroundColor Red
    }
}
