# Copies the files shared by both sketches from the controller (the source of
# truth) into the display sketch. Arduino gives a sketch no way to include code
# from outside its own directory, so these exist as duplicates.
#
#   .\firmware\sync-shared.ps1          copy controller -> display
#   .\firmware\sync-shared.ps1 -Check   report drift, change nothing (exit 1 if any)
#
# Run the -Check form before building a release. Silent drift here means the
# UART settings message loses a field and the two boards disagree about the
# machine's configuration.

param([switch]$Check)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$from = Join-Path $root "pt-leg-controller"
$to = Join-Path $root "pt-leg-display"
$shared = @("link.h", "link.cpp", "session.h", "settings.h")

$drift = @()
foreach ($f in $shared) {
    $src = Join-Path $from $f
    $dst = Join-Path $to $f
    if (-not (Test-Path $src)) { throw "missing source: $src" }

    $same = (Test-Path $dst) -and
            ((Get-FileHash $src).Hash -eq (Get-FileHash $dst).Hash)
    if ($same) { continue }

    if ($Check) {
        $drift += $f
        Write-Host "DRIFT: $f" -ForegroundColor Yellow
    } else {
        Copy-Item $src $dst -Force
        Write-Host "synced: $f" -ForegroundColor Green
    }
}

if ($Check) {
    if ($drift.Count -gt 0) {
        Write-Host "$($drift.Count) shared file(s) differ. Run without -Check to sync." -ForegroundColor Red
        exit 1
    }
    Write-Host "shared files in sync" -ForegroundColor Green
} elseif ($drift.Count -eq 0) {
    Write-Host "nothing to do" -ForegroundColor DarkGray
}
