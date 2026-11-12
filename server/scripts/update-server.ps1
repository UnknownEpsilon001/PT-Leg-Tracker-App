# Safe updater/bootstrapper for the FastAPI server PC.
# LEO WAS HERE ;3
param(
    [int]$Port = 8000,
    [switch]$SkipPull,
    [switch]$SkipStart
)

$ErrorActionPreference = 'Stop'
$serverDir = Split-Path $PSScriptRoot -Parent
$repoDir = Split-Path $serverDir -Parent
$venvPython = Join-Path $serverDir '.venv\Scripts\python.exe'
$requirements = Join-Path $serverDir 'requirements-frozen.txt'

function Stop-WithMessage([string]$message) {
    Write-Host ''
    Write-Host "Not completed: $message" -ForegroundColor Red
    exit 1
}

function Run-Step([string]$name, [scriptblock]$action) {
    Write-Host ''
    Write-Host "== $name ==" -ForegroundColor Cyan
    & $action
    if ($LASTEXITCODE -is [int] -and $LASTEXITCODE -ne 0) {
        throw "$name failed with exit code $LASTEXITCODE"
    }
}

try {
    if (-not (Test-Path (Join-Path $repoDir '.git'))) {
        Stop-WithMessage "this script must run from a Git clone ($repoDir has no .git folder)"
    }
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
        Stop-WithMessage 'Git is not installed or is not available in PATH.'
    }

    # Updating or copying SQLite while the old server writes to it is unsafe.
    # [TH] ต้องปิดเซิร์ฟเวอร์เก่าก่อน เพื่อไม่คัดลอกฐานข้อมูลระหว่างที่กำลังเขียน
    $busy = Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($busy) {
        $owner = Get-Process -Id $busy.OwningProcess -ErrorAction SilentlyContinue
        $ownerName = if ($owner) { $owner.ProcessName } else { 'unknown process' }
        Stop-WithMessage "port $Port is in use by $ownerName (PID $($busy.OwningProcess)). Close the running server, then run this updater again."
    }

    if (-not $SkipPull) {
        Push-Location $repoDir
        try {
            $branch = (git branch --show-current).Trim()
            if ($LASTEXITCODE -ne 0) { throw 'could not read the Git branch' }
            if ($branch -ne 'main') {
                Stop-WithMessage "current branch is '$branch'; switch to main before updating the server."
            }

            # Refuse tracked local edits instead of overwriting server-PC changes.
            # Untracked runtime files (database, logs, backups) are allowed.
            # [TH] ถ้ามีไฟล์ที่ติดตามโดย Git ถูกแก้เอง ให้หยุดแทนการเขียนทับ
            $trackedChanges = @(git status --porcelain --untracked-files=no)
            if ($LASTEXITCODE -ne 0) { throw 'could not inspect the Git worktree' }
            if ($trackedChanges.Count -gt 0) {
                Write-Host ($trackedChanges -join [Environment]::NewLine) -ForegroundColor Yellow
                Stop-WithMessage 'tracked files have local changes. Commit or restore them before updating.'
            }

            Run-Step 'Downloading main from GitHub' {
                git pull --ff-only origin main
            }
        } finally {
            Pop-Location
        }
    } else {
        Write-Host 'Skipping Git pull (-SkipPull).' -ForegroundColor DarkYellow
    }

    # Back up the live database after pull validation but before changing Python.
    $dbPath = if ($env:PTLT_DB) {
        if ([IO.Path]::IsPathRooted($env:PTLT_DB)) { $env:PTLT_DB } else { Join-Path $serverDir $env:PTLT_DB }
    } else {
        Join-Path $serverDir 'ptlt.sqlite3'
    }
    if (Test-Path -LiteralPath $dbPath) {
        $backupDir = Join-Path $serverDir 'backups'
        New-Item -ItemType Directory -Force $backupDir | Out-Null
        $stamp = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'
        $backup = Join-Path $backupDir "ptlt-$stamp.sqlite3"
        Copy-Item -LiteralPath $dbPath -Destination $backup
        Write-Host "Database backup: $backup" -ForegroundColor Green
    } else {
        Write-Host 'No existing database yet; the server will create one.' -ForegroundColor DarkGray
    }

    if (-not (Test-Path $venvPython)) {
        Write-Host ''
        Write-Host 'Creating Python environment...' -ForegroundColor Cyan
        $created = $false
        $py = Get-Command py -ErrorAction SilentlyContinue
        if ($py) {
            & $py.Source -3.13 -m venv (Join-Path $serverDir '.venv')
            $created = $LASTEXITCODE -eq 0
        }
        if (-not $created) {
            $python = Get-Command python -ErrorAction SilentlyContinue
            if (-not $python) {
                Stop-WithMessage 'Python 3.13 was not found. Install it from python.org and enable Add Python to PATH.'
            }
            & $python.Source -m venv (Join-Path $serverDir '.venv')
            if ($LASTEXITCODE -ne 0) { throw 'creating the Python environment failed' }
        }
    }

    Run-Step 'Installing server dependencies' {
        & $venvPython -m pip install --disable-pip-version-check -r $requirements
    }

    $testTemp = Join-Path $serverDir ('.update-test-' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force $testTemp | Out-Null
    try {
        Push-Location $serverDir
        try {
            Run-Step 'Checking server installation' {
                & $venvPython -m pytest -q -p no:cacheprovider "--basetemp=$testTemp"
            }
        } finally {
            Pop-Location
        }
    } finally {
        if (Test-Path -LiteralPath $testTemp) {
            Remove-Item -LiteralPath $testTemp -Recurse -Force
        }
    }

    Write-Host ''
    Write-Host 'Server update and setup completed successfully.' -ForegroundColor Green
    if ($SkipStart) {
        Write-Host 'Server not started (-SkipStart).'
        exit 0
    }

    Write-Host "Starting server on port $Port. Close this window to stop it." -ForegroundColor Green
    & (Join-Path $PSScriptRoot 'dev.ps1') -NoMock -Port $Port
} catch {
    Stop-WithMessage $_.Exception.Message
}
