# [TH] สร้าง APK และติดตั้งแอปผ่าน USB debugging หรือ Wireless debugging
# Android app build/deploy helper. Run with no arguments for the numbered menu.
#
#   .\tools\app-build.ps1
#   .\tools\app-build.ps1 -Action build
#   .\tools\app-build.ps1 -Action usb
#   .\tools\app-build.ps1 -Action wireless
#   .\tools\app-build.ps1 -Action auto
#   .\tools\app-build.ps1 -Action connect -Address 192.168.1.50:5555

param(
    [ValidateSet("menu", "build", "devices", "usb", "wireless", "auto", "connect", "pair", "exit")]
    [string]$Action = "menu",
    [ValidateSet("any", "usb", "wireless")]
    [string]$Connection = "any",
    [string]$Device = "",
    [string]$Address = "",
    [string]$PairingCode = "",
    [string]$AdbPath = "",
    [switch]$ForceBuild,
    [switch]$NoBuild,
    [switch]$NoLaunch
)

$ErrorActionPreference = "Stop"
$scriptArguments = $PSBoundParameters
$appRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$androidRoot = Join-Path $appRoot "android"
$buildStateRoot = Join-Path $appRoot ".app-build"
$statePath = Join-Path $buildStateRoot "state.json"
$outputRoot = Join-Path $appRoot "output"
$publishedApk = Join-Path $outputRoot "PT-Leg-Tracker-debug.apk"
$gradleApk = Join-Path $androidRoot "app\build\outputs\apk\debug\app-debug.apk"
$appId = "com.unknownaimo.ptlegtracker"
$script:interactiveMode = $Action -eq "menu"

function Get-AppSourceHash {
    $files = @()
    foreach ($directory in @("src", "public")) {
        $path = Join-Path $appRoot $directory
        if (Test-Path $path) { $files += Get-ChildItem $path -Recurse -File }
    }

    $androidSource = Join-Path $androidRoot "app\src"
    if (Test-Path $androidSource) {
        $files += Get-ChildItem $androidSource -Recurse -File | Where-Object {
            $_.FullName -notmatch "[\\/]assets[\\/]public[\\/]" -and
            $_.FullName -notmatch "[\\/]assets[\\/]capacitor\.config\.json$"
        }
    }

    foreach ($relative in @(
        "package.json", "package-lock.json", "index.html", "capacitor.config.ts",
        "vite.config.ts", "env.d.ts", "tsconfig.json", "tsconfig.app.json",
        "tsconfig.node.json", "android\app\build.gradle", "android\build.gradle",
        "android\settings.gradle", "android\variables.gradle",
        "android\gradle.properties", "android\gradle\wrapper\gradle-wrapper.properties"
    )) {
        $path = Join-Path $appRoot $relative
        if (Test-Path $path -PathType Leaf) { $files += Get-Item $path }
    }

    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        $parts = foreach ($file in @($files | Sort-Object FullName -Unique)) {
            $relative = $file.FullName.Substring($appRoot.Length).TrimStart("\", "/")
            "$relative|$((Get-FileHash -Algorithm SHA256 $file.FullName).Hash)"
        }
        $payload = [Text.Encoding]::UTF8.GetBytes(($parts -join "`n"))
        return ([BitConverter]::ToString($sha.ComputeHash($payload))).Replace("-", "")
    } finally {
        $sha.Dispose()
    }
}

function Test-PublishedApkCurrent([string]$sourceHash) {
    if (-not (Test-Path $statePath -PathType Leaf) -or -not (Test-Path $publishedApk -PathType Leaf)) {
        return $false
    }
    try { $state = Get-Content -Raw $statePath | ConvertFrom-Json } catch { return $false }
    if ($state.schemaVersion -ne 1 -or $state.sourceHash -ne $sourceHash) { return $false }
    return (Get-FileHash -Algorithm SHA256 $publishedApk).Hash -eq $state.apkHash
}

