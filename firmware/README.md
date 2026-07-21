# PT Leg Device Firmware

Two ESP32s per machine:

| Sketch | Board | Job |
|---|---|---|
| `pt-leg-controller/` | plain ESP32 dev board (`esp32:esp32:esp32`) | relays, limit switches, motion state machine, Wi-Fi + server client, NVS settings |
| `pt-leg-display/` | ESP32-2432S028R "Cheap Yellow Display" (`esp32:esp32:esp32`) | LVGL touch UI only |

They talk over UART (115200 8N1, cross-wired RX↔TX + common GND). Protocol:
`link-protocol.md`.

Spec: `../docs/superpowers/specs/2026-07-19-esp32-firmware-design.md`
Plan: `../docs/superpowers/plans/2026-07-19-esp32-firmware.md`

## Pinned libraries (install these exact versions)

- lvgl 8.4.0 — `lv_conf.h` lives next to the library folder, see setup below
- TFT_eSPI 2.5.43 — `User_Setup.h` for the CYD: `TFT_eSPI_User_Setup.h`, copy over the library's own
- XPT2046_Touchscreen 1.4
- ArduinoJson 7.4.1 (7.4.x fine — same API)

```powershell
arduino-cli lib install "lvgl@8.4.0" "TFT_eSPI@2.5.43" "XPT2046_Touchscreen@1.4" "ArduinoJson@7.4.1"
```

Core: `arduino-cli core install esp32:esp32` (board manager URL
`https://espressif.github.io/arduino-esp32/package_esp32_index.json`).

## One-time library config (both files live in this repo, copied into the libraries dir)

Arduino IDE / arduino-cli has no per-sketch config for these two libraries, so
they are configured globally. The repo copies are the source of truth — re-copy
after any library reinstall.

```powershell
$lib = "$env:USERPROFILE\Documents\Arduino\libraries"
# TFT_eSPI panel config for the CYD (keep a .orig backup of the stock file)
Copy-Item firmware/TFT_eSPI_User_Setup.h "$lib\TFT_eSPI\User_Setup.h" -Force
# lv_conf.h must sit NEXT TO the lvgl folder, not inside it
Copy-Item firmware/lv_conf.h "$lib\lv_conf.h" -Force
```

`firmware/lv_conf.h` is lvgl 8.4.0's `lv_conf_template.h` with only these
changed: enable block `#if 1`, `LV_COLOR_16_SWAP 1`, `LV_TICK_CUSTOM 1`
(millis()), Montserrat 20/28/48 fonts on. `LV_COLOR_DEPTH 16` and
`LV_MEM_SIZE 48K` are already the template defaults.

## UI language: English

The device UI is **English**, unlike the phone app. A Kanit-based LVGL font was
generated and tried on hardware (2026-07-21) and Thai rendered wrong: LVGL 8 has
no complex-script shaping, so Thai vowel/tone marks that stack above and below
the base consonant are laid out as separate advancing glyphs. Fixing that means
a shaping engine, which is out of proportion for a screen showing six words.
Short English words (`START`, `HOLD`, `SWITCH FAULT`) use the built-in Montserrat
fonts and stay legible at 20–48 px.

## Shared files between the two sketches

`link.h`, `link.cpp`, `session.h` and `settings.h` exist in **both** sketch
directories because an Arduino sketch cannot include sources from outside its own
folder. The copies in `pt-leg-controller/` are the source of truth.

```powershell
.\firmware\sync-shared.ps1          # copy controller -> display
.\firmware\sync-shared.ps1 -Check   # report drift only, exit 1 if any
```

Run the `-Check` form before flashing a release. Undetected drift means the UART
`settings` message silently loses a field and the boards disagree about the
machine's configuration.

## Build

```powershell
arduino-cli compile --fqbn "esp32:esp32:esp32:PartitionScheme=huge_app" firmware/pt-leg-controller
arduino-cli compile --fqbn "esp32:esp32:esp32:PartitionScheme=huge_app" firmware/pt-leg-display
```

**`PartitionScheme=huge_app` is required, not optional.** The default scheme
reserves half of the 4 MB flash for OTA and leaves 1.31 MB for the app; the
display sketch (LVGL + Thai font + the Wi-Fi stack the settings-screen SSID scan
pulls in) lands at 97% of that. `huge_app` gives 3 MB and drops OTA, which this
design never uses — both boards are flashed over USB.

## Flash

```powershell
arduino-cli upload -p COMx --fqbn "esp32:esp32:esp32:PartitionScheme=huge_app" firmware/pt-leg-controller
arduino-cli upload -p COMy --fqbn "esp32:esp32:esp32:PartitionScheme=huge_app" firmware/pt-leg-display
```

## Wiring

- Controller relays: UP GPIO 22, DOWN GPIO 27. Limit switches: TOP GPIO 32,
  BOTTOM GPIO 33 (INPUT_PULLUP, `SWITCH_NC` in `config.h` if normally-closed).
- UART: controller GPIO 16 (RX) / 17 (TX) ↔ CYD GPIO 22 (RX) / 27 (TX),
  cross-wired, common GND. 115200 8N1. Pins to be confirmed on the bench.
