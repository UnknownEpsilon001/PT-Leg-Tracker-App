# PT Leg Device Firmware

Two ESP32s per machine:

| Sketch | Board | Job |
|---|---|---|
| `pt-leg-controller/` | plain ESP32 dev board (`esp32:esp32:esp32`) | relays, timed motion state machine, panel buttons + lamps, Wi-Fi + server client, NVS settings |
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
`https://espressif.github.io/arduino-esp32/package_esp32_index.json`). Both
sketches are built and flashed against **3.3.11**; 3.3.10 also worked, and the
core is not pinned because nothing here depends on a version-specific API.

The library versions above are a different matter — they are pinned for a
reason. The Arduino IDE offers to update libraries on startup, and accepting
that is what silently moves lvgl to 9.x. Before a build after any long gap:

```powershell
arduino-cli lib list
```

Expect 8.4.0 / 2.5.43 / 7.4.x. Anything else, reinstall the pin, re-copy the two
config files below, and rebuild with `--clean` — a stale cache otherwise links
objects from both versions.
[TH] เวอร์ชันของไลบรารีด้านบนถูกล็อกไว้ด้วยเหตุผล ถ้ากดยอมรับการอัปเดตที่ Arduino IDE เสนอตอนเปิดโปรแกรม
     lvgl จะถูกอัปเป็น 9.x แล้วคอมไพล์ไม่ผ่าน ก่อนบิลด์ให้ตรวจเวอร์ชันก่อนเสมอ

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
  "liftSec": 4,
  "lowerSec": 4,
  "holdSec": 10,
  "restSec": 10,
  "targetReps": 10
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
| Out-of-range value in the file | Clamped to the settings screen's own limits (`maxTravelSec` 1-20, `liftSec`/`lowerSec` 1-`maxTravelSec`, `holdSec`/`restSec` 1-60, `targetReps` 0-99) and logged, because `maxTravelSec` is the relay safety cap and `liftSec`/`lowerSec` are what actually time the travel |

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

`link.h`, `link.cpp` and `settings.h` exist in **both** sketch directories
because an Arduino sketch cannot include sources from outside its own folder,
and `sync-shared.ps1` keeps the display's copies identical to the
controller's, which are the source of truth.

```powershell
.\firmware\sync-shared.ps1          # copy controller -> display
.\firmware\sync-shared.ps1 -Check   # report drift only, exit 1 if any
```

Run the `-Check` form before flashing a release. Undetected drift means the UART
`settings` message silently loses a field and the boards disagree about the
machine's configuration.

`session.h` also exists in both directories but is **not** synced by the
script, and deliberately so: the display's copy is a reduced one — just the
`Phase` enum, because that board receives a phase number over the link and
never runs the state machine, so the two files are meant to differ line for
line. The one thing that still must match between them is the enumerator
**order**, since the wire's `"phase":int` is that order. There is no automated
check for this — it relies on both copies documenting it in a comment, and both
now do. `sync-shared.ps1` reads the two files and prints a note if either
comment goes missing, which is as close to a gate as this coupling gets.

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
phases visibly move — shorten it with `-LiftSec`/`-LowerSec`/`-HoldSec`/`-RestSec`. Stop it and the
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
edges, raw pin levels, phase and rep changes, all printed on change only:

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

**Both panel buttons are normally-open** (`BTN_START_NC` and `BTN_STOP_NC` are
both `0` in `config.h`), so at rest each reads HIGH through its internal pullup
and a press pulls the pin to GND. A bare board with nothing wired therefore boots
reading both buttons "at rest" — the opposite of an NC panel, where a floating
pin misreads as held down. For a bench test there is nothing to jumper at rest:
just touch each button pin to GND to stand in for a press.

| Signal | Pin | At rest | Press |
| ------ | --- | ------- | ----- |
| START  | 25  | leave open (unwired) | touch to GND |
| STOP   | 26  | leave open (unwired) | touch to GND |
| GREEN lamp  | 23 | — output, see below | — |
| YELLOW lamp | 32 | — output, see below | — |

START does nothing while a session is already running. From Idle it begins a
normal session. From a latched FAULT it lowers the leg for `lowerSec` first and
then lifts straight into the new session — the debug build prints `EDGE START`
on the press, `phase=SAFE-LOWER` while it recovers, then `phase=LIFT` once the
new session actually begins.

