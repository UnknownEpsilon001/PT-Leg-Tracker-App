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

### The Y axis was inverted, which reads as "touch does not work" (2026-07-27)

Presses registered all along; they just landed in the wrong place, so no button
ever received a click and the screen felt dead. Tapping the gear button — a known
target at x≈288, y≈24 — produced `touch raw=~700,~3450 -> 285,207`: the X result
was right and the Y result was 240 minus the truth. The `map()` for Y ran
descending (`3700 -> 200`) when the panel counts ascend with screen Y.

Two traps to avoid when re-tuning this:

- **Calibrate against on-screen targets, not "corners".** Corner taps are
  ambiguous — the panel's idea of a corner and the rotated UI's idea need not
  agree, and a corner sample sent this hunt off in the wrong direction for an
  hour. The gear and list buttons have known coordinates; use those.
- **A press only prints on the press edge**, so a serial capture started after the
  tap shows nothing. An empty capture is not evidence that touch is dead.

Ruled out along the way, so nobody repeats it: the driver reaches the chip on
pins 25/32/33/36/39 (the other CYD pin sets, touch on the TFT bus at
CLK 14 / MOSI 13 / MISO 12, are silent), and an I2C sweep finds no capacitive
controller, so this board is the resistive XPT2046 variant it claims to be.
[TH] อาการที่เหมือนทัชสกรีนเสียแท้จริงคือแกน Y กลับด้าน จุดที่แตะจึงไปตกคนละที่กับปุ่ม
     ให้ปรับค่าโดยอ้างอิงปุ่มที่รู้พิกัดบนจอ ไม่ใช่การแตะที่มุมจอซึ่งกำกวมเพราะจอถูกหมุน
     และการอ่านค่าจะพิมพ์เฉพาะตอนเริ่มกด ถ้าเก็บ log ไม่ทันจะดูเหมือนไม่มีการแตะเลย

## Device settings live on the card (`/settings.json`)

Settings are stored twice: as JSON on the CYD's microSD card, and in the
controller's NVS. The card is the master copy — a readable file is pushed to the
controller at boot — so a Wi-Fi password can be typed on a PC rather than on the
touchscreen, and a controller board can be swapped without retyping anything.

```json
{
  "wifiSsid": "ClinicWiFi",
  "wifiPass": "hunter2",
  "serverUrl": "http://192.168.0.12:8000",
  "deviceCode": "default",
  "maxTravelSec": 8,
  "holdSec": 10,
  "restSec": 10
}
```

Missing keys take their default; unknown keys are ignored. The settings screen
states which source is in use, because a stale card reconfigures a working
machine by design.

Three things caught us on the bench, all of them things a clinic tech would hit:

- **The name must be exactly `settings.json`.** With Explorer hiding known
  extensions, renaming the file by hand produces `settings..json`, and the
  display then reports `SD: การ์ดยังไม่มีไฟล์` and quietly keeps its old
  configuration. Turn extensions on before editing, or edit in place.
- **A UTF-8 BOM is fine.** Notepad's "UTF-8 with BOM" used to be quarantined as a
  malformed file; the loader now steps over the three bytes.
- **A card can go in while the machine is running.** The settings are re-read
  when the settings screen is opened, so edit on a PC, insert, tap the gear — no
  power cycle. A card inserted and never looked at is not read.

| Situation | Behaviour |
|---|---|
| No card | Runs from NVS; saves reach the controller only, and say so |
| File absent | Seeded from the controller's current settings, so a blank card does not blank the machine |
| Malformed file | Renamed to `/settings.bad.json`, a fresh one written, machine runs |
| Card pulled mid-save | New values go to `/settings.tmp`; the previous file is moved to `/settings.old.json` and only deleted once the new one holds its name, so a settings file always exists |
| Interrupted save found at boot | `/settings.old.json` is loaded and renamed back — the save is lost, the configuration is not |
| Card swapped while running | The new card's file is read the next time the settings screen opens |
| Out-of-range value in the file | Clamped to the settings screen's own limits (`maxTravelSec` 1-20, `holdSec`/`restSec` 1-60) and logged, because `maxTravelSec` is the relay safety cap |

