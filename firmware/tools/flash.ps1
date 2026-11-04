# [TH] สคริปต์คอมไพล์และอัปโหลดเฟิร์มแวร์ลงบอร์ด ESP32 ทั้งสองใบ
#      บังคับใช้ PartitionScheme=huge_app เสมอ ตรวจสวิตช์โหมดทดสอบและความต่างของไฟล์ที่แชร์กันก่อนแฟลช
#
# Compile and/or upload the two sketches. Wraps the commands in firmware/README.md so
# the two things that silently ruin a flash cannot be forgotten: the partition
# scheme, and a bench switch left on.
#
#   .\firmware\tools\flash.ps1                      # interactive menu
#   .\firmware\tools\flash.ps1 -Action ports        # show controller/display ports
#   .\firmware\tools\flash.ps1 -Action compile -Board both
#   .\firmware\tools\flash.ps1 -Action upload -Board display
#   .\firmware\tools\flash.ps1 -Action auto -Board both
#   .\firmware\tools\flash.ps1 -Action auto -Board controller -Erase
#   .\firmware\tools\flash.ps1 -Action auto -Board both -Port COM4 -DisplayPort COM5
#   .\firmware\tools\flash.ps1 -Check               # run the guards, flash nothing
#
# PULL THE UART JUMPERS BEFORE FLASHING. The controller's Serial2 (GPIO 16/17)
# and the CYD's (GPIO 22/27) are wired together on an assembled machine. Left
# connected, the board being flashed is driven by a board that is not in reset,
# which fakes boot-mode and checksum failures that look like a dead flash chip.
# Power both boards again afterwards to test the link.
# [TH] ถอดสายจัมเปอร์ UART ก่อนแฟลชทุกครั้ง บนเครื่องที่ประกอบแล้วสาย Serial2 ของสองบอร์ดต่อถึงกันอยู่
#      ถ้าไม่ถอด บอร์ดที่กำลังแฟลชจะถูกอีกบอร์ดที่ไม่ได้อยู่ในสถานะรีเซ็ตขับสัญญาณใส่
#      ทำให้เกิดอาการบูตผิดโหมดและ checksum ผิด ซึ่งดูเหมือนชิปแฟลชเสีย
#      แฟลชเสร็จแล้วให้จ่ายไฟทั้งสองบอร์ดเพื่อทดสอบลิงก์

param(
    [ValidateSet("menu", "ports", "compile", "upload", "auto", "check", "exit")]
    [string]$Action = "menu",
    [ValidateSet("controller", "display", "both")]
    [string]$Board = "controller",
    [string]$Port = "",          # controller port; empty = auto-detect by USB VID
    [string]$DisplayPort = "",   # display/CYD port; empty = auto-detect by USB VID
    [switch]$Erase,              # esptool erase_flash before uploading (wipes NVS)
    [switch]$Check,              # run the guards and exit; upload nothing
    [switch]$SkipGuards,         # flash a bench build on purpose
    [switch]$NoCompile,          # compatibility alias for -Action upload
    [switch]$ForceCompile,       # rebuild even when the source hash is unchanged
    [switch]$IfChanged,          # -Action compile: skip a board whose source hash is unchanged
    [switch]$DryRun,             # resolve the workflow but do not erase or upload
    [switch]$AllowPortRoleMismatch, # allow an explicit port whose VID maps to the other board
    [switch]$Yes,                # answer the -Erase confirmation up front
    [string]$EsptoolPath = ""    # override the auto-located esptool.exe
)

$ErrorActionPreference = "Stop"
$scriptArguments = $PSBoundParameters

# [TH] PartitionScheme=huge_app จำเป็น ไม่ใช่ตัวเลือก สเก็ตช์ฝั่งจอใหญ่เกินพาร์ทิชันมาตรฐาน
# Required, not optional: the default scheme leaves 1.31 MB for the app and the
# display sketch lands at 97% of that. See "Build" in firmware/README.md.
$FQBN = "esp32:esp32:esp32:PartitionScheme=huge_app"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$firmware = Join-Path $repoRoot "firmware"
$buildRoot = Join-Path $firmware ".build"

# [TH] ตรวจสวิตช์โหมดทดสอบในซอร์ส ค่าที่ commit ไว้ต้องเป็น 0 ทุกตัว
# The bench switches. Each is committed as 0 and turned on by hand for a bench
# session; flashing a real machine with one still on produces a board that looks
# broken in a way none of its output explains — LINK_OVER_USB moves the link off
# Serial2 so the CYD never hears the controller, and UI_TEST_HOOK takes the same
# USB port the link would use.
$guards = @(
    @{ File = "pt-leg-display\pt-leg-display.ino"; Name = "LINK_OVER_USB";
       Why  = "the display link moves to USB and the controller is never heard" },
    @{ File = "pt-leg-display\uitest.h";           Name = "UI_TEST_HOOK";
       Why  = "the USB test hook takes the port and cannot coexist with the link" },
    @{ File = "pt-leg-controller\pt-leg-controller.ino"; Name = "BENCH_DEBUG";
       Why  = "button/phase debug prints flood UART0" },
    @{ File = "pt-leg-controller\pt-leg-controller.ino"; Name = "LOOP_DEBUG";
       Why  = "loop timing prints flood UART0" },
    @{ File = "pt-leg-display\pt-leg-display.ino";       Name = "LOOP_DEBUG";
       Why  = "loop timing prints flood UART0" }
)

