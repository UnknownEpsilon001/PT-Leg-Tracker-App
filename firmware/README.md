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
- ArduinoJson 7.4.1 (7.4.x fine — same API)
- RTClib 2.1.4 — DS3231 on the controller, controller sketch only

```powershell
arduino-cli lib install "lvgl@8.4.0" "TFT_eSPI@2.5.43" "ArduinoJson@7.4.1" "RTClib@2.1.4"
```

XPT2046_Touchscreen is **no longer used** — see "Touch is bit-banged" below. `SD`
comes bundled with the ESP32 core, so it is not pinned here.

lvgl **must** stay on 8.4.0. Installing 9.x breaks the display sketch with a wall
of renamed-API errors (`lv_disp_draw_buf_t`, `LV_IMG_CF_TRUE_COLOR_ALPHA`,
`lv_image_header_t has no member named 'always_zero'`) — that is version drift,
not a code problem. Reinstall the pin and rebuild with `--clean`.
[TH] ต้องใช้ lvgl 8.4.0 เท่านั้น ถ้าเผลออัปเป็น 9.x จะคอมไพล์จอไม่ผ่านเพราะชื่อ API เปลี่ยน

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

## UI language: Thai

The device UI is **Thai**, matching the phone app (since 2026-07-25). Text is drawn
with Kanit fonts generated at 14/20/28 px into `font_th_14.c`, `font_th_20.c` and
`font_th_28.c`. The 48 px clock stays Montserrat — it shows only digits.

**Wording must avoid stacking a vowel and a tone mark on the same consonant.**
LVGL has no shaping, so two marks land at the same height and overprint. This is
not a style preference, it is the one hard constraint; it is why reps read `รอบ`
rather than ครั้ง, and why "no link" reads `ไม่พบตัวควบคุม` rather than
ไม่เชื่อมต่อ. Check any new string against it before shipping.

Left in Latin on purpose: the clock, the on-screen keyboard, and everything typed
through it (SSID, password, server URL, device code).

`LV_SYMBOL_*` icons live in Montserrat's 0xF000 range, which the generated fonts
do not carry. `ui.cpp` copies each font struct into RAM and sets `.fallback` to the
matching Montserrat size, so icons resolve through the fallback chain. The copy is
needed because the generated structs are `const`, hence in unwritable flash.

### What the 2026-07-21 Thai attempt actually hit

That attempt concluded LVGL 8 lays Thai vowel/tone marks out "as separate
advancing glyphs" and that Thai therefore needs a shaping engine. **A re-test on
hardware 2026-07-25 disproved this.** The original font was generated compressed
while `lv_conf.h` keeps `LV_USE_FONT_COMPRESSED 0`, so *every* glyph lookup
failed and the label drew nothing; the boxes visible on screen were only the
characters the font lacked entirely, drawn as `LV_USE_FONT_PLACEHOLDER`
rectangles. The device says so plainly over the link:

```
lv_font_get_bitmap_fmt_txt: Compressed fonts is used but LV_USE_FONT_COMPRESSED is not enabled in lv_conf.h
lv_draw_sw_letter: lv_draw_letter: character's bitmap not found
```

Two rules for regenerating these fonts:

- pass `--no-compress` (or flip `LV_USE_FONT_COMPRESSED`, which costs runtime CPU)
- pass **both** fontsource woffs — `kanit-latin-*` and `kanit-thai-*` are separate
  files, and the Thai one carries no Latin at all

```powershell
# run from the repo root; repeat for each size the UI uses
foreach ($s in 14, 20, 28) {
  npx lv_font_conv `
    --font PT-Leg-Tracker/node_modules/@fontsource/kanit/files/kanit-latin-400-normal.woff --range 0x20-0x7F `
    --font PT-Leg-Tracker/node_modules/@fontsource/kanit/files/kanit-thai-400-normal.woff --range 0x0E00-0x0E7F `
    --size $s --bpp 2 --no-compress --format lvgl --lv-include lvgl.h `
    -o "firmware/pt-leg-display/font_th_$s.c"
}
```

Built that way, Thai renders on the CYD **without** a shaping engine. Kanit gives
combining marks `adv_w = 0` and a negative `ofs_x`, so LVGL draws each mark back
over the consonant it follows and never advances the cursor.

The one genuine limit is the double stack described above: both marks sit at
`ofs_y = +15`, so `ตั้งค่า` (ต + ั + ้) overprints while single-mark syllables
like `ค้าง` and `พัก` are clean. Far narrower than "needs a shaping engine", but
it does constrain wording permanently.

A fast way to check a font before flashing: run `firmware/tools/fake-controller.ps1`
and grep its output for `bitmap not found`. The display logs one such line per
unresolved glyph, so a silent-but-blank font is loud in the log.

## Touch is bit-banged, because the SD card owns VSPI