The Wi-Fi password is plaintext on removable media. That is inherent to editing
it on a PC, and accepted for a clinic LAN.
[TH] ค่าตั้งค่าเก็บสองที่: ไฟล์ JSON บนการ์ด SD ของจอ และ NVS ของคอนโทรลเลอร์
     โดยยึดค่าจากการ์ดเป็นหลัก แก้รหัสผ่าน WiFi จากคอมพิวเตอร์ได้ และเปลี่ยนบอร์ดคอนโทรลเลอร์
     ได้โดยไม่ต้องพิมพ์ใหม่ รหัสผ่านเก็บเป็นข้อความธรรมดาบนการ์ด ยอมรับได้สำหรับเครือข่ายในคลินิก

## Thai keyboard

Both on-screen keyboards carry Thai, defined in `pt-leg-display/thai_kb.cpp` and
installed into LVGL's `USER_1` (unshifted) and `USER_2` (shifted) modes. The
layout is Kedmanee in the row order a Thai keyboard has — number row first, then
the three letter rows — at twelve columns and 26 px keys. Ten wide columns were
tried first for bigger keys and rejected: moving บ and ล off the end of the first
row shifts every key after them, and the layout stops matching the one in the
typist's hands.

The add-patient keyboard opens on Thai for the name field and on Latin for the
code and age fields; the age field accepts digits only, so a Thai keyboard there
types nothing at all. The settings keyboard opens on Latin, because `deviceCode`
is matched byte-for-byte by the server and `serverUrl` must resolve.

Three things here are not obvious and were each found the hard way:

- **The stock LVGL handler cannot be kept.** `lv_keyboard_def_event_cb`
  recognises only the control keys in its own maps and types anything else into
  the textarea, so a page-switch key from a user map lands inside the patient's
  name. `thaiKbApply` removes it, says so on the serial line if the removal
  fails, and installs a replacement that owns every control key.
- **The Latin pages are ours too.** LVGL's stock bottom row is
  close/left/space/right/OK with no globe key, so a keyboard switched to Latin
  could never get back to Thai — and the mode outlives the screen, so reopening
  the form did not rescue it either.
- **Combining marks need a base on their key.** Kanit gives `ิ ี ่ ้ ุ ึ` and the
  rest `adv_w 0` and a negative `ofs_x`, so alone on a key they are drawn left of
  the label box and clipped away — every mark key rendered blank. Their labels
  carry **two** leading spaces (one is not wide enough) and the handler types
  only the mark.

Combining marks stack correctly at thai14, which is what the roster and the text
fields use. At thai20/28 the marks share a baseline and overlap — see
`docs/superpowers/specs/2026-07-28-thai-keyboard-design.md` for the measurements.
[TH] แป้นพิมพ์ไทยอยู่ในโหมด USER_1 และ USER_2 ของ LVGL ผังแบบเกษมณี สิบสองคอลัมน์ ปุ่มกว้าง 26 พิกเซล
     ฟอร์มลงทะเบียนเปิดเป็นไทยเฉพาะช่องชื่อ ส่วนช่องรหัสและอายุเป็นอังกฤษ หน้าตั้งค่าเปิดเป็นอังกฤษ
     ปุ่มสระและวรรณยุกต์ต้องมีช่องว่างนำหน้าสองช่อง ไม่งั้นจะถูกวาดนอกกรอบและปุ่มจะว่างเปล่า

## Driving the UI from a PC (`UI_TEST_HOOK`)