function Test-Guards {
    $bad = @()
    foreach ($g in $guards) {
        $path = Join-Path $firmware $g.File
        if (-not (Test-Path $path)) { throw "Guard file missing: $path" }
        # Only the committed `#define NAME <value>` line at column 0 counts. The
        # controller's switches also have an #ifndef fallback further up, which
        # is not the line a person edits.
        # [TH] นับเฉพาะบรรทัด #define ที่คอลัมน์แรกเท่านั้น ตัว #ifndef ด้านบนไม่ใช่บรรทัดที่คนแก้
        $line = Select-String -Path $path -Pattern "^#define\s+$($g.Name)\s+(\S+)" |
                Select-Object -Last 1
        if ($null -eq $line) { throw "Could not find '#define $($g.Name)' in $($g.File)" }
        $value = $line.Matches[0].Groups[1].Value
        if ($value -ne "0") {
            $bad += "  $($g.File): $($g.Name) = $value  ->  $($g.Why)"
        }
    }
    if ($bad.Count -gt 0) {
        Write-Host "Bench switches are still on:" -ForegroundColor Red
        $bad | ForEach-Object { Write-Host $_ -ForegroundColor Red }
        throw "Set them back to 0, or pass -SkipGuards if this is deliberate."
    }
    Write-Host "guards: bench switches all 0" -ForegroundColor Green
}

# [TH] ตรวจว่าไฟล์ที่แชร์กันสองสำเนายังตรงกัน ถ้าเพี้ยนข้อความ settings จะหายฟิลด์เงียบๆ
# Drift in link.h/link.cpp/settings.h means the UART `settings` message loses a
# field silently and the two boards disagree about the machine's configuration.
function Test-SharedDrift {
    $sync = Join-Path $firmware "sync-shared.ps1"
    if (-not (Test-Path $sync)) { throw "sync-shared.ps1 not found at $sync" }
    # Zeroed first, deliberately. sync-shared.ps1 calls `exit 1` on drift but
    # returns normally when clean, and PowerShell only updates $LASTEXITCODE on
    # an actual exit — so a stale non-zero left by any earlier native command
    # read as drift on a tree that was in sync.
    # [TH] ตั้งเป็นศูนย์ก่อนโดยตั้งใจ sync-shared.ps1 เรียก exit 1 เมื่อไฟล์ต่างกัน
    #      แต่จบแบบปกติเมื่อไม่ต่าง และ PowerShell อัปเดต $LASTEXITCODE เฉพาะตอนมีการ exit จริง
    #      ค่าเก่าที่ค้างจากคำสั่งก่อนหน้าจึงถูกอ่านว่าไฟล์ต่างกันทั้งที่ตรงกันอยู่
    $global:LASTEXITCODE = 0
    & $sync -Check
    if ($LASTEXITCODE -ne 0) {
        throw "Shared files have drifted. Run .\firmware\sync-shared.ps1 to copy controller -> display."
    }
    Write-Host "guards: shared files in sync" -ForegroundColor Green
}

# [TH] หา esptool.exe เลือก 4.5.1 ก่อนถ้ามี เพราะเป็นรุ่นที่ README อ้างอิงและใช้คำสั่งแบบเดิม
# Prefer 4.5.1 — the version README pins and the one whose subcommand spelling
# (erase_flash, underscored) this script uses. esptool 5.x renamed the commands
# to hyphenated form and only keeps the old spelling as a deprecated alias.
function Resolve-Esptool {
    if ($EsptoolPath) {
        if (-not (Test-Path $EsptoolPath)) { throw "esptool not found at $EsptoolPath" }
        return $EsptoolPath
    }
    $base = Join-Path $env:LOCALAPPDATA "Arduino15\packages\esp32\tools\esptool_py"
    if (-not (Test-Path $base)) {
        throw "esptool_py not installed under $base. Pass -EsptoolPath, or install the esp32 core."
    }
    $versions = Get-ChildItem $base -Directory | Select-Object -ExpandProperty Name
    if (@($versions).Count -eq 0) {
        throw "No esptool versions are installed under $base. Install the esp32 core."
    }
    $pick = if ($versions -contains "4.5.1") {
        "4.5.1"
    } else {
        ($versions | Sort-Object { try { [version]$_ } catch { [version]"0.0.0" } })[-1]
    }
    $exe = Join-Path $base "$pick\esptool.exe"
    if (-not (Test-Path $exe)) { throw "esptool.exe not found at $exe" }
    return $exe
}