STOP always ends whatever is in progress: from LIFT/HOLD/LOWER it lowers the leg
for `lowerSec` and then stops; from REST the leg is already down, so it stops on
the spot; from a latched FAULT it lowers and clears the latch once the descent
finishes; from IDLE it zeroes the rep count instead — the job the retired RESET
button used to do. The debug build prints `EDGE STOP` followed by the phase(s)
that follow.

Both lamps are the out-of-band readout for the whole bench run: green lit while a
session is in progress (including a recovery lowering out of FAULT), yellow lit
otherwise, and both lit for a latched FAULT. Neither blinks, so the only thing to
watch is that exactly one is lit outside a fault, and that they swap at the phase
boundaries and nowhere else. Details in
[The panel lamps](#the-panel-lamps) below.

`firmware/tools/button-bench.ps1` walks that test as a pass/fail checklist: it
opens the port, reboots the board, tells you which pin to touch next, and fails a
step if the expected lines do not arrive within the timeout.

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

With no limit switches left, there is nothing to fake or ground on the bench:
every stroke runs its full `liftSec`/`lowerSec`. A starting START press gives
LIFT for `liftSec` then HOLD for `holdSec`, and so on around the cycle; a STOP
mid-session gives SAFE-LOWER for `lowerSec` then IDLE. That safe-lower always
ends cleanly (`fault=false`) now — there is no arrival to confirm, so nothing
about a bench stop is faulted by design. The only way to see `fault=true` on the
bench is a LIFT or LOWER that is still energized when `maxTravelSec` runs out,
which the default timings never do on their own.

**A start is refused while START reads pressed since power-up, or has read
pressed for longer than anyone plausibly holds a button.** `buttons.cpp` treats
either as evidence of a short rather than a real press — `STUCK_PRESSED_MS` is
30 s — since an open wire on an NO button can never read as pressed at all, only
a short can. Touching START's pin to GND and releasing it once clears the
suspicion; until then a starting press prints
`start refused: START reads pressed, check its wiring` and does nothing. A
*stopping* press is never refused, by this gate or any other.

## The panel lamps

Two 12 V lamps, each switched by its own mechanical relay: green on **GPIO 23**,
yellow on **GPIO 32**. Exactly one is lit in each of the two normal states, and
both are lit for a fault:

| Lamps | Means |
| ----- | ----- |
| Green only  | a session is in progress and the actuator may move — keep clear |
| Yellow only | idle; nothing will move on its own |
| **Both lit** | a latched FAULT — the relays are off, but the leg may be raised |

Green is lit on `SessionSM::running()` — LIFT, HOLD, LOWER, REST **and** the
safe-lower — plus a latched FAULT; yellow is lit whenever the machine is not
running, plus that same FAULT. So green still answers "may it move" on its own,
and the both-lit combination is unmistakably abnormal without anything blinking.

A fault used to read plain yellow, identical to an ordinary idle. That is fine
when a CYD is attached, and wrong without one: on a machine running standalone
the panel is the only indicator there is, and an operator reading "ready" on a
machine that faulted with the leg raised is the one impression this panel must
not give. It costs two extra contact operations per fault, not a blink, so the
relay-life argument for never blinking is untouched. What a fault *is* is still
diagnosed on the CYD, the serial log and the card row.

**Why there is no blinking.** The relay makes every state change a contact
operation and an audible click. A 5 Hz fault blink is 18,000 operations an hour,
which retires a ~100k-operation relay in about six hours of one latched fault —
and an idle heartbeat would grind it down while the machine sat doing nothing at
all. Solid states cost four operations per session across the pair (both lamps,
each switching on and off once): at twenty sessions a day that is still roughly
three and a half years. An earlier version of this file specified 2.5 Hz / 5 Hz /
heartbeat patterns; those were written for a 3.3 V LED driven straight off the pin
and do not survive contact with a relay.

If the lamps are ever moved onto MOSFETs or SSRs the constraint disappears and
patterns become free again — but the semantics above are the ones the panel is
built and documented around.

`panelLampsBegin()` writes the level **before** `pinMode(OUTPUT)` for both
lamps, which is the opposite order to `actuatorBegin()`. A pin leaves reset as a
high-Z input, and `pinMode(OUTPUT)` makes it drive whatever the output register
holds — LOW. On an active-low relay board LOW is *energized*, so mode-then-write
would flick a lamp on at every boot. The motor relays get away with the other
order only because they are active-high.

There is deliberately **no power-on lamp test**. Idle is the boot state, so
yellow comes on and stays on — that is not a lamp test and must not become one:
the panel is specified to show exactly one lamp at all times, and a green flash
at power-up would read as the machine starting by itself, the one impression it
must never give. Test the lamps by starting a session.

Set `LED_ACTIVE_LOW` in `config.h` if your relay board energizes on LOW, as most
cheap opto-isolated modules do. It covers **both** lamps, on the assumption they
are two channels of one board. **Check this before first power-up**: backwards,
green is lit whenever the machine is idle and yellow while it is working, which
is the dangerous way round.

**Why 23 and 32.** GPIO 23 was already the lamp pin under the single-LED design,
kept for green. GPIO 32 is the pin the retired TOP limit switch freed — it is not
a strapping pin and does not glitch at boot the way 0/2/5/12/15 can, and nothing
else reads it or reserves it.

## The safety nets, and what each one actually covers

The limit switches are gone, and with them went the nets that depended on
knowing where the leg was — a wiring-fault check on two switches closed at once,
a bottom-switch-only-if-it-was-clear-at-the-start belief, and an Idle-can-lower
path keyed off that belief. What is left is smaller and does not pretend to know
the leg's position: it bounds how long a relay can be energized, and it makes
sure every stop and every recovery drives the same bounded descent whether or
not that descent actually gets the leg to the bottom.

**1. Break-before-make, plus a 50 ms dead time.** `actuator.cpp` is the only
file that touches the relay pins. It always de-energizes both before energizing
one, and on a direction reversal it drops both, waits `DEADTIME_MS`, and
energizes the new direction from `actuatorTick()`. The wait matters because
commanding the relays apart is not the same as their contacts being apart: a
STOP press during a lift goes from UP to SafeLower's DOWN in a single loop
iteration, and without the wait that reversal relied on the UP relay releasing
faster than the DOWN one operates — a property of the parts, not of this code,
and the two are on different connectors.

**2. The travel cap, enforced twice.** `actuatorTick()` checks it every loop,
and an `esp_timer` one-shot armed on every energize cuts the relays from the
timer task if the loop does not get there. The loop-side check alone was not a
second net: the failures that make a stroke run long — a network call blocking
for its timeout, a long flash write — are the same failures that stop the check
running. The timer covers a blocked loop, and it runs on the other core so it
survives one. It does **not** cover a panic (neither core runs) and it is
delayed, not defeated, by a flash erase, which stalls both CPUs. If it cannot be
created at boot, `setup()` prints `cap timer missing, the hardware backstop is
OFF` — that message means the machine is down to net-1 only. This is now the
*only* way a FAULT is entered: `forceFault()` is called only when the cap trips
outside a safe-lower, so a FAULT always means a LIFT or a LOWER ran too long,
never a travel timeout against a switch that no longer exists.

**3. A fault does not trap the leg.** Both START and STOP drive a lowering out
of a latched Fault — SafeLower for `lowerSec`, the same bounded stroke as any
other descent. STOP clears the latch once that lowering finishes and the machine
lands in Idle; START queues a fresh session behind it, so the descent runs first
and the new LIFT begins right after. Neither is refused by a Fault, and neither
drives the actuator further in the direction that may have caused it — descents
only ever command DOWN. The lowering reports nothing of its own: the session was
already recorded when it faulted, and a second report would put a duplicate row
on the card.

There is no confirmation that the descent actually reached the bottom — with the
limit switches gone the firmware has no way to check, which is stated plainly in
`config.h`: a slipping belt, a jammed actuator and a clean stroke all look
identical to this firmware, "the relay was energized for `lowerSec`". A recovery
that does not really clear whatever jammed the actuator is indistinguishable, to
the firmware, from one that does; both simply run `lowerSec` and land in Idle.
That is why the machine must not run unattended with a leg loaded on it — the
actuator's energize cap is the only backstop left.

**4. Only a stop can be recovered from the interrupt.** Buttons are
level-sampled once per loop, and one iteration can block for most of a second
inside a network call against an unreachable server — long enough for a normal
press to begin and end unseen. A pin-change interrupt therefore times each press
and hands the loop the ones it was not awake for. Such a press may never start a
session: a lost start just means pressing the button again, whereas a *false*
start lifts a leg with nobody at the panel, and the interrupt is not
IRAM-registered on this core, so a glitch delivered late out of a
flash-cache-off window can be mis-timed as a full press. A spurious stop costs
one safe-lower; that is the trade, and with START and STOP on separate pins it
is settled entirely by wiring the interrupt latch to STOP alone (`isrMayEdge` in
`buttons.cpp` is set only on STOP's struct) — a START press is never latched, so
there is no press-origin bookkeeping to drop the way a single toggle button
would have needed.

**5. A stuck START line cannot restart the machine on its own.** Both buttons
are normally-open, so a broken wire simply goes dead — it can never raise an
edge in either direction, and it can never be misread as held down either. What
is left to guard against is a short: a pinched loom, a jammed button, a
screwdriver left across the terminals reads exactly like a held press. On STOP
that is cheap, because a repeated stop is a no-op. On START it is not: read
literally, a shorted START line would restart the machine the instant every
session ended, with nobody at the panel. `buttonStartStuck()` in `buttons.cpp`
catches this two ways — a START line reading pressed since power-up, or one that
has read pressed longer than `STUCK_PRESSED_MS` (30 s) — and refuses a start
while it holds, including one commanded from the server. It clears the moment
the line moves, which a genuinely held button does on release; a short never
does.

Two things that are deliberately **not** nets. The UART-loss safe-stop is armed
by the first line ever received, so a controller running standalone is not
stopped by a display that was never there. And a start is refused while the
START line reads pressed with nobody plausibly pressing it — a suspected short —
on the grounds that a session should not begin under conditions the firmware
cannot tell apart from a stuck button; STOP is on its own pin and keeps working
regardless, so this refusal never costs the machine its only way to stop.

### What the firmware cannot fix

A DOWN relay that has failed open, or a jammed actuator, leaves no way down by
any command: every descent is a bounded `lowerSec` stroke and nothing here can
tell whether the leg actually moved. Strokes stay bounded and the machine can
always be told to try again — START or STOP from a latched Fault both drive
another descent, identically, because there is no confirmed-arrival state to
distinguish a first attempt from a repeat. The leg comes down by hand or not at
all if the mechanism genuinely cannot move it; treat a fault that survives
another STOP as a mechanical fault, not a firmware one.

Both buttons being normally-open changes what a broken wire costs, compared to
the old NC panel. An NC line that breaks reads pressed forever and fires one
edge on the way open — a stop, the safe direction — and then the panel has
neither a start nor a stop until it is fixed. An NO line that breaks just goes
dead: it can never raise an edge again, but it also never fires that one safe
stop on the way out, and it does not block the *other* button, because START and
STOP are on separate pins now. A broken START goes quietly dead and only costs
starting; a broken STOP goes quietly dead too, and the server's `stop` command
is what is left to end a session from a broken STOP line. The travel cap and the
UART-loss safe-stop are both unaffected by either.

## LVGL logging and asserts

`lv_conf.h` enables `LV_USE_LOG` at WARN with `LV_LOG_PRINTF`, and sets
`LV_ASSERT_HANDLER` to `esp_restart()`. The stock handler is `while(1)`, which
turns an LVGL out-of-memory into a frozen screen with a dead touchscreen, no
reboot and nothing on serial — a failure mode that cost real debugging time on
2026-07-21. A reboot with a printed reason is far easier to diagnose, and on a
patient-facing machine it recovers instead of appearing bricked.

## Wiring

- Controller relays: UP GPIO 22, DOWN GPIO 27. There are no limit switches —
  travel is timed by `liftSec`/`lowerSec`, both editable and capped by
  `maxTravelSec`.
- Panel buttons: START GPIO 25, STOP GPIO 26. Momentary, INPUT_PULLUP, other
  side to GND; both normally-open (`BTN_START_NC` / `BTN_STOP_NC`, both `0` in
  `config.h`). GPIO 4 and GPIO 33 are free — nothing reads them and nothing
  reserves them.
- Panel lamps: green GPIO 23, yellow GPIO 32, each a 12 V lamp through its own
  relay. Green lit while a session is in progress, yellow while idle, **both**
  lit for a latched FAULT — one lamp in each normal state, never blinking. Set
  `LED_ACTIVE_LOW` if the relay board energizes on LOW; it covers both lamps.
- UART: controller GPIO 16 (RX) / 17 (TX) ↔ CYD GPIO 22 (RX) / 27 (TX),
  cross-wired, common GND. 115200 8N1. Pins to be confirmed on the bench.