The ESP32 has two usable SPI peripherals. TFT_eSPI holds HSPI (12/13/14/15), and
the CYD's microSD socket is hard-wired to VSPI's pins (CS 5, SCK 18, MISO 19,
MOSI 23) — there is no third bus for the touch panel. Touch therefore moved to a
software SPI read in `pt-leg-display/touch_bb.cpp` (pins unchanged: MOSI 32,
MISO 39, CLK 25, CS 33, PENIRQ 36), and `XPT2046_Touchscreen` was dropped.

The usual bit-bang driver for this chip is GitHub-only and not in the Arduino
library index, so it could not be pinned like everything else here; the protocol
is one command byte and two data bytes per axis, so it is vendored instead.

The old library applied `setRotation(1)` internally. `touch_bb` returns raw panel
counts, so the axis swap and flip now live in `touchCb` in `ui.cpp`. Those
`map()` ranges are a **starting point and need a bench pass** — the display logs
one `touch raw=..,.. -> x,y` line per press for exactly that purpose.
[TH] ทัชสกรีนเปลี่ยนมาอ่านด้วยการกระตุกขาเอง เพราะการ์ด SD ต้องใช้บัส VSPI
     ค่าคาลิเบรตในตอนนี้เป็นค่าตั้งต้น ต้องปรับจูนกับเครื่องจริงอีกครั้ง

### The first CYD's touch panel is dead — what was ruled out (2026-07-27)

Nothing on that unit's screen responds to a press, and the calibration above was
never reached because no press is ever detected. The bench pass ruled out the
firmware entirely, so **do not debug `touch_bb.cpp` again from scratch**:

- `touchBBBegin()` runs, pins are configured, and nothing else on the board claims
  GPIO 25/32/33/36/39.
- PENIRQ (36) reads HIGH in every sample, pressed or not.
- The chip reports no pressure: Z1 reads `0` and Z2 reads ~`4090` (the untouched
  signature) while a finger is held on the panel.
- Slowing the bit-bang clock to ~500 kHz changed no reading.
- `XPT2046_Touchscreen` 1.4 on hardware SPI — a known-good reference driver, with
  the library's own Z-pressure detection instead of PENIRQ — reads `z=0 x=0 y=0`
  too, so this is not a bit-bang bug.
- The other two pin sets a CYD revision might use (touch on the TFT bus,
  CLK 14 / MOSI 13 / MISO 12, with CS 33 or 15) are equally silent.
- An I2C scan on 33/32, 21/22, 32/33 and 27/22 finds no device, so it is not a
  capacitive variant (CST816/CST820) wired to a different controller.

Two drivers, three pin sets and an I2C sweep all get nothing: the touch
controller is not answering on any interface this board could use. The remaining
causes are physical — an unseated touch flex, or a faulty unit. Retest on a
second CYD before touching the driver code.
[TH] จอสัมผัสของบอร์ด CYD ตัวแรกไม่ตอบสนองเลย ตรวจแล้วว่าไม่ใช่ปัญหาจากเฟิร์มแวร์
     ทั้งไดรเวอร์สองแบบ ชุดขาสามชุด และการสแกน I2C ล้วนไม่พบการตอบสนอง
     สาเหตุที่เหลืออยู่คือทางกายภาพ เช่น สายแพหลุดหรือบอร์ดเสีย ให้ลองกับบอร์ดตัวที่สองก่อนแก้โค้ด

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

## Erase the flash when the partition scheme changes

If a board boot-loops with `No bootable app partitions in the partition table`,
or a freshly flashed sketch prints its first line and then goes quiet, the flash
holds a partition table and OTA data from a different scheme. Uploading a new app
does not fix it, because the stale OTA record at `0xe000` still points at a slot
the new table does not have:

```powershell
$esptool = "$env:LOCALAPPDATA\Arduino15\packages\esp32\tools\esptool_py\4.5.1\esptool.exe"
& $esptool --chip esp32 --port COM4 --baud 921600 erase_flash
```

Then flash normally. This costs the NVS settings too, which on the controller
means Wi-Fi credentials and the device code.

## Running the display without a controller

`pt-leg-display.ino` has a `LINK_OVER_USB` switch. Set it to `1` and the
controller link moves from `Serial2` to the USB port, so a PC can impersonate the
controller and drive the whole UI with one board on the desk:

```powershell
# flip LINK_OVER_USB to 1 in pt-leg-display.ino, then
arduino-cli compile --fqbn "esp32:esp32:esp32:PartitionScheme=huge_app" --upload -p COM4 firmware/pt-leg-display
# port auto-detected from the USB-serial chip; pass -Port COMx to override
.\firmware\tools\fake-controller.ps1 -AutoStart
```

The script answers `hello`, reacts to START/STOP touches, and runs a compressed
LIFT/HOLD/LOWER/REST cycle so the phases visibly move. Stop it and the display
falls back to `NO LINK` after ~2.5 s. **Set the switch back to `0` before
flashing a real machine** — do not use `--build-property` to set it, as flag
changes against cached objects have produced broken images.

## Running the controller without a display

The panel buttons hang off the controller, so their polarity can be checked with
that board alone — no CYD, no UART jumpers. Two things make this work:

- the display link is on `Serial2` (GPIO 16/17), so USB serial (UART0) is free;
- the UART-loss safe-stop is guarded by `gLink.lastRxMs() != 0`, so a board that
  never saw a display does not stop itself.