# [TH] แยกพอร์ตด้วย VID ของชิป USB-serial ที่ใช้ในเครื่องนี้: คอนโทรลเลอร์ CP210x,
#      จอ CYD CH340/CH9102 ถ้าพบหลายตัวในกลุ่มเดียวกันจะหยุดแทนการเดา
# Identify this machine's two boards by their USB bridges: controller = CP210x,
# display/CYD = CH340/CH9102. Explicit -Port/-DisplayPort always wins. If two
# matching bridges exist, stop rather than flash a guessed board.
function Get-EspPorts {
    if (-not (Get-Command arduino-cli -CommandType Application -ErrorAction SilentlyContinue)) {
        throw "arduino-cli was not found in PATH. Install it before using port, compile, or upload actions."
    }
    $global:LASTEXITCODE = 0
    $raw = arduino-cli board list --format json
    if ($LASTEXITCODE -ne 0) { throw "arduino-cli could not list serial ports." }
    try { $json = $raw | ConvertFrom-Json } catch {
        throw "arduino-cli returned invalid port JSON: $($_.Exception.Message)"
    }
    $ports = @($json.detected_ports | Where-Object {
        $_.port.protocol -eq "serial" -and $_.port.address
    } | ForEach-Object {
        $vid = ([string]$_.port.properties.vid).Trim().ToUpperInvariant()
        if ($vid -match "^[0-9A-F]{4}$") { $vid = "0X$vid" }
        [pscustomobject]@{
            Address = [string]$_.port.address
            Vid = $vid
            Label = [string]$_.port.label
            ProtocolLabel = [string]$_.port.protocol_label
        }
    })
    $duplicates = @($ports | Group-Object Address | Where-Object { $_.Count -gt 1 })
    if ($duplicates.Count -gt 0) {
        $names = ($duplicates | ForEach-Object { $_.Name }) -join ", "
        throw "arduino-cli returned duplicate serial-port entries: $names. Unplug/replug the boards and retry."
    }
    return @($ports | Sort-Object Address)
}

function Get-PortRole([string]$vid) {
    switch ($vid) {
        "0X10C4" { "controller" }
        "0X1A86" { "display" }
        default  { "unknown" }
    }
}

function Show-EspPorts($ports) {
    if ($ports.Count -eq 0) {
        Write-Host "No serial boards found." -ForegroundColor Yellow
        return
    }
    foreach ($item in $ports) {
        $role = Get-PortRole $item.Vid
        Write-Host ("{0,-8} {1,-12} VID {2}  {3}" -f $item.Address, $role, $item.Vid, $item.Label)
    }
}

function Resolve-RolePort([string]$role, $ports) {
    $explicit = ""
    if ($role -eq "controller") { $explicit = $Port }
    if ($role -eq "display") {
        $explicit = $DisplayPort
        # For a single display operation, -Port is the convenient override.
        if (-not $explicit -and $Board -eq "display") { $explicit = $Port }
    }

    if ($explicit) {
        $found = @($ports | Where-Object { $_.Address -eq $explicit })
        if ($found.Count -eq 0) {
            throw "Explicit $role port $explicit is not currently detected. Run -Action ports."
        }
        if ($found.Count -gt 1) { throw "Port inventory contains duplicate entries for $explicit." }
        $detectedRole = Get-PortRole $found[0].Vid
        if (-not $AllowPortRoleMismatch -and $detectedRole -ne "unknown" -and $detectedRole -ne $role) {
            throw "$explicit is detected as $detectedRole, not $role. Check the cables/arguments, or pass -AllowPortRoleMismatch for replacement hardware."
        }
        return $found[0].Address
    }

    $wantedVid = if ($role -eq "controller") { "0X10C4" } else { "0X1A86" }
    $matches = @($ports | Where-Object { $_.Vid -eq $wantedVid })
    if ($matches.Count -eq 1) { return $matches[0].Address }
    if ($matches.Count -eq 0) {
        $flag = if ($role -eq "controller") { "-Port COMx" } else { "-DisplayPort COMx" }
        throw "No $role port found (expected USB VID $wantedVid). Plug it in or pass $flag."
    }
    $names = ($matches | ForEach-Object { $_.Address }) -join ", "
    throw "Multiple $role candidates found: $names. Pass the port explicitly."
}

function Get-SketchInfo([string]$role) {
    $sketch = if ($role -eq "controller") { "pt-leg-controller" } else { "pt-leg-display" }
    return [pscustomobject]@{
        Role = $role
        Sketch = $sketch
        Path = Join-Path $firmware $sketch
        BuildPath = Join-Path $buildRoot $role
        StatePath = Join-Path (Join-Path $buildRoot $role) "source-state.json"
    }
}

function Get-SourceHash($info) {
    $files = @(Get-ChildItem $info.Path -Recurse -File | Where-Object {
        $_.Extension -in @(".ino", ".c", ".cpp", ".h", ".hpp", ".S")
    })
    if ($info.Role -eq "display") {
        $files += Get-Item (Join-Path $firmware "lv_conf.h"), (Join-Path $firmware "TFT_eSPI_User_Setup.h")
    }
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        $parts = foreach ($file in ($files | Sort-Object FullName)) {
            $relative = $file.FullName.Substring(([string]$firmware).Length).TrimStart("\", "/")
            "$relative|$((Get-FileHash -Algorithm SHA256 $file.FullName).Hash)"
        }
        $payload = [Text.Encoding]::UTF8.GetBytes(($FQBN + "`n" + ($parts -join "`n")))
        return ([BitConverter]::ToString($sha.ComputeHash($payload))).Replace("-", "")
    } finally {
        $sha.Dispose()
    }
}