function Invoke-CheckedCommand {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$WorkingDirectory,
        [Parameter(Mandatory = $true)][string]$FailureMessage
    )
    Push-Location $WorkingDirectory
    try {
        $global:LASTEXITCODE = 0
        # Render native output without leaking it into this function's return value.
        # Otherwise callers can receive build log lines mixed with the APK path.
        & $FilePath @Arguments 2>&1 | ForEach-Object { Write-Host ([string]$_) }
        $code = $LASTEXITCODE
    } finally {
        Pop-Location
    }
    if ($code -ne 0) { throw "$FailureMessage (exit code $code)." }
}

function Publish-Apk([string]$sourceHash) {
    if (-not (Test-Path $gradleApk -PathType Leaf)) { throw "Gradle APK not found: $gradleApk" }
    if ((Get-Item $gradleApk).Length -eq 0) { throw "Gradle produced an empty APK: $gradleApk" }
    New-Item -ItemType Directory -Force $outputRoot, $buildStateRoot | Out-Null

    $temporaryApk = Join-Path $outputRoot ".$([guid]::NewGuid().ToString('N')).apk.tmp"
    $backupApk = Join-Path $outputRoot ".$([guid]::NewGuid().ToString('N')).apk.bak"
    try {
        Copy-Item -LiteralPath $gradleApk -Destination $temporaryApk
        if (Test-Path $publishedApk) {
            [IO.File]::Replace($temporaryApk, $publishedApk, $backupApk)
        } else {
            Move-Item -LiteralPath $temporaryApk -Destination $publishedApk
        }
    } finally {
        Remove-Item -LiteralPath $temporaryApk -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $backupApk -Force -ErrorAction SilentlyContinue
    }

    $apkHash = (Get-FileHash -Algorithm SHA256 $publishedApk).Hash
    $stateJson = [pscustomobject]@{
        schemaVersion = 1
        sourceHash = $sourceHash
        apkHash = $apkHash
        builtAt = (Get-Date).ToString("o")
    } | ConvertTo-Json
    $temporaryState = Join-Path $buildStateRoot ".$([guid]::NewGuid().ToString('N')).state.tmp"
    $backupState = Join-Path $buildStateRoot ".$([guid]::NewGuid().ToString('N')).state.bak"
    try {
        $stateJson | Set-Content -Encoding UTF8 $temporaryState
        if (Test-Path $statePath) {
            [IO.File]::Replace($temporaryState, $statePath, $backupState)
        } else {
            Move-Item -LiteralPath $temporaryState -Destination $statePath
        }
    } finally {
        Remove-Item -LiteralPath $temporaryState -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $backupState -Force -ErrorAction SilentlyContinue
    }
    return $publishedApk
}

function Invoke-AppBuild([switch]$OnlyWhenChanged) {
    $sourceHash = Get-AppSourceHash
    if ($OnlyWhenChanged -and -not $ForceBuild -and (Test-PublishedApkCurrent $sourceHash)) {
        Write-Host "build skipped: app source unchanged" -ForegroundColor Green
        Write-Host "APK: $publishedApk" -ForegroundColor Cyan
        return $publishedApk
    }
    if (-not (Test-Path (Join-Path $appRoot "node_modules"))) {
        throw "node_modules is missing. Run 'npm install' from $appRoot first."
    }

    $npm = (Get-Command npm.cmd -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1).Source
    $npx = (Get-Command npx.cmd -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1).Source
    $gradle = Join-Path $androidRoot "gradlew.bat"
    if (-not $npm) { throw "npm.cmd was not found in PATH." }
    if (-not $npx) { throw "npx.cmd was not found in PATH." }
    if (-not (Test-Path $gradle)) { throw "Gradle wrapper not found: $gradle" }

    try {
        Write-Progress -Activity "Building Med-Flex Auto Smart APK" -Status "1/3 Building Vue web assets" -PercentComplete 10
        Write-Host "[1/3] npm run build" -ForegroundColor Cyan
        Invoke-CheckedCommand $npm @("run", "build") $appRoot "Web build failed"

        Write-Progress -Activity "Building Med-Flex Auto Smart APK" -Status "2/3 Syncing Capacitor Android" -PercentComplete 45
        Write-Host "[2/3] npx cap sync android" -ForegroundColor Cyan
        Invoke-CheckedCommand $npx @("cap", "sync", "android") $appRoot "Capacitor Android sync failed"

        Write-Progress -Activity "Building Med-Flex Auto Smart APK" -Status "3/3 Building Android debug APK" -PercentComplete 70
        Write-Host "[3/3] gradlew assembleDebug" -ForegroundColor Cyan
        Invoke-CheckedCommand $gradle @("assembleDebug") $androidRoot "Android debug build failed"

        $result = Publish-Apk $sourceHash
        Write-Progress -Activity "Building Med-Flex Auto Smart APK" -Status "Complete" -PercentComplete 100
        Write-Host "APK ready: $result" -ForegroundColor Green
        return $result
    } finally {
        Write-Progress -Activity "Building Med-Flex Auto Smart APK" -Completed
    }
}