Build with `BENCH_DEBUG=1` for a running commentary on the USB port — button
edges, raw pin levels, limit-switch state, phase and rep changes, all printed on
change only:

```powershell
arduino-cli compile --fqbn "esp32:esp32:esp32:PartitionScheme=huge_app" `
  --build-property compiler.cpp.extra_flags=-DBENCH_DEBUG=1 --clean `
  --upload -p COM4 firmware/pt-leg-controller
```

`--clean` is required: the flag changes a `#define`, and Arduino's cache happily
reuses stale objects compiled without it. The source line stays at `0`, so the
flag cannot be committed enabled.

The flag is `BENCH_DEBUG`, **not** `DEBUG_SERIAL`, and must not be renamed back:
Adafruit_BusIO (pulled in by RTClib) uses `DEBUG_SERIAL` as a `Stream` object, so
defining it as `1` on the command line turns every `DEBUG_SERIAL.print(...)` in
that library into `1.print(...)` and the controller stops compiling.
[TH] ต้องใช้ชื่อ `BENCH_DEBUG` ห้ามกลับไปใช้ `DEBUG_SERIAL` เพราะไลบรารี Adafruit_BusIO
ใช้ชื่อนี้เป็นออบเจ็กต์ Stream อยู่แล้ว จะทำให้คอมไพล์ไม่ผ่าน

**Unwired NC inputs read as pressed.** `SWITCH_NC` + `INPUT_PULLUP` means a
closed contact pulls the pin LOW and an open one floats HIGH, and an absent wire
is indistinguishable from an open contact — that is the fail-safe, not a bug. A
bare board therefore boots with START, STOP and RESET all "pressed", firing one
edge each on the first debounce pass. For a bench test, jumper each button pin to
GND to stand in for the closed contact, and lift the jumper to press. **Ground
GPIO 32 and 33 as well before starting** — see the fault note below:

| Signal | Pin | Jumper to GND | Lift jumper |
| ------ | --- | ------------- | ----------- |
| START  | 25  | at rest       | press       |
| STOP   | 26  | at rest       | press       |
| RESET  | 4   | at rest       | press       |
| TOP limit    | 32 | not at limit | leg fully up   |
| BOTTOM limit | 33 | not at limit | leg fully down |

`firmware/tools/button-bench.ps1` walks that test as a pass/fail checklist: it
opens the port, reboots the board, tells you which jumper to move next, and fails
a step if the expected lines do not arrive within the timeout.

```powershell
.\firmware\tools\button-bench.ps1            # guided checklist, port auto-detected
.\firmware\tools\button-bench.ps1 -Tail      # just stream the debug output
.\firmware\tools\button-bench.ps1 -DryRun    # replay a canned transcript, no board
```

It matches patterns **in order**, because the firmware prints the button edge
first and the resulting state right after it; unordered matching let a state line
printed before the press satisfy a step nobody had performed yet. `-DryRun`
replays a transcript copied from the sketch's own `printf` formats, so a pattern
that drifts out of sync with the firmware fails at the desk rather than on the
bench. Exit code is the number of failed steps.

Leaving the limit pins floating does **not** give a fast cycle. Both read
AT-LIMIT at once, which is physically impossible, so `SessionSM::tick` calls it a
wiring fault and the session goes straight to FAULT — verified on the bench
2026-07-26, where a bare board went `SAFE-LOWER` → `FAULT` 13 ms after boot:

```
[dbg 247] EDGE START / EDGE STOP / EDGE RESET   <- three floating buttons
[dbg 247] limits top=AT-LIMIT bottom=AT-LIMIT
[dbg 260] phase=FAULT reps=0 elapsed=0s
```

So ground 32 and 33 for the whole button test. With them grounded, neither limit
ever closes, and the phases run on the max-travel cap alone: START gives LIFT for
8 s and then FAULT (net 1), while STOP gives SAFE-LOWER for 8 s and then IDLE —
the safe-stop path deliberately ends idle rather than faulted.

## LVGL logging and asserts

`lv_conf.h` enables `LV_USE_LOG` at WARN with `LV_LOG_PRINTF`, and sets
`LV_ASSERT_HANDLER` to `esp_restart()`. The stock handler is `while(1)`, which
turns an LVGL out-of-memory into a frozen screen with a dead touchscreen, no
reboot and nothing on serial — a failure mode that cost real debugging time on
2026-07-21. A reboot with a printed reason is far easier to diagnose, and on a
patient-facing machine it recovers instead of appearing bricked.

## Wiring

- Controller relays: UP GPIO 22, DOWN GPIO 27. Limit switches: TOP GPIO 32,
  BOTTOM GPIO 33 (INPUT_PULLUP, `SWITCH_NC` in `config.h` if normally-closed).
- UART: controller GPIO 16 (RX) / 17 (TX) ↔ CYD GPIO 22 (RX) / 27 (TX),
  cross-wired, common GND. 115200 8N1. Pins to be confirmed on the bench.