function Get-RequiredBuildArtifacts($info) {
    return @(
        "$($info.Sketch).ino.bin",
        "$($info.Sketch).ino.bootloader.bin",
        "$($info.Sketch).ino.partitions.bin",
        "boot_app0.bin",
        "flash_args"
    )
}

function Get-BuildArtifactHashes($info) {
    $hashes = [ordered]@{}
    foreach ($name in @(Get-RequiredBuildArtifacts $info)) {
        $path = Join-Path $info.BuildPath $name
        if (-not (Test-Path $path -PathType Leaf)) { throw "Required build artifact is missing: $path" }
        if ((Get-Item $path).Length -eq 0) { throw "Required build artifact is empty: $path" }
        $hashes[$name] = (Get-FileHash -Algorithm SHA256 $path).Hash
    }
    return $hashes
}

function Test-BuildCurrent($info, [string]$sourceHash) {
    if (-not (Test-Path $info.StatePath)) { return $false }
    try { $state = Get-Content -Raw $info.StatePath | ConvertFrom-Json } catch { return $false }
    if ($state.schemaVersion -ne 2 -or $state.sourceHash -ne $sourceHash -or $state.fqbn -ne $FQBN) {
        return $false
    }
    if ($null -eq $state.artifacts) { return $false }
    foreach ($name in @(Get-RequiredBuildArtifacts $info)) {
        $saved = $state.artifacts.PSObject.Properties[$name]
        $path = Join-Path $info.BuildPath $name
        if ($null -eq $saved -or -not (Test-Path $path -PathType Leaf)) { return $false }
        if ((Get-FileHash -Algorithm SHA256 $path).Hash -ne $saved.Value) { return $false }
    }
    return $true
}

