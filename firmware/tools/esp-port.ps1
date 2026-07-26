# [TH] ตัวช่วยหา COM ของ ESP32 อัตโนมัติ ใช้ร่วมกันระหว่างเครื่องมือในโฟลเดอร์นี้
#      dot-source ไฟล์นี้ก่อนใช้: . (Join-Path $PSScriptRoot 'esp-port.ps1')
# Shared ESP32 port lookup. Dot-source it; this file defines a function and does
# nothing on its own.

# [TH] หาพอร์ตจากชิป USB-serial ที่รู้จัก (VID) ผ่าน arduino-cli
#      มีตัวเดียว -> ใช้เลย, หลายตัว -> ให้ระบุ -Port, ไม่มี -> แจ้ง error
# Match the USB-serial chip's VID against arduino-cli's JSON port list (already a
# build dependency). The CYD is a CH340; the other VIDs cover the common ESP32
# dev-board bridges so this is not board-specific. One match -> use it;
# several -> ask for an explicit port.
function Resolve-EspPort {
    # 1A86 WCH CH340/CH9102 (the CYD) · 10C4 Silicon Labs CP210x · 0403 FTDI
    # 303A Espressif native USB (S2/S3/C3)
    $knownVids = @("0x1A86", "0x10C4", "0x0403", "0x303A")
    $json = arduino-cli board list --format json | ConvertFrom-Json
    $found = @($json.detected_ports | Where-Object {
        $_.port.protocol -eq "serial" -and $knownVids -contains $_.port.properties.vid
    })
    if ($found.Count -eq 0) {
        throw "No ESP32 USB-serial port found. Plug the board in, or pass -Port COMx."
    }
    if ($found.Count -gt 1) {
        $list = ($found | ForEach-Object { "$($_.port.address) (vid $($_.port.properties.vid))" }) -join ", "
        throw "Multiple candidate ports: $list. Pass -Port COMx to pick one."
    }
    return $found[0].port.address
}