`pt-leg-display/uitest.h` carries a flag that is `0` in every commit. Set it to
`1` for a bench run and the display takes commands on the USB serial line:
`tap X Y`, `swipe X1 Y1 X2 Y2`, `type TEXT`, `clear`, `ta`, `screen`, `mem`,
`cat PATH`, `known`, `shot`, `shot x1 y1 x2 y2`, `reset`. `known` unlocks SAVE
and RESET DEFAULTS without a controller attached. Taps use screen coordinates and bypass the touch
panel's calibration, so a scripted run repeats exactly.

`shot` streams the screen back as one base64 line per pixel row — there is no
framebuffer to read, so the display flush callback tees each strip out as LVGL
draws it. A full frame is about 200 KB and takes roughly twenty seconds at
115200; a rectangle is much quicker and is the way to inspect one label.

It cannot be enabled alongside `LINK_OVER_USB` — both own the port, and the build
fails with an `#error` if you try. And it cannot see anything the firmware does
not know: a field holding `เกิ่น` reads the same whether the glyphs drew or came
out as boxes. That is what `shot` is for.
[TH] แฟล็กในไฟล์ uitest.h ต้องเป็น 0 ทุกครั้งที่ commit เปิดเป็น 1 เฉพาะตอนทดสอบ
     แล้วสั่งงานจอผ่านสาย USB ได้ ทั้งแตะ พิมพ์ อ่านค่า ดูหน่วยความจำ ดึงไฟล์ และจับภาพหน้าจอ
     เปิดพร้อม LINK_OVER_USB ไม่ได้ เพราะใช้พอร์ตเดียวกัน

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

The script answers `hello`, takes `s`/`x`/`r` on the keyboard in place of the
controller's panel buttons, and runs the real LIFT/HOLD/LOWER/REST cycle so the
phases visibly move — shorten it with `-HoldSec`/`-RestSec`. Stop it and the
display falls back to `ไม่พบตัวควบคุม` (no link) after ~2.5 s. **Set the switch back to `0` before
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
bare board therefore boots with RUN and RESET both reading "pressed", but
fires **no** edges: the debouncer is seeded from the pin state at power-up, so an
edge needs a release-then-press that a floating pin never produces. For a bench
test, jumper each button pin to GND to stand in for the closed contact, and lift
the jumper to press. **Ground GPIO 32 and 33 as well before starting** — see the
fault note below:

| Signal | Pin | Jumper to GND | Lift jumper |
| ------ | --- | ------------- | ----------- |
| RUN    | 25  | at rest       | press       |
| RESET  | 4   | at rest       | press       |
| TOP limit    | 32 | not at limit | leg fully up   |
| BOTTOM limit | 33 | not at limit | leg fully down |
| STATUS LED   | 23 | — output, see below | — |

RUN is a **toggle**: one press starts a session when the machine is idle with the
leg down, and stops it — by lowering first — in every other state. The debug
build prints which of the two a press meant, e.g.
`EDGE RUN (would START, polled)`, so an edge can be checked against the response.