function ConvertTo-NativeCommandLineArgument([string]$value) {
    if ($null -eq $value -or $value.Length -eq 0) { return '""' }
    if ($value -notmatch '[\s"]') { return $value }

    $result = New-Object Text.StringBuilder
    [void]$result.Append('"')
    $slashes = 0
    foreach ($character in $value.ToCharArray()) {
        if ($character -eq '\') {
            $slashes++
        } elseif ($character -eq '"') {
            [void]$result.Append((('\' * (($slashes * 2) + 1)) -join ''))
            [void]$result.Append('"')
            $slashes = 0
        } else {
            if ($slashes -gt 0) { [void]$result.Append((('\' * $slashes) -join '')) }
            [void]$result.Append($character)
            $slashes = 0
        }
    }
    if ($slashes -gt 0) { [void]$result.Append((('\' * ($slashes * 2)) -join '')) }
    [void]$result.Append('"')
    return $result.ToString()
}

function Stop-NativeProcessTree($process) {
    if ($null -eq $process) { return }
    try { if ($process.HasExited) { return } } catch { return }

    try {
        $killTree = $process.GetType().GetMethod("Kill", [type[]]@([bool]))
        if ($null -ne $killTree) {
            [void]$killTree.Invoke($process, @($true))
        } else {
            $killer = Start-Process -FilePath "$env:SystemRoot\System32\taskkill.exe" `
                -ArgumentList "/PID $($process.Id) /T /F" -WindowStyle Hidden -PassThru -Wait
            if ($killer.ExitCode -ne 0) { throw "taskkill exited with $($killer.ExitCode)" }
        }
    } catch {
        try { $process.Kill() } catch { }
    }
    try { [void]$process.WaitForExit(5000) } catch { }
}

function Read-NativeLog([string]$path) {
    if (-not (Test-Path $path -PathType Leaf)) { return "" }
    try {
        $stream = [IO.File]::Open(
            $path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite
        )
        try {
            $reader = New-Object IO.StreamReader($stream)
            try { return $reader.ReadToEnd() } finally { $reader.Dispose() }
        } finally {
            $stream.Dispose()
        }
    } catch { return "" }
}

function Invoke-NativeWithProgress {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string[]]$ArgumentList,
        [Parameter(Mandatory = $true)][string]$Activity,
        [ValidateSet("compile", "upload", "erase")][string]$Kind,
        [int]$ExpectedSeconds = 120
    )

    New-Item -ItemType Directory -Force $buildRoot | Out-Null
    $runId = [guid]::NewGuid().ToString("N")
    $stdoutPath = Join-Path $buildRoot ".progress-$runId.out.log"
    $stderrPath = Join-Path $buildRoot ".progress-$runId.err.log"
    $arguments = ($ArgumentList | ForEach-Object { ConvertTo-NativeCommandLineArgument $_ }) -join " "
    $process = $null
    $stopwatch = [Diagnostics.Stopwatch]::StartNew()
    $cancelled = $false
    $canReadCancelKey = $false
    $oldTreatControlCAsInput = $false
    $combinedOutput = ""
    $exitCode = -1

    try {
        try {
            if (-not [Console]::IsInputRedirected) {
                $oldTreatControlCAsInput = [Console]::TreatControlCAsInput
                [Console]::TreatControlCAsInput = $true
                $canReadCancelKey = $true
            }
        } catch { $canReadCancelKey = $false }

        $process = Start-Process -FilePath $FilePath -ArgumentList $arguments `
            -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath `
            -WindowStyle Hidden -PassThru

        # Touching .Handle here is load-bearing, not defensive. Start-Process
        # -PassThru hands back a Process object that has not opened a handle to
        # the child; Windows then reaps the process on exit and .ExitCode reads
        # back as $null. Since $null -ne 0 is true in PowerShell, the check at the
        # end of this function threw on every run — a *successful* compile
        # reported "failed (exit code )" with the value blank, and because that
        # throw happens before Install-StagedBuild in Invoke-Compile, no build was
        # ever installed: every compile rebuilt from scratch and -Action upload
        # refused with "No current build". Reading .Handle once, while the child is
        # still alive, makes .NET keep the handle and the exit code survives.
        # [TH] การอ่าน .Handle ตรงนี้จำเป็น ไม่ใช่แค่กันไว้
        #      Start-Process -PassThru คืนออบเจ็กต์ที่ยังไม่ได้เปิดแฮนเดิลของโพรเซสลูก
        #      วินโดวส์จึงเก็บกวาดโพรเซสทิ้งเมื่อจบ และ .ExitCode อ่านได้เป็นค่าว่าง
        #      เนื่องจาก $null -ne 0 เป็นจริงในPowerShell การตรวจท้ายฟังก์ชันนี้จึงโยนข้อผิดพลาดทุกครั้ง
        #      การคอมไพล์ที่สำเร็จจะรายงานว่า "failed (exit code )" โดยไม่มีตัวเลข
        #      และเพราะการโยนนั้นเกิดก่อน Install-StagedBuild ใน Invoke-Compile
        #      จึงไม่มีการติดตั้งผลบิลด์เลย ทุกครั้งต้องคอมไพล์ใหม่ทั้งหมด และ -Action upload จะปฏิเสธ
        #      การอ่าน .Handle หนึ่งครั้งขณะโพรเซสลูกยังทำงานอยู่ ทำให้ .NET เก็บแฮนเดิลไว้ และรหัสจบงานไม่หายไป
        $null = $process.Handle

        while (-not $process.HasExited) {
            if ($canReadCancelKey) {
                try {
                    while ([Console]::KeyAvailable) {
                        $key = [Console]::ReadKey($true)
                        if ($key.Key -eq [ConsoleKey]::C -and
                            ($key.Modifiers -band [ConsoleModifiers]::Control)) {
                            $cancelled = $true
                            break
                        }
                    }
                } catch { $canReadCancelKey = $false }
            }
            if ($cancelled) { break }

            $elapsed = [math]::Max(0, [int]$stopwatch.Elapsed.TotalSeconds)
            $log = (Read-NativeLog $stdoutPath) + "`n" + (Read-NativeLog $stderrPath)
            $percent = [math]::Min(90, [math]::Max(1, [int](90 * $elapsed / [math]::Max(1, $ExpectedSeconds))))
            $status = "Working... ${elapsed}s elapsed (approximate) - Ctrl+C cancels safely"
            if ($Kind -eq "erase") {
                $status = "Erasing... ${elapsed}s elapsed - interruption requires reflash"
            }
            if ($Kind -eq "upload") {
                $matches = [regex]::Matches($log, '\((\d{1,3})\s*%\)')
                if ($matches.Count -gt 0) {
                    $percent = [math]::Min(99, [int]$matches[$matches.Count - 1].Groups[1].Value)
                    $status = "Writing flash: $percent% - interruption requires re-upload"
                }
            }
            Write-Progress -Activity $Activity -Status $status -PercentComplete $percent
            Start-Sleep -Milliseconds 500
            $process.Refresh()
        }

        if ($cancelled) {
            Stop-NativeProcessTree $process
        } else {
            $process.WaitForExit()
            # Keep the -1 seeded above if the exit code still comes back empty:
            # assigning $null here is what previously turned every run into a
            # failure reported with a blank code, which is unactionable.
            # [TH] ถ้ารหัสจบงานยังอ่านได้เป็นค่าว่าง ให้คงค่า -1 ที่ตั้งไว้ด้านบน
            #      การกำหนดค่าว่างตรงนี้คือสาเหตุเดิมที่ทำให้ทุกครั้งกลายเป็นความล้มเหลวที่ไม่มีรหัสบอก
            #      ซึ่งเอาไปแก้อะไรไม่ได้
            $reported = $process.ExitCode
            if ($null -ne $reported) { $exitCode = $reported }
        }
    } finally {
        $stopwatch.Stop()
        if ($null -ne $process -and -not $process.HasExited) { Stop-NativeProcessTree $process }
        if ($canReadCancelKey) {
            try { [Console]::TreatControlCAsInput = $oldTreatControlCAsInput } catch { }
        }
        Write-Progress -Activity $Activity -Completed
        $combinedOutput = (Read-NativeLog $stdoutPath) + "`n" + (Read-NativeLog $stderrPath)
        Remove-Item -Force $stdoutPath, $stderrPath -ErrorAction SilentlyContinue
    }

    if ($cancelled) {
        $message = switch ($Kind) {
            "compile" { "$Activity cancelled safely; the previous good build was kept." }
            "upload"  { "$Activity interrupted. Re-run upload because board firmware may be incomplete." }
            "erase"   { "$Activity interrupted. Re-run erase/upload before using the board." }
        }
        throw [OperationCanceledException]::new($message)
    }
    if ($exitCode -ne 0) {
        if ($combinedOutput.Trim()) { Write-Host $combinedOutput.TrimEnd() -ForegroundColor DarkGray }
        throw "$Activity failed (exit code $exitCode)."
    }

    Write-Progress -Activity $Activity -Status "Complete" -PercentComplete 100
    Write-Progress -Activity $Activity -Completed
    $summary = @($combinedOutput -split "`r?`n" | Where-Object {
        $_ -match "Sketch uses|Global variables|Hash of data verified|Hard resetting|Leaving"
    })
    if ($summary.Count -gt 0) { $summary | ForEach-Object { Write-Host $_ -ForegroundColor DarkGray } }
}

function Install-StagedBuild($activeInfo, $stagedInfo) {
    $backupPath = Join-Path $buildRoot ".$($activeInfo.Role)-backup-$([guid]::NewGuid().ToString('N'))"
    $hadActiveBuild = Test-Path $activeInfo.BuildPath
    $installSucceeded = $false
    try {
        if ($hadActiveBuild) { Move-Item -LiteralPath $activeInfo.BuildPath -Destination $backupPath }
        Move-Item -LiteralPath $stagedInfo.BuildPath -Destination $activeInfo.BuildPath
        $installSucceeded = $true
    } catch {
        $installError = $_.Exception.Message
        if (-not (Test-Path $activeInfo.BuildPath) -and (Test-Path $backupPath)) {
            try {
                Move-Item -LiteralPath $backupPath -Destination $activeInfo.BuildPath
            } catch {
                throw "Could not install the new $($activeInfo.Role) build or restore the previous build. " +
                      "The previous build is preserved at $backupPath. Install error: $installError. " +
                      "Restore error: $($_.Exception.Message)"
            }
        }
        throw
    } finally {
        # Delete the backup only after the staged build is definitely active.
        # If rollback itself fails, leave the last good build recoverable.
        if ($installSucceeded -and (Test-Path $backupPath)) {
            try { Remove-Item -LiteralPath $backupPath -Recurse -Force } catch {
                Write-Host "warning: old build cache remains at $backupPath" -ForegroundColor Yellow
            }
        }
    }
}

function Invoke-Compile([string]$role, [switch]$OnlyWhenChanged) {
    $info = Get-SketchInfo $role
    if (-not (Test-Path $info.Path)) { throw "Sketch not found: $($info.Path)" }
    $hash = Get-SourceHash $info
    if ($OnlyWhenChanged -and -not $ForceCompile -and (Test-BuildCurrent $info $hash)) {
        Write-Host "compile skipped: $role source unchanged" -ForegroundColor Green
        return $info
    }
    $stagingPath = Join-Path $buildRoot ".$role-staging-$([guid]::NewGuid().ToString('N'))"
    $stagedInfo = [pscustomobject]@{
        Role = $info.Role
        Sketch = $info.Sketch
        Path = $info.Path
        BuildPath = $stagingPath
        StatePath = Join-Path $stagingPath "source-state.json"
    }
    New-Item -ItemType Directory -Force $stagedInfo.BuildPath | Out-Null
    try {
        Write-Host "compiling $role (press Ctrl+C to cancel safely)" -ForegroundColor Cyan
        $cli = (Get-Command arduino-cli -CommandType Application | Select-Object -First 1).Source
        $expected = if ($role -eq "display") { 180 } else { 120 }
        Invoke-NativeWithProgress -FilePath $cli -ArgumentList @(
            "compile", "--fqbn", $FQBN, "--build-path", $stagedInfo.BuildPath, $stagedInfo.Path
        ) -Activity "Compiling $role" -Kind compile -ExpectedSeconds $expected
        $artifacts = Get-BuildArtifactHashes $stagedInfo
        [pscustomobject]@{
            schemaVersion = 2
            sourceHash = $hash
            fqbn = $FQBN
            compiledAt = (Get-Date).ToString("o")
            artifacts = $artifacts
        } | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 $stagedInfo.StatePath
        Install-StagedBuild $info $stagedInfo
    } finally {
        if (Test-Path $stagedInfo.BuildPath) {
            Remove-Item -LiteralPath $stagedInfo.BuildPath -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
    Write-Host "compiled: $role" -ForegroundColor Green
    return $info
}

function Invoke-Upload([string]$role, [string]$comPort) {
    $info = Get-SketchInfo $role
    $hash = Get-SourceHash $info
    if (-not (Test-BuildCurrent $info $hash)) {
        throw "No current $role build. Run -Action compile, or use -Action auto."
    }
    if ($DryRun) {
        if ($Erase) { Write-Host "dry run: would erase $role on $comPort" -ForegroundColor Yellow }
        Write-Host "dry run: would upload $role -> $comPort" -ForegroundColor Yellow
        return
    }
    try {
        if ($Erase) { Invoke-Erase $comPort }
        Write-Host "uploading $role -> $comPort" -ForegroundColor Cyan
        $cli = (Get-Command arduino-cli -CommandType Application | Select-Object -First 1).Source
        Invoke-NativeWithProgress -FilePath $cli -ArgumentList @(
            "upload", "-p", $comPort, "--fqbn", $FQBN, "--build-path", $info.BuildPath, $info.Path
        ) -Activity "Uploading $role to $comPort" -Kind upload -ExpectedSeconds 30
    } catch [OperationCanceledException] {
        throw
    } catch {
        throw "Upload/erase did not complete for $role on $comPort. The board may have disconnected; " +
              "do not use it until you reconnect it and rerun the upload. The verified build cache is intact. " +
              "A board flashed earlier in this run may already be updated. Cause: $($_.Exception.Message)"
    }
    Write-Host "done: $role on $comPort" -ForegroundColor Green
}

function Invoke-Erase([string]$comPort) {
    # [TH] ลบแฟลชทั้งชิป ใช้เมื่อเปลี่ยน partition scheme หรือบอร์ดบูตวนไม่ขึ้น
    #      ค่าใน NVS หายด้วย ฝั่งคอนโทรลเลอร์คือรหัส WiFi และรหัสเครื่อง
    # Whole-chip erase. Needed when the partition scheme changes: uploading a new
    # app does not fix it, because the stale OTA record at 0xe000 still points at
    # a slot the new table does not have. Costs NVS — on the controller that is
    # the Wi-Fi credentials and the device code.
    $esptool = Resolve-Esptool
    Write-Host "erasing $comPort with $esptool" -ForegroundColor Yellow
    Invoke-NativeWithProgress -FilePath $esptool -ArgumentList @(
        "--chip", "esp32", "--port", $comPort, "--baud", "921600", "erase_flash"
    ) -Activity "Erasing $comPort" -Kind erase -ExpectedSeconds 20
}

function Get-SelectedRoles {
    if ($Board -eq "both") { return @("controller", "display") }
    return @($Board)
}

function Read-Menu {
    Write-Host ""
    Write-Host "PT Leg firmware tool" -ForegroundColor Cyan
    Write-Host "  1  Check controller/display ports"
    Write-Host "  2  Compile"
    Write-Host "  3  Upload existing build"
    Write-Host "  4  Full auto: compile only changed code, then detect ports and upload"
    Write-Host "  5  Run safety checks only"
    Write-Host "  6  Exit"
    $choice = Read-Host "Choose 1-6"
    $script:Action = switch ($choice) {
        "1" { "ports" }
        "2" { "compile" }
        "3" { "upload" }
        "4" { "auto" }
        "5" { "check" }
        "6" { "exit" }
        default { throw "Unknown menu choice: $choice" }
    }
    if ($script:Action -in @("compile", "upload", "auto")) {
        Write-Host ""
        Write-Host "Choose board" -ForegroundColor Cyan
        Write-Host "  1  Controller"
        Write-Host "  2  Display / CYD"
        Write-Host "  3  Both"
        $boardChoice = Read-Host "Choose 1-3"
        $script:Board = switch ($boardChoice) {
            "1" { "controller" }
            "2" { "display" }
            "3" { "both" }
            default { throw "Unknown board choice: $boardChoice" }
        }
    }
}

# --- action and guards -------------------------------------------------------

function Invoke-SelectedAction {
    $script:Port = $Port.Trim().ToUpperInvariant()
    $script:DisplayPort = $DisplayPort.Trim().ToUpperInvariant()
    foreach ($namedPort in @(@{ Name = "Port"; Value = $Port }, @{ Name = "DisplayPort"; Value = $DisplayPort })) {
        if ($namedPort.Value -and $namedPort.Value -notmatch "^COM[1-9][0-9]*$") {
            throw "-$($namedPort.Name) must look like COM4, not '$($namedPort.Value)'."
        }
    }
    if ($Board -eq "controller" -and $DisplayPort) { throw "-DisplayPort is not valid with -Board controller." }
    if ($Board -eq "display" -and $Port -and $DisplayPort) {
        throw "For -Board display, pass either -Port or -DisplayPort, not both."
    }
    if ($Board -eq "both" -and $Port -and $DisplayPort -and $Port -eq $DisplayPort) {
        throw "-Port and -DisplayPort cannot both be $Port."
    }
    if (($Port -or $DisplayPort) -and $Action -notin @("upload", "auto")) {
        throw "-Port and -DisplayPort are valid only with upload or auto."
    }
    if ($Erase -and $Action -notin @("upload", "auto")) { throw "-Erase is valid only with upload or auto." }
    if ($Yes -and -not $Erase) { throw "-Yes is meaningful only with -Erase." }
    if ($EsptoolPath -and -not $Erase) { throw "-EsptoolPath is meaningful only with -Erase." }
    if ($ForceCompile -and $Action -notin @("compile", "auto")) { throw "-ForceCompile is valid only with compile or auto." }
    if ($DryRun -and $Action -notin @("upload", "auto")) { throw "-DryRun is valid only with upload or auto." }
    if ($AllowPortRoleMismatch -and $Action -notin @("upload", "auto")) {
        throw "-AllowPortRoleMismatch is valid only with upload or auto."
    }
    if ($AllowPortRoleMismatch -and -not ($Port -or $DisplayPort)) {
        throw "-AllowPortRoleMismatch requires an explicit -Port or -DisplayPort."
    }
    if ($SkipGuards -and $Action -notin @("compile", "upload", "auto")) {
        throw "-SkipGuards is valid only with compile, upload, or auto."
    }

    if ($Action -eq "ports") {
        Show-EspPorts -ports @(Get-EspPorts)
        return
    }

    if ($SkipGuards) {
        Write-Host "guards skipped (-SkipGuards)" -ForegroundColor Yellow
    } else {
        Test-Guards
        Test-SharedDrift
    }

    if ($Action -eq "check") {
        Write-Host "check only, nothing flashed" -ForegroundColor Green
        return
    }

    $roles = @(Get-SelectedRoles)
    $resolved = @{}
    if ($Action -in @("upload", "auto")) {
        # Resolve every selected board from one snapshot before spending time on
        # a compile or erasing anything.
        $portSnapshot = @(Get-EspPorts)
        foreach ($role in $roles) { $resolved[$role] = Resolve-RolePort $role $portSnapshot }
        if ($roles.Count -eq 2 -and $resolved.controller -eq $resolved.display) {
            throw "Controller and display resolved to the same port ($($resolved.controller))."
        }
    }

    if (-not $DryRun -and $Action -in @("upload", "auto") -and $Erase -and -not $Yes) {
        Write-Host ""
        Write-Host "-Erase wipes the whole chip, including NVS." -ForegroundColor Yellow
        if ($roles -contains "controller") {
            Write-Host "On the controller, Wi-Fi credentials and the device code must be entered again." -ForegroundColor Yellow
        }
        if ($roles -contains "display") {
            Write-Host "On the display, locally stored NVS settings will be lost." -ForegroundColor Yellow
        }
        $answer = Read-Host "Type 'erase' to continue"
        if ($answer -ne "erase") { throw "Erase cancelled; nothing was changed." }
    }

    if ($Action -in @("compile", "auto")) {
        foreach ($role in $roles) { Invoke-Compile $role -OnlyWhenChanged:($Action -eq "auto" -or $IfChanged) | Out-Null }
    }

    if ($Action -in @("upload", "auto")) {
        Write-Host ""
        Write-Host "UART jumpers off? Disconnect controller GPIO 16/17 and CYD GPIO 22/27" -ForegroundColor Yellow
        Write-Host "before uploading." -ForegroundColor Yellow
        # Compilation can take minutes. Confirm no board disappeared or moved.
        $freshPorts = @(Get-EspPorts)
        foreach ($role in $roles) {
            $freshPort = Resolve-RolePort $role $freshPorts
            if ($freshPort -ne $resolved[$role]) {
                throw "$role moved from $($resolved[$role]) to $freshPort during the run. Rerun after USB ports are stable."
            }
        }
        foreach ($role in $roles) { Invoke-Upload $role $resolved[$role] }
        Write-Host ""
        Write-Host "Reconnect the UART jumpers and power both boards to test the link." -ForegroundColor Cyan
    }
}

function Invoke-Program {
    if ($NoCompile -and $ForceCompile) { throw "-NoCompile and -ForceCompile cannot be used together." }
    if ($Check -and $scriptArguments.ContainsKey("Action") -and $Action -notin @("menu", "check")) {
        throw "-Check conflicts with -Action $Action. Use -Action check by itself."
    }
    if ($NoCompile -and $scriptArguments.ContainsKey("Action") -and $Action -notin @("menu", "upload")) {
        throw "-NoCompile conflicts with -Action $Action. Use -Action upload."
    }
    if ($NoCompile) { $script:Action = "upload" }
    if ($Check) { $script:Action = "check" }

    $interactive = $Action -eq "menu"
    if (-not $interactive) {
        if ($Action -eq "exit") { return }
        Invoke-SelectedAction
        return
    }

    while ($true) {
        try {
            Read-Menu
            if ($Action -eq "exit") {
                Write-Host "Bye." -ForegroundColor Cyan
                return
            }
            Invoke-SelectedAction
        } catch {
            Write-Host ""
            Write-Host "Not completed: $($_.Exception.Message)" -ForegroundColor Yellow
            Write-Host "Returning to the menu. You can connect the board and try again." -ForegroundColor Yellow
        }
    }
}

try {
    Invoke-Program
} catch {
    # Non-interactive calls must terminate for CI/automation, but keep the
    # output concise instead of emitting a PowerShell exception stack.
    Write-Host "flash.ps1: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