function Resolve-Adb {
    if ($AdbPath) {
        if (-not (Test-Path $AdbPath -PathType Leaf)) { throw "adb not found at $AdbPath" }
        return (Resolve-Path $AdbPath).Path
    }
    $command = Get-Command adb.exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($command) { return $command.Source }
    $sdkAdb = Join-Path $env:LOCALAPPDATA "Android\Sdk\platform-tools\adb.exe"
    if (Test-Path $sdkAdb -PathType Leaf) { return $sdkAdb }
    throw "adb.exe was not found. Install Android SDK Platform-Tools or pass -AdbPath."
}

function Get-AdbDevices {
    $adb = Resolve-Adb
    $global:LASTEXITCODE = 0
    $lines = @(& $adb devices -l 2>&1 | ForEach-Object { [string]$_ })
    $code = $LASTEXITCODE
    if ($code -ne 0) { throw "adb devices failed (exit code $code): $($lines -join ' ')" }

    $devices = @()
    foreach ($line in $lines) {
        if ($line -match "^([^\s]+)\s+(device|offline|unauthorized)(?:\s+(.*))?$") {
            $serial = $Matches[1]
            $state = $Matches[2]
            $details = [string]$Matches[3]
            $transport = if ($serial -match ":|_adb-tls-connect") {
                "wireless"
            } elseif ($serial -match "^emulator-") {
                "emulator"
            } else {
                "usb"
            }
            $devices += [pscustomobject]@{
                Serial = $serial
                State = $state
                Details = $details
                Connection = $transport
            }
        }
    }
    return @($devices)
}

function Show-AdbDevices($devices) {
    $devices = @($devices)
    if ($devices.Count -eq 0) {
        Write-Host "No Android debugging devices found." -ForegroundColor Yellow
        return
    }
    $number = 1
    foreach ($item in $devices) {
        Write-Host ("  {0}  {1,-24} {2,-12} {3}" -f $number, $item.Serial, $item.Connection, $item.State)
        $number++
    }
}

function Select-AdbDevice([string]$connection, [switch]$AllowNone) {
    $all = @(Get-AdbDevices)
    $matching = @($all | Where-Object {
        $_.State -eq "device" -and ($connection -eq "any" -or $_.Connection -eq $connection)
    })
    if ($Device) {
        $picked = @($matching | Where-Object { $_.Serial -eq $Device })
        if ($picked.Count -ne 1) { throw "Requested device '$Device' is not ready as $connection." }
        return $picked[0]
    }
    if ($matching.Count -eq 0) {
        $blocked = @($all | Where-Object {
            $connection -eq "any" -or $_.Connection -eq $connection
        })
        if ($blocked.Count -gt 0) {
            $states = ($blocked | ForEach-Object { "$($_.Serial)=$($_.State)" }) -join ", "
            throw "No ready $connection device. Current state: $states. Accept the debugging prompt or reconnect."
        }
        if ($AllowNone) { return $null }
        throw "No ready $connection debugging device found."
    }
    if ($matching.Count -eq 1) { return $matching[0] }
    if (-not $script:interactiveMode) {
        $names = ($matching | ForEach-Object { $_.Serial }) -join ", "
        throw "Multiple ready devices: $names. Pass -Device with the serial to select one."
    }

    Write-Host ""
    Write-Host "Choose Android device" -ForegroundColor Cyan
    Show-AdbDevices $matching
    $choice = Read-Host "Choose 1-$($matching.Count)"
    $index = 0
    if (-not [int]::TryParse($choice, [ref]$index) -or $index -lt 1 -or $index -gt $matching.Count) {
        throw "Unknown device choice: $choice"
    }
    return $matching[$index - 1]
}

