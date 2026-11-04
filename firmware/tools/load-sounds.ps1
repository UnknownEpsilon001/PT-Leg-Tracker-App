# Copies the CYD's spoken cues onto its SD card.
#
# Finds the SD card plugged into this PC, copies the four cue files into
# <card>\sounds\, and compares each copy's hash with the original. The read-back
# goes through the Windows file cache, so it catches a wrong or truncated copy,
# not a card that silently fails to store what it was given. Nothing else on the
# card (patients.csv, sessions.csv, settings.json) is touched.
#
#   .\firmware\tools\load-sounds.ps1            find the card, ask if there are several
#   .\firmware\tools\load-sounds.ps1 -Drive E:  use this drive
#
# Works from the repo (sounds in firmware\sd\sounds) and from an unzipped release
# (sounds in sd-card\sounds next to this script).

param([string]$Drive = "")

$ErrorActionPreference = "Stop"

function Fail($msg) {
    Write-Host ""
    Write-Host "[FAILED] $msg" -ForegroundColor Red
    exit 1
}

# ---- where the sound files are -------------------------------------------
$candidates = @(
    (Join-Path $PSScriptRoot "sd-card\sounds"),
    (Join-Path $PSScriptRoot "..\sd\sounds")
)
$src = $candidates | Where-Object { Test-Path (Join-Path $_ "boot.wav") } | Select-Object -First 1
if (-not $src) { Fail "No sounds folder found next to this script (looked for sd-card\sounds and ..\sd\sounds)." }
$src = (Resolve-Path $src).Path
$files = @(Get-ChildItem -Path $src -Filter *.wav | Sort-Object Name)
Write-Host "sounds: $($files.Count) files in $src"

# ---- which drive is the card ----------------------------------------------
# DriveType 2 = removable. A USB SD reader shows up as one; a built-in reader
# sometimes reports as a fixed disk, which is what -Drive is for.
if ($Drive) {
    $Drive = $Drive.TrimEnd('\').ToUpper()
    if ($Drive.Length -eq 1) { $Drive = "${Drive}:" }
    $disk = Get-CimInstance Win32_LogicalDisk -Filter "DeviceID = '$Drive'"
    if (-not $disk) { Fail "There is no drive $Drive." }
} else {
    $cards = @(Get-CimInstance Win32_LogicalDisk -Filter "DriveType = 2" | Where-Object { $_.Size -gt 0 })
    if ($cards.Count -eq 0) {
        Fail "No SD card found. Put the card in the PC's reader and run this again, or pass -Drive E: if Windows shows it as a normal disk."
    }
    if ($cards.Count -eq 1) {
        $disk = $cards[0]
    } else {
        Write-Host ""
        Write-Host "More than one removable drive:"
        for ($i = 0; $i -lt $cards.Count; $i++) {
            $c = $cards[$i]
            $gb = [math]::Round($c.Size / 1GB, 1)
            Write-Host ("  [{0}] {1}  {2}  {3} GB  {4}" -f ($i + 1), $c.DeviceID, $c.VolumeName, $gb, $c.FileSystem)
        }
        $pick = Read-Host "Which one is the CYD's card (number)"
        $n = 0
        if (-not [int]::TryParse($pick, [ref]$n) -or $n -lt 1 -or $n -gt $cards.Count) { Fail "Nothing picked." }
        $disk = $cards[$n - 1]
    }
}

$root = "$($disk.DeviceID)\"
$gb = [math]::Round($disk.Size / 1GB, 1)
Write-Host ("card:   {0}  {1}  {2} GB  {3}" -f $disk.DeviceID, $disk.VolumeName, $gb, $disk.FileSystem)

# The CYD's SD library reads FAT32 (and FAT16) only. exFAT is the default for
# cards over 32 GB and mounts as "sd card: absent" on the CYD.
if ($disk.FileSystem -notin @("FAT32", "FAT")) {
    Fail "The card is $($disk.FileSystem). The CYD needs FAT32 - reformat it as FAT32 first (this erases the card)."
}

# A card that has been in the CYD carries its files; a blank card is fine too.
$known = @("settings.json", "sessions.csv", "patients.csv") | Where-Object { Test-Path (Join-Path $root $_) }
if ($known) { Write-Host "        looks like a CYD card (has $($known -join ', '))" }
else { Write-Host "        no CYD files on it yet - the sounds go on anyway" }

# ---- copy and check ---------------------------------------------------------
$dst = Join-Path $root "sounds"
if (-not (Test-Path $dst)) { New-Item -ItemType Directory -Path $dst | Out-Null }

Write-Host ""
$bad = 0
$i = 0
foreach ($f in $files) {
    $i++
    $target = Join-Path $dst $f.Name
    Copy-Item -Path $f.FullName -Destination $target -Force
    $same = (Get-FileHash $f.FullName -Algorithm SHA256).Hash -eq (Get-FileHash $target -Algorithm SHA256).Hash
    if ($same) {
        Write-Host ("  [{0}/{1}] {2,-12} {3,7} bytes  OK" -f $i, $files.Count, $f.Name, $f.Length) -ForegroundColor Green
    } else {
        Write-Host ("  [{0}/{1}] {2,-12} copy does not match" -f $i, $files.Count, $f.Name) -ForegroundColor Red
        $bad++
    }
}

if ($bad) { Fail "$bad file(s) did not copy cleanly. Try another reader or card." }

Write-Host ""
Write-Host "[OK] sounds are on the card in $dst" -ForegroundColor Green
Write-Host "Eject the card in Windows first (Safely Remove), then put it back in the CYD and power it on."
Write-Host "You should hear the boot sound once the screen is up."
[console]::Beep(880, 150)