The status LED on GPIO 23 is the out-of-band readout for the whole bench run: lit
while a session is in progress, dark when idle or in a latched FAULT. It never
blinks, so the only thing to watch is that it switches at the phase boundaries and
nowhere else. Details in [The status LED](#the-status-led) below.

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
wiring fault and the session goes straight to FAULT the moment one is started. (A
bare board no longer starts one by itself — see the seeding note above. The
transcript that used to sit here showed a board faulting 13 ms after boot off
floating buttons, which the seeding made impossible.)

So ground 32 and 33 for the whole button test. With them grounded, neither limit
ever closes, and the phases run on the max-travel cap alone: a starting RUN press
gives LIFT for 8 s and then FAULT (net 1), while a stopping one gives SAFE-LOWER
for 8 s and then IDLE. The *phase* deliberately ends idle rather than faulted, but
the *session* is flagged faulted whenever the leg never reached the bottom
switch — so a bench stop produces `done.fault=true` and a `fault=1` row on the
card. Expected with the limits jumpered to GND, and the thing to look for on a
real machine.

**A start is refused while RUN has read pressed since power-up.** On an NC button
an open line reads pressed forever and can never raise an edge, so the panel would
have no stop at all for the whole power cycle. Wiring RUN to GND and lifting the
jumper once clears the suspicion; until then a starting press prints
`start refused: RUN has read pressed since power-up, check its wiring` and does
nothing. A *stopping* press is never refused, by this gate or any other.

## The status LED

A 12 V panel lamp on **GPIO 23**, switched by a mechanical relay. It carries one
bit, and it is the bit that matters standing next to the machine:

| Lamp | Means |
| ---- | ----- |
| Lit | a session is in progress and the actuator may move — keep clear |
| Dark | idle, a latched FAULT, or no power; nothing will move on its own |

Lit is exactly `SessionSM::running()` — LIFT, HOLD, LOWER, REST **and** the
safe-lower. A latched FAULT reads dark because the relays genuinely are off in
that state. Faults are diagnosed on the CYD, the serial log and the card row; the
lamp only answers "is it about to move".

**Why there is no blinking.** The relay makes every state change a contact
operation and an audible click. A 5 Hz fault blink is 18,000 operations an hour,
which retires a ~100k-operation relay in about six hours of one latched fault —
and an idle heartbeat would grind it down while the machine sat doing nothing at
all. Solid states cost two operations per session: at twenty sessions a day that
is roughly seven years. An earlier version of this file specified 2.5 Hz / 5 Hz /
heartbeat patterns; those were written for a 3.3 V LED driven straight off the pin
and do not survive contact with a relay.

If the lamp is ever moved onto a MOSFET or an SSR the constraint disappears and
patterns become free again — but the semantics above are the ones the panel is
built and documented around.

`statusLedBegin()` writes the level **before** `pinMode(OUTPUT)`, which is the
opposite order to `actuatorBegin()`. A pin leaves reset as a high-Z input, and
`pinMode(OUTPUT)` makes it drive whatever the output register holds — LOW. On an
active-low relay board LOW is *energized*, so mode-then-write flicks the lamp on
at every boot. The motor relays get away with the other order only because they
are active-high.

There is deliberately **no power-on lamp test**. It would prove the bulb at every
boot, but the lamp is specified to light only while the machine is working, and a
flash plus a click at power-up reads as the machine starting by itself — the one
impression this panel must never give. Test the lamp by starting a session.

Set `LED_ACTIVE_LOW` in `config.h` if your relay board energizes on LOW, as most
cheap opto-isolated modules do. **Check this before first power-up**: backwards,
the lamp is lit whenever the machine is idle and dark while it is working, which
is the dangerous way round.

**Why 23.** It is free, and it is not a strapping pin — 0/2/5/12/15 are, and an
LED on 12 in particular holds the flash-voltage select high at reset and stops the
board booting at all. GPIO 26, which the retired STOP button freed, would have
worked just as well; nothing reads it and nothing reserves it. The one case where
26 is a bad choice is an older machine that still has a STOP button physically
wired to that header, because an output tied to a switch that shorts to GND is a
dead short on every press. A panel built to this two-button design has no such
wire.

## The safety nets, and what each one actually covers

Seven things stand between a fault and a patient's leg. They are listed here
together because several of them look redundant and are not, and because two of
them were added after a review found the leg could be left in the air.

**1. Break-before-make, plus a 50 ms dead time.** `actuator.cpp` is the only
file that touches the relay pins. It always de-energizes both before energizing
one, and on a direction reversal it drops both, waits `DEADTIME_MS`, and
energizes the new direction from `actuatorTick()`. The wait matters because
commanding the relays apart is not the same as their contacts being apart: a RUN
press during a lift goes from UP to SafeLower's DOWN in a single loop iteration, and
without the wait that reversal relied on the UP relay releasing faster than the
DOWN one operates — a property of the parts, not of this code, and the two are
on different connectors.

**2. The travel cap, enforced twice.** `actuatorTick()` checks it every loop,
and an `esp_timer` one-shot armed on every energize cuts the relays from the
timer task if the loop does not get there. The loop-side check alone was not a
second net: the failures that make a stroke run long — a network call blocking
for its timeout, a long flash write — are the same failures that stop the check
running. The timer covers a blocked loop, and it runs on the other core so it
survives one. It does **not** cover a panic (neither core runs) and it is
delayed, not defeated, by a flash erase, which stalls both CPUs. If it cannot be
created at boot, `setup()` prints `cap timer missing, the hardware backstop is
OFF` — that message means the machine is down to net-1 only.

**3. Both limit switches pressed at once is a wiring fault** (net 2), because it
is physically impossible. SafeLower is exempt: the usual cause is one open
switch line reading pressed forever, and faulting *during* a lowering is what
strands the leg. A safe-lower only ever drives DOWN and is time-capped, so
letting it finish is bounded and always moves the leg the safe way.

**4. A fault does not trap the leg.** Both RUN and RESET lower it from a
latched Fault. Before that, a travel-timeout fault parked the leg wherever it
stopped — often halfway up — and STOP did nothing, RESET only cleared the latch,
and START drove the actuator *further up* into whatever had jammed it. The
lowering reports nothing of its own: the session was already recorded when it
faulted, and a second report would put a duplicate row on the card.

Note what RUN does **not** do in a Fault: it never clears the latch. Acknowledging
a fault is RESET's job alone, so the button an operator just used to stop the
machine cannot also dismiss the reason it stopped. Restarting after a fault is
therefore RESET (which lowers on the way to Idle) and then RUN.

**Any** safe-lower — a recovery, an ordinary stop, or a lower started from Idle —
returns to Idle only if the bottom switch confirmed the arrival; otherwise it
latches Fault. A descent that ran its cap out without reaching the bottom used to
leave the leg in the air with the screen reading "พร้อม" (ready) and no button
able to lower it, because at the time Idle had no DOWN command at all — and a
failed DOWN relay or a jammed actuator looks exactly like that. Latching Fault
keeps the failure visible and keeps RUN and RESET commanding descents. `maxTravelSec` is sized for a lift, not necessarily for a
full recovery descent, so more than one press can legitimately be needed.

Two details that keep that from becoming its own trap. A bottom switch already
closed at the first tick of a stroke means the leg is **already down**, not that
the switch is lying — so a fault latched at the bottom of a stroke does not
drive into the mechanical stop for the full cap trying to confirm something that
was true before it started. And after a descent that could not confirm, a second
RESET clears the fault instead of driving again; repeating a descent that did
not work is not help, it is another eight seconds against whatever is in the
way. RUN still commands another descent for an operator who wants one.

**5. The bottom switch is only believed if it was clear when the stroke began.**
A leg that still needs lowering cannot already be at the bottom, so a bottom
line reading pressed from the first tick is an open circuit, not an arrival.
Without this, a recovery lower after the canonical net-2 fault ended about 25 ms
in — as soon as the top switch debounced clear, the lying bottom switch was
believed — leaving the leg up after travelling a couple of centimetres. A
safe-lower that runs its cap out without a confirmed arrival ends in Idle by
design but is reported as `fault=true`: the phase is a clean stop, the session
is not.

**6. Only a stop can be recovered from the interrupt.** Buttons are level-sampled
once per loop, and one iteration can block for most of a second inside a network
call against an unreachable server — long enough for a normal press to begin and
end unseen. A pin-change interrupt therefore times each press and hands the loop
the ones it was not awake for. Such a press may never start a session: a lost
start just means pressing the button again, whereas a *false* start lifts a leg
with nobody at the panel, and the interrupt is not IRAM-registered on this core,
so a glitch delivered late out of a flash-cache-off window can be mis-timed as a
full press. A spurious stop costs one safe-lower; that is the trade.

With START and STOP on separate pins this was settled by wiring the interrupt
path to the STOP pin only. One RUN button cannot be split that way — the same
press means either thing depending on the phase, and the phase can still change
between the poll and the sketch acting on the edge, because `tick()` runs in
between. So the latch fires for RUN and the edge carries its origin
(`edgeFromIsr`); the sketch drops a latched press that would have started a
session and prints `start ignored: latched RUN press, not a polled one`. The
guarantee is unchanged, just enforced one step later.

**7. Idle can lower too, when the leg is believed up.** Idle used to be the only
state with no DOWN command at all, and it is reachable with the leg in the air —
a second RESET after a failed recovery lands there, and so does a power cut
part-way through a lift. RUN in Idle now commands one capped descent, but only
while the firmware believes the leg has been driven up since the bottom was last
genuinely confirmed. That same belief is what decides whether a RUN press starts
at all (`SessionSM::toggleWouldStart`), so the two readings can never disagree:
if the leg is believed up, the press lowers; if it is believed down, the press
starts.

That belief is seeded from the bottom limit switch at boot, which is what makes
the power-cut case work: a board that comes up with the leg in the air knows it.
The descent ends under the same rules as any other — Idle on a confirmed
arrival, Fault otherwise — so a lower that achieves nothing is visible and is
not repeatable without the operator acknowledging it. An earlier version let
this path always return to Idle on the reasoning that Idle has no fault to go
back to; that made it unconfirmable, unlatched and endlessly repeatable, and
every press drove the full cap into the mechanical bottom stop in silence.

Two things that are deliberately **not** nets. The UART-loss safe-stop is armed
by the first line ever received, so a controller running standalone is not
stopped by a display that was never there. And a start is refused while the RUN
line reads pressed with nobody plausibly pressing it — an open NC circuit — on
the grounds that a session nobody can stop from the panel should not begin.

### What the firmware cannot fix

A DOWN relay that has failed open, or a jammed actuator, leaves no way down by
any command. The firmware behaves correctly around both — strokes stay bounded,
the machine stays latched in Fault so RUN keeps buying descents — but the leg
comes down by hand or not at all. Treat a fault that survives two descents as a
mechanical fault, not a firmware one.

A broken RUN line is now a bigger single-point failure than a broken STOP line
was, and that is the price of the two-button panel. Under `BTN_RUN_NC` an open
circuit reads pressed forever: it raises exactly one edge on the way open — a
stop, which is the safe direction — and then the panel has neither a start nor a
stop until the wire is fixed. The server can still stop the machine, and the
travel cap and the UART-loss safe-stop are both unaffected. Wiring RUN as
normally-open would trade this for a line that goes silently dead instead of
firing one safe stop, which is worse; `BTN_RUN_NC` stays at `1`.

One combination is still a dead end by design: leg up in Idle **and** the RUN
line open at power-up. Nothing can be commanded from the panel — the wire-suspect
refusal also blocks a start from the server — and it needs two coincident faults.
The alternative, letting a machine with no working stop button start a session,
is worse.

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
- Panel buttons: RUN (start/stop toggle) GPIO 25, RESET GPIO 4. Momentary,
  INPUT_PULLUP, other side to GND; contact type per button via `BTN_RUN_NC` /
  `BTN_RESET_NC`. GPIO 26 carried a separate STOP button until the panel went to
  two buttons and is now free.
- Status LED: GPIO 23, a 12 V lamp through a relay. Lit while a session is in
  progress, dark otherwise. Set `LED_ACTIVE_LOW` if the relay board energizes on
  LOW.
- UART: controller GPIO 16 (RX) / 17 (TX) ↔ CYD GPIO 22 (RX) / 27 (TX),
  cross-wired, common GND. 115200 8N1. Pins to be confirmed on the bench.