function Invoke-AdbCommand([string[]]$arguments, [string]$failureMessage) {
    $adb = Resolve-Adb
    $global:LASTEXITCODE = 0
    $output = @(& $adb @arguments 2>&1 | ForEach-Object { [string]$_ })
    $code = $LASTEXITCODE
    if ($output.Count -gt 0) { $output | ForEach-Object { Write-Host $_ } }
    if ($code -ne 0) { throw "$failureMessage (exit code $code)." }
    return @($output)
}

function Install-AndLaunch([string]$apk, $target) {
    Write-Host "Installing APK -> $($target.Serial) [$($target.Connection)]" -ForegroundColor Cyan
    try {
        [void](Invoke-AdbCommand @("-s", $target.Serial, "install", "-r", $apk) "APK installation failed")
    } catch {
        throw "APK install did not complete on $($target.Serial). The device may have disconnected; " +
              "reconnect it and rerun this option. The built APK is still safe at $apk. Cause: $($_.Exception.Message)"
    }
    if (-not $NoLaunch) {
        try {
            [void](Invoke-AdbCommand @("-s", $target.Serial, "shell", "am", "force-stop", $appId) "App force-stop failed")
            [void](Invoke-AdbCommand @("-s", $target.Serial, "shell", "am", "start", "-n", "$appId/.MainActivity") "App launch failed")
        } catch {
            throw "APK installed on $($target.Serial), but launch did not complete. The device may have " +
                  "disconnected; reconnect it and launch $appId manually or rerun with -NoBuild. " +
                  "Cause: $($_.Exception.Message)"
        }
        Write-Host "launched: $appId on $($target.Serial)" -ForegroundColor Green
    }
}

function Get-DeployApk {
    if ($NoBuild) {
        if (-not (Test-Path $publishedApk -PathType Leaf)) {
            throw "No published APK exists. Run a build first: $publishedApk"
        }
        return $publishedApk
    }
    return Invoke-AppBuild -OnlyWhenChanged
}

function Read-NetworkAddress([string]$prompt) {
    $value = $Address.Trim()
    if (-not $value -and $script:interactiveMode) { $value = (Read-Host $prompt).Trim() }
    if ($value -notmatch "^([^:\s]+):(\d{1,5})$") { throw "Address must look like 192.168.1.50:5555." }
    $port = [int]$Matches[2]
    if ($port -lt 1 -or $port -gt 65535) { throw "Wireless debugging port is out of range: $port" }
    return $value
}

function Connect-Wireless {
    $targetAddress = Read-NetworkAddress "Wireless debugging address (IP:port)"
    $result = @(Invoke-AdbCommand @("connect", $targetAddress) "Wireless ADB connection failed")
    if (($result -join " ") -notmatch "(?i)connected to|already connected") {
        throw "ADB did not confirm a connection to $targetAddress."
    }
    return $targetAddress
}

function Pair-Wireless {
    $targetAddress = Read-NetworkAddress "Wireless pairing address (IP:pairing-port)"
    $code = $PairingCode.Trim()
    if (-not $code -and $script:interactiveMode) { $code = (Read-Host "Wireless pairing code").Trim() }
    if ($code -notmatch "^\d{6}$") { throw "Pairing code must be exactly 6 digits." }
    $result = @(Invoke-AdbCommand @("pair", $targetAddress, $code) "Wireless ADB pairing failed")
    if (($result -join " ") -notmatch "(?i)successfully paired|already paired") {
        throw "ADB did not confirm pairing with $targetAddress."
    }
}

function Read-Menu {
    Write-Host ""
    Write-Host "Med-Flex Auto Smart Android tool" -ForegroundColor Cyan
    Write-Host "  1  Build debug APK"
    Write-Host "  2  Check Android debugging devices"
    Write-Host "  3  Build, install, and launch via USB debugging"
    Write-Host "  4  Build, install, and launch via Wireless debugging"
    Write-Host "  5  Full auto: build; install if a ready device is found"
    Write-Host "  6  Connect Wireless debugging, then build/install"
    Write-Host "  7  Pair Wireless debugging"
    Write-Host "  8  Exit"
    $choice = Read-Host "Choose 1-8"
    $script:Action = switch ($choice) {
        "1" { "build" }
        "2" { "devices" }
        "3" { "usb" }
        "4" { "wireless" }
        "5" { "auto" }
        "6" { "connect" }
        "7" { "pair" }
        "8" { "exit" }
        default { throw "Unknown menu choice: $choice" }
    }
}

function Invoke-SelectedAction {
    if ($NoBuild -and $Action -notin @("usb", "wireless", "auto", "connect")) {
        throw "-NoBuild is valid only with USB, Wireless, auto, or connect actions."
    }
    if ($NoLaunch -and $Action -notin @("usb", "wireless", "auto", "connect")) {
        throw "-NoLaunch is valid only with an install action."
    }
    if ($ForceBuild -and $Action -notin @("build", "usb", "wireless", "auto", "connect")) {
        throw "-ForceBuild is valid only with a build or install action."
    }
    if ($Device -and $Action -notin @("usb", "wireless", "auto")) {
        throw "-Device is valid only with USB, Wireless, or auto actions."
    }
    if ($Address -and $Action -notin @("connect", "pair")) {
        throw "-Address is valid only with connect or pair actions."
    }
    if ($PairingCode -and $Action -ne "pair") {
        throw "-PairingCode is valid only with the pair action."
    }
    if ($scriptArguments.ContainsKey("Connection") -and $Action -ne "auto") {
        throw "-Connection is valid only with the auto action."
    }
    if ($AdbPath -and $Action -notin @("devices", "usb", "wireless", "auto", "connect", "pair")) {
        throw "-AdbPath is not used by the build-only action."
    }

    switch ($Action) {
        "build" {
            [void](Invoke-AppBuild)
        }
        "devices" {
            Show-AdbDevices @(Get-AdbDevices)
        }
        "usb" {
            $apk = Get-DeployApk
            $target = Select-AdbDevice "usb"
            Install-AndLaunch $apk $target
        }
        "wireless" {
            $apk = Get-DeployApk
            $target = Select-AdbDevice "wireless"
            Install-AndLaunch $apk $target
        }
        "auto" {
            $apk = Get-DeployApk
            $target = Select-AdbDevice $Connection -AllowNone
            if ($null -eq $target) {
                Write-Host "No ready Android device; APK was built only." -ForegroundColor Yellow
                Write-Host "APK: $apk" -ForegroundColor Cyan
            } else {
                Install-AndLaunch $apk $target
            }
        }
        "connect" {
            $connectedAddress = Connect-Wireless
            $apk = Get-DeployApk
            $script:Device = $connectedAddress
            $target = Select-AdbDevice "wireless"
            Install-AndLaunch $apk $target
        }
        "pair" {
            Pair-Wireless
            Write-Host "Paired. Use option 6 with the separate connection address/port shown by Android." -ForegroundColor Green
        }
    }
}

function Invoke-Program {
    if ($ForceBuild -and $NoBuild) { throw "-ForceBuild and -NoBuild cannot be used together." }
    if ($Action -eq "exit") { return }
    if (-not $script:interactiveMode) {
        Invoke-SelectedAction
        return
    }
    while ($true) {
        try {
            Read-Menu
            if ($Action -eq "exit") { Write-Host "Bye." -ForegroundColor Cyan; return }
            Invoke-SelectedAction
        } catch {
            Write-Host ""
            Write-Host "Not completed: $($_.Exception.Message)" -ForegroundColor Yellow
            Write-Host "Returning to the menu." -ForegroundColor Yellow
        }
    }
}

try {
    Invoke-Program
} catch {
    Write-Host "app-build.ps1: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
