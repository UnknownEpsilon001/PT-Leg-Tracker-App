# Smart OA Knee — Device Firmware

Two ESP32s per machine:

| Sketch | Board | Job |
|---|---|---|
| `pt-leg-controller/` | plain ESP32 dev board (`esp32:esp32:esp32`) | BTS7960 motor drive, timed motion state machine, panel buttons + lamps, Wi-Fi + server client, NVS settings |
| `pt-leg-display/` | ESP32-2432S028R "Cheap Yellow Display" (`esp32:esp32:esp32`) | LVGL touch UI only |

They talk over UART (19200 8N1, cross-wired RX↔TX + common GND, each line
checksummed — **flash both boards together when the protocol changes**).
Protocol:
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

## Running without the app or the internet

The machine is meant to work on its own: the panel buttons and the CYD run a
session, and every finished session is a row in `/sessions.csv` on the CYD's
card. Wi-Fi is only for setting the clock.

**The clock.** A session row carries the time it ended. The controller takes it
from, in order:

1. the DS3231 chip, when one is fitted (SDA 21, SCL 19, 3.3 V) and set. It has a
   battery, so the time survives a power cut;
2. the system clock, once SNTP has answered over Wi-Fi or someone has set it on
   the CYD under ตั้งค่า → **วันและเวลา**. Without the chip this is lost at every
   power cut, so a machine with neither chip nor internet has to be set once per
   power-on.

With no time at all the row says `?`, never a made-up date. The CYD's main
screen shows the current time top left, or **ไม่ทราบเวลา** when there is none.

**Wi-Fi only for the clock.** With no server address set, the controller turns
its radio off as soon as SNTP has answered, so an idle machine makes no network
traffic at all. If a server address is set but nothing answers it, every HTTP
call stops for 30 s after a failed connect (`NET_QUIET_MS` in `net.cpp`), so a
missing server cannot stall the safety loop with a 400 ms timeout every second.

**Reading the data.**

- On a PC: take the card out and double-click `sessions.csv`. The file starts
  with a UTF-8 byte order mark, so Excel shows Thai names correctly, and the
  date is written `2026-11-05 14:32:10` so Excel sorts and filters it as a date.
  A card made before this change keeps its old header with no mark; rename or
  delete that `sessions.csv` once and the next session starts a fresh one.
- On the CYD: the list button (top right), then **ประวัติ**, shows the newest 20
  sessions with time, patient, reps and duration. A faulted session is red and
  marked ขัดข้อง.

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
  "liftSec": 7,
  "lowerSec": 7,
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
| Out-of-range value in the file | Clamped to the settings screen's own limits (`maxTravelSec` 1-20, `liftSec`/`lowerSec` 1-`maxTravelSec`, `holdSec`/`restSec` 1-60, `targetReps` 0-99) and logged, because `maxTravelSec` is the motor safety cap and `liftSec`/`lowerSec` are what actually time the travel |

The Wi-Fi password is plaintext on removable media. That is inherent to editing
it on a PC, and accepted for a clinic LAN.
[TH] ค่าตั้งค่าเก็บสองที่: ไฟล์ JSON บนการ์ด SD ของจอ และ NVS ของคอนโทรลเลอร์
     โดยยึดค่าจากการ์ดเป็นหลัก แก้รหัสผ่าน WiFi จากคอมพิวเตอร์ได้ และเปลี่ยนบอร์ดคอนโทรลเลอร์
     ได้โดยไม่ต้องพิมพ์ใหม่ รหัสผ่านเก็บเป็นข้อความธรรมดาบนการ์ด ยอมรับได้สำหรับเครือข่ายในคลินิก

## Spoken cues from the card (`/sounds`)

The display board plays a spoken Thai prompt on four events, through the CYD's
speaker output on GPIO 26. The switch labelled **เสียง** on the settings screen
turns them off; the state is the `soundOn` key in `/settings.json`.

| File | Spoken | Fires when |
|---|---|---|
| `/sounds/boot.wav` | เครื่องพร้อมใช้งาน | End of `setup()`, so hearing it means the whole init path finished |
| `/sounds/start.wav` | เริ่มการทำงาน | The controller's one-shot `started` message, when a session's lift begins |
| `/sounds/stop.wav` | หยุดการทำงาน | A `done` message with the target unmet, or no target set |
| `/sounds/target.wav` | ครบตามเป้าหมายแล้ว | A `done` message with the rep target reached |

Stop and target are taken from the one-shot `done` message rather than from the
`running` flag falling, because this board cannot tell a finished session from a
dropped link — and a machine that says หยุดการทำงาน every time a jumper glitches
is worse than one that says nothing. Start has no such message, so it edge-detects
with two guards: the link must be up, and the first state frame after boot only
seeds the previous value. Without the second, a display restarted beside a running
machine announces a start that already happened.

**The one accepted format**, and nothing else: RIFF WAV, uncompressed PCM,
8-bit unsigned, mono, 22050 Hz, at most 49152 bytes of samples (2.23 s). The rate
is not a preference — `dac_continuous_config_t::freq_hz` has a 19.6 kHz floor on
the ESP32 with the default clock source. A file that fails any of these is
rejected with a serial line naming the file and the field, because a silent
speaker and a rejected file have to be distinguishable without an oscilloscope.

Produce files with the tool rather than by hand:

```bash
# one file, or a whole folder at once
powershell -File firmware/tools/make-sounds.ps1 -In .\raw\start.m4a -Out .\sd\sounds\start.wav
powershell -File firmware/tools/make-sounds.ps1 -In .\raw -Out .\sd\sounds
```

The four shipped cues in `firmware/sd/sounds/` come from `make-cues.py`: a short
UI effect from `assets/soundeffectthatifound/UI Sound/`, then the phrase spoken
by the `th-TH-PremwadeeNeural` voice. To put them on the card, plug the card
into the PC and double-click `load-sounds.cmd` at the repo root
(`firmware/tools/load-sounds.ps1`): it finds the card, refuses anything that is
not FAT32, copies the folder to `/sounds` and checks each copy by hash.
To rebuild them (needs ffmpeg, `pip install edge-tts` and internet; the phrases
go to Microsoft's online TTS):

```bash
python firmware/tools/make-cues.py                          # female voice
python firmware/tools/make-cues.py --voice th-TH-NiwatNeural  # male voice
```

It passes its mix through `make-sounds.ps1`, so the checks below still apply.

`make-sounds.ps1` normalises, converts, strips metadata chunks, pads 20 ms of silence at both
ends, and refuses to write a file over the buffer size. **Loudness is decided
there or not at all** — there is no gain stage anywhere in the firmware, so a
quiet recording is a quiet machine and the fix is to re-run the tool, not to
change a constant.

The padding matters more than it looks. The DAC holds the last code written to
it, so a clip ending on a loud sample leaves the output there; disabling the
channel then steps it audibly. Both ends sit at `0x80`, which is silence for
unsigned 8-bit and the converter's rest value.

Clips are loaded whole into RAM before the first sample is emitted, 4 KB per
`loop()`, so the card is never read while audio is playing. That is deliberate:
the stop cue and the `sessions.csv` append are triggered by the same message, and
streaming would have put an SD read in the middle of the one moment of a session
anyone is watching.

[TH] บอร์ดจอเล่นเสียงพูดภาษาไทยสี่รายการจากการ์ด ผ่านลำโพงที่ขา 26
     สวิตช์ "เสียง" บนหน้าตั้งค่าใช้ปิดเสียง โดยเก็บสถานะเป็นคีย์ soundOn ใน /settings.json
     เสียงหยุดและเสียงครบเป้าหมายมาจากข้อความ done ที่ส่งครั้งเดียวต่อรอบ ไม่ใช่จากสถานะกำลังทำงานที่ตกลง
     เพราะบอร์ดนี้แยกไม่ออกว่ารอบจบแล้วหรือลิงก์ขาด
     รูปแบบไฟล์ที่ยอมรับมีแบบเดียว คือ RIFF WAV แบบ PCM 8 บิตไม่มีเครื่องหมาย โมโน 22050 เฮิรตซ์
     ข้อมูลเสียงไม่เกิน 49152 ไบต์ (2.23 วินาที) ให้สร้างไฟล์ด้วย tools/make-sounds.ps1 ไม่ใช่ทำเอง
     ความดังถูกกำหนดที่เครื่องมือนั้นที่เดียว เพราะเฟิร์มแวร์ไม่มีการปรับความดังเลย
     คลิปถูกโหลดเข้าหน่วยความจำจนครบก่อนเล่น การ์ดจึงไม่ถูกอ่านระหว่างมีเสียง

## Host tests (`tests/run-tests.ps1`)

```bash
# from the repo root
powershell -File firmware/tests/run-tests.ps1
```

Compiles `pt-leg-display/wav.cpp` for the PC with `g++` and runs the assertions in
`tests/wav_test.cpp`. `-Werror` is on.

This covers the WAV header parser and nothing else, which is the honest boundary:
that file reads bytes and knows nothing about the DAC, the card or LVGL, so it is
the only part of the audio feature that can be checked without a board. **Do not
add stubs to widen it** — the result would test the stubs. Everything else is
covered by the bench checklist below.

If `firmware/.build/sounds/start.wav` exists, the run also feeds a real
`make-sounds.ps1` output through the same parser the firmware uses, so a change to
the tool that breaks the format fails here instead of on the bench. Without it the
run prints a `SKIP:` line and is one check shorter.

[TH] คอมไพล์ wav.cpp บนคอมพิวเตอร์ด้วย g++ แล้วรันชุดทดสอบใน tests/wav_test.cpp
     ครอบคลุมเฉพาะตัวแยกส่วนหัวไฟล์ WAV เท่านั้น ซึ่งเป็นขอบเขตตามความจริง
     เพราะไฟล์นั้นอ่านแต่ไบต์ ไม่รู้จัก DAC การ์ด หรือ LVGL
     ห้ามเพิ่มตัวจำลองเพื่อขยายขอบเขต เพราะจะกลายเป็นการทดสอบตัวจำลองเอง
     ส่วนที่เหลือใช้รายการตรวจบนโต๊ะแทน

## Audio bench checklist

`UI_TEST_HOOK` cannot hear anything, so every row here needs ears on the bench.

**Do this one first, with no speaker connected.** Some CYD revisions leave the
amp pads empty and wire the speaker JST straight to GPIO 26. Run `dacWrite(26,
128)` and measure: the pin reads about 1.65 V on every board, but the JST should
read near 0 V DC if an amp is present. If the JST reads that same 1.65 V there is
no amp — do not connect the 8 Ω speaker, fit an external PAM8403 or MAX98357A
between pin and speaker instead. The firmware is identical either way.

| Check | Passes when |
|---|---|
| Touch after `audioBegin()` | Every settings-screen control still responds. Proves the CH1-only channel mask left GPIO 25 alone. |
| Boot cue | Plays once on a real power cycle, after the UI is up. |
| Start cue | Fires on the panel START button, once. |
| Stop cue | Fires on a STOP before the target, once; the target clip does not. |
| Target cue | With a target of 2, the target clip plays and the stop clip does not. |
| Target 0 | Target 0, run, press STOP: stop clip, never target. |
| Start after a stop | Run, STOP, wait for the leg to come down, START again: the start cue plays every time, including the first START after a boot or a jog (which lowers first; the cue comes when the lift begins). |
| Jog exit | Leave jog by holding both buttons: the leg lowers, and no start cue plays. |
| Link drop | Pull a UART jumper mid-session and reseat it. **No start cue.** |
| Display reboot | Reset the display while the controller runs. **No start cue.** |
| Cue against the SD write | Stop cue and the `sessions.csv` append fire together: audio must not stutter **and** the row must still be written. |
| Back-to-back cues | Start then stop in quick succession: the second interrupts the first cleanly, no noise, no freeze. |
| Heap | `ESP.getFreeHeap()` after 20 cues equals the value after the first. |
| Bad file | A 16-bit WAV in place of a good one prints `rejected — not 8-bit` and makes no noise. |
| No card | Every cue is a no-op; the machine runs normally. |
| Sound switch | Off silences all cues and survives a power cycle; check `"soundOn": false` in the file on a PC. |
| Five settings opens | Open and close the settings screen five times with no freeze. The switch row must be built once, in `buildSettings()`. |
| Pop | No click at the start or end of any clip. |

[TH] โหมด UI_TEST_HOOK ไม่ได้ยินเสียง ทุกข้อในรายการนี้จึงต้องใช้คนฟังที่หน้าเครื่อง
     ให้ตรวจว่ามีวงจรขยายเสียงหรือไม่ก่อนเป็นข้อแรก โดยยังไม่ต้องต่อลำโพง

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

`flash.ps1` wraps the above and refuses the two mistakes that produce a board
which looks broken for reasons its own output never explains — a missing
`PartitionScheme=huge_app`, and a bench switch (`LINK_OVER_USB`, `UI_TEST_HOOK`,
`BENCH_DEBUG`, `LOOP_DEBUG`) left at 1. It also runs `sync-shared.ps1 -Check`
first, so a release cannot go out with the two copies of `link.*` drifted.

```powershell
.\firmware\tools\flash.ps1                    # interactive menu
.\firmware\tools\flash.ps1 -Action ports      # identify controller/display COM ports
.\firmware\tools\flash.ps1 -Action compile -Board both
.\firmware\tools\flash.ps1 -Action upload -Board display
.\firmware\tools\flash.ps1 -Action auto -Board both # compile changed code, detect ports, upload
.\firmware\tools\flash.ps1 -Action auto -Board both -DryRun -Port COM4 -DisplayPort COM5
.\firmware\tools\flash.ps1 -Action auto -Board controller -Erase # erase, build if needed, upload
.\firmware\tools\flash.ps1 -Check             # guards only, flash nothing
.\firmware\tools\flash.ps1 -Action auto -Board both -Port COM4 -DisplayPort COM5
```

Full-auto mode stores each build and its source hash under ignored
`firmware/.build/`. If that board's code and required build configuration have
not changed and every required build artifact still matches its saved SHA-256,
it skips compilation and uploads the current binary. It identifies this
machine's controller by its CP210x USB bridge and the CYD display by its
CH340/CH9102 bridge. Ports are checked before compiling and checked again before
uploading so an unplug or COM-port move stops the run. Explicit `-Port` and
`-DisplayPort` values override automatic selection, but a port whose known role
is the opposite board is rejected. Use `-AllowPortRoleMismatch` only when a
replacement board intentionally uses a different USB bridge. Missing,
duplicate, and ambiguous matches stop rather than guessing. `-SkipGuards`
permits a bench build on purpose, `-DryRun` performs no erase or upload, and
`-NoCompile` remains an alias for `-Action upload`.

When run without arguments, the interactive menu stays open until **Exit** is
selected. Board selection also uses numbers: `1` controller, `2` display/CYD,
or `3` both. A missing/unplugged board, invalid choice, or failed action prints a
short `Not completed` message and returns to the menu, so the board can be
connected and the action retried. Explicit command-line actions remain suitable
for automation: they print one concise error and return a non-zero exit code
instead of looping forever.

Compile, erase, and upload operations show a PowerShell progress bar. Compile
percentage is explicitly approximate because Arduino CLI does not report a true
percentage; upload uses esptool's real write percentage when it is available.
Compiles run in a separate staging directory. Pressing **Ctrl+C** stops the
compiler process tree, deletes the incomplete staging build, keeps the previous
verified build cache byte-for-byte, and returns to the interactive menu. Source
files are never modified by cancellation. Interrupting an erase or upload cannot
be made electrically atomic: the script stops cleanly, but the board may contain
incomplete firmware and must be erased/uploaded again before use.

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

START does nothing while a session is already running. From Idle or a latched
FAULT, START opens a confirmation instead of starting outright — but only when
a display is linked to show it on. The debug build prints `EDGE START` on the
press and `phase=CONFIRM`; the CYD names the phase รอยืนยัน and shows the rep
count about to run, still editable there. A second START press commits it, and
ตกลง on the touchscreen does the same — both follow from there exactly as a
direct start always did, `phase=LIFT` from Idle, or `phase=SAFE-LOWER` while it
recovers then `phase=LIFT` if the confirmation was opened from a FAULT. STOP
cancels a pending confirmation back to Idle, and so does 30 s of nobody
touching anything; either way nothing has run, because nothing energizes a
relay while the confirmation is open. With the link down there is no screen to
put a confirmation on, so START falls back to starting on the first press, the
way it always has — a display that has dropped off must never be the reason
the machine cannot be started.

Whatever rep count is showing when a confirmation is committed becomes that one
session's target and nothing more: it is never written back to `cfg.targetReps`,
the stored prescription that the settings screen owns, so the next confirmation
still starts from the settings screen's own number unless someone changes it
again right there. The app's own start flow asks the same question before it
queues a start over the server, for the same reason, and a target sent along
with that start is likewise good for one session only.

STOP always ends whatever is in progress: from LIFT/HOLD/LOWER it lowers the leg
for `lowerSec` and then stops; from REST the leg is already down, so it stops on
the spot; from a latched FAULT it lowers and clears the latch once the descent
finishes; from IDLE it zeroes the rep count instead — the job the retired RESET
button used to do. The debug build prints `EDGE STOP` followed by the phase(s)
that follow.

The lamps are the out-of-band readout for the whole bench run: green lit while a
session is in progress (including a recovery lowering out of FAULT), yellow lit
otherwise, and blinking for jog mode and a latched FAULT. Outside those two,
watch that they swap at the phase boundaries and nowhere else. Details in
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

Two 12 V lamps on **one changeover relay**, driven from **GPIO 23**: green on the
relay's NO contact, yellow on NC. The bench machine's second relay channel (the
old yellow on GPIO 32) died, so both lamps moved onto one channel. The contact
lights exactly one lamp at any moment:

| Lamps | Means |
| ----- | ----- |
| Green  | a session is in progress and the actuator may move — keep clear |
| Yellow | idle; nothing will move on its own |
| **Blinking** green/yellow, 1 s | jog mode, or a latched FAULT — motor cut, leg may be raised |

Green is lit on `SessionSM::running()` — LIFT, HOLD, LOWER, REST **and** the
safe-lower, including the lowering that ends jog mode. Jog and a latched FAULT
blink, so neither reads as an ordinary idle. On a machine running without a CYD
the panel is the only indicator there is, and an operator reading "ready" on a
machine that faulted with the leg raised is the one impression this panel must
not give. What a fault *is* is still diagnosed on the CYD, the serial log and
the card row. With two relays a fault lit both lamps instead; one changeover
contact cannot do that.

**Keep green on NO.** A relay that is off — controller unpowered, resetting,
browned out — then shows yellow. Green on NC would light "may move" on a dead
controller.

**What blinking costs.** Every toggle is a contact operation and a click. At
`LAMP_BLINK_HALF_MS` 500 that is 7,200 operations an hour. Jog is short, so it is
nothing there. A fault blinks until STOP clears it, so a fault left latched
overnight is about 50k operations, half the life of a ~100k-operation relay.
That trade was chosen deliberately; solid green and yellow still cost two
operations per session. If the lamps ever move onto MOSFETs or SSRs the cost
disappears.

`panelLampsBegin()` and `actuatorBegin()` both write the level **before**
`pinMode(OUTPUT)`. A pin leaves reset as a high-Z input, and `pinMode(OUTPUT)`
makes it drive whatever the output register holds — LOW. The board fitted here
is active-low, so LOW is *energized* and mode-then-write would flick a lamp on
at every boot. The BTS7960's three inputs are active-HIGH so LOW is already safe
there, but they are written first anyway, so the rule is one rule rather than
two.

There is deliberately **no power-on lamp test**. Idle is the boot state, so
yellow comes on and stays on — that is not a lamp test and must not become one:
a green flash at power-up would read as the machine starting by itself, the one
impression it must never give. Test the lamps by starting a session.

`LED_ACTIVE_LOW` is **set** in `config.h`, because the lamp relay is a channel of
an active-low relay board. Clear it only for a board that energizes on HIGH, and
**check this before first power-up**: backwards, green is lit whenever the
machine is idle and yellow while it is working, which is the dangerous way round.

GPIO 23 was already the lamp pin under the single-LED design. GPIO 32 is free
again.

## The motor drive

The two motor relays are gone; a **BTS7960** H-bridge drives the motor through
`actuator.cpp`, which is still the only file that touches a motor pin.

**Why change.** A relay is on or off, so every stroke started and stopped by
slamming a loaded leg into and out of full speed. The bridge lets the drive be
faded, and that fade is the whole point of the change.

**PWM.** LEDC, 20 kHz, 10-bit (`MOTOR_PWM_HZ` / `MOTOR_PWM_BITS`). 20 kHz is
above hearing, so the patient hears the gearbox and not the drive, and it is
just under the BTS7960's 25 kHz datasheet ceiling. Drop to 10000 if the module
runs hot — it will whine, but switching losses fall. 10-bit is the most LEDC
holds at that frequency. If `ledcAttach` fails, `setup()` prints `LEDC attach
failed, the motor CANNOT be driven`; that machine will time out every stroke and
fault with nothing having moved.

**Ramping.** Every stroke fades in over `MOTOR_RAMP_MS` (400 ms) and every
*normal* stop fades out over the same. That costs travel — roughly half a ramp
at each end, about 0.4 s of a 7 s stroke — so **`liftSec` and `lowerSec` want
re-tuning on the bench** after this change.

Ramping is a comfort feature and never a safety one. These paths cut the drive
dead, with no fade:

| Path | Why |
|---|---|
| `Phase::Fault` | something is wrong with a leg that may be raised; another 400 ms of driving is the wrong answer |
| the energize cap | a stroke that ran long, from either the loop check or the `esp_timer` backstop |
| a live reversal | STOP during a lift; the dead time follows, the fade does not |
| `actuatorBegin()` | boot and reset |

`actuatorOff()` is the instant cut and `actuatorSoftOff()` is the fade; once a
fade has landed the two are identical, which is why `applyActuator()` can call
soft-off unconditionally for Idle, Hold and Rest.

A fade re-arms the `esp_timer` backstop with a **full** `capMs` rather than a
short window of its own. A window sized to the ramp is the obvious answer and it
is wrong: this loop's blocking budget is two 400 ms HTTP timeouts per iteration,
so an unreachable server routinely costs more wall clock than a ramp, and a
ramp-sized window would fault the machine in Hold every session the Wi-Fi was
down. The cost of the full cap is stated plainly in `actuator.cpp`: a stroke
plus a *stranded* fade can drive for up to twice `capMs` before the backstop
lands, where the relay build had one.

**There is no firmware-controlled cut in the motor supply.** A relay on GPIO 33
was designed and then dropped, and the reason is worth keeping: the machine
already has a panel **emergency stop** that opens the whole 12 V feed — motor
rail, panel lamps and the controller itself, all together.

That button strictly dominates what the relay could do. Both cover a shorted
high-side MOSFET, which no amount of firmware can stop, because it drives the
motor from `B+` regardless of what `EN` and the PWM pins say. Only the button
also covers a panic in *this* firmware — a relay driven by the firmware cannot
open when the firmware is the thing that died. The cost is that it is manual and
needs an operator present, which is already the rule this machine runs under and
is stated in `config.h`.

So GPIO 33 is free, the machine is down to two relay channels (the two lamps),
and everything `actuator.cpp` does to stop the motor is a signal-level cut: `EN`
LOW plus both PWM channels at zero.

## Jog mode — hold a button to move the leg by hand

For bench tuning and for nudging a leg into position. It is a mode, not a second
meaning for the buttons: outside it START and STOP are exactly the edges they
have always been, and STOP stays instant.

**Enter:** hold **STOP** for 2 s while the machine is idle (`JOG_ENTER_MS`).
A normal STOP tap still just clears the rep count — the hold is timed from the
debounced level, so a tap never accumulates toward it. Once the mode is on,
nothing drives until both buttons have been let go, so the entry hold never
turns into a lower and a START line that reads permanently pressed never lifts.

**Use:** hold **START** to raise, **STOP** to lower, at `MOTOR_JOG_DUTY` (half
duty). Release and the drive fades out. Both buttons held never moves the leg:
two directions at once has no sensible answer, and guessing one would drive a
leg the way the operator did not mean.

**Leave:** hold **START and STOP together** for 2 s (`JOG_EXIT_HOLD_MS`), tap
**ออกจากโหมด** on the CYD (sends `jogexit`), or let 20 s pass with nothing held
(`JOG_IDLE_EXIT_MS`). Either way the machine then
lowers the leg in full, the same quiet full-length descent a fault recovery
uses: green lamp solid, no session, nothing on the card. After the two-button exit,
STOP has to be let go before a new 2 s entry hold counts, so the exit does not
walk straight back into the mode. A session starting or a fault latching also
ends the mode, without the lowering, because the state machine takes the motor.

Entry is a hold on STOP rather than anything involving START because the START
edge starts a session the instant it is seen: there is no gesture beginning with
START that does not run the machine first.

Things worth knowing:

- **A jog marks the leg position unknown.** It moved the leg somewhere this
  firmware cannot know, which is the same state a power cut mid-stroke leaves
  behind, so it is recorded the same way — the next START lowers in full before
  it lifts.
- **The lamps blink green/yellow for the whole mode**, not only while a button
  is held. A held button moves the leg immediately, so the panel must not read
  as an ordinary idle at any point while the mode is on.
- **The CYD shows it too.** The state message carries `"jog":true`, and the main
  screen swaps the logo for **ปรับตำแหน่งขา** with the hint *ค้าง START ยก / STOP ลด*.
  The exit lowering shows as an ordinary descent, and plays no start cue: the
  cue now comes from the controller's one-shot `started` message, not from
  `running` going true.
  The settings gear is disabled for the same reason it is during a session.
- **Direction is a setting, not a wiring rule.** `MOTOR_REVERSED` in `config.h`
  picks which BTS7960 input lifts. The first bench machine had its motor leads
  the other way round (Lift went down), so it ships as 1. Check with the leg
  unloaded: START must raise it.
- **Every safety path still applies.** Jog drives through the same `request()` a
  stroke does, so it inherits break-before-make, the 50 ms reversal dead time,
  the energize cap and its hardware timer unchanged. There is deliberately no
  second way to drive the motor, because that would be a second way to get the
  cut wrong. A cap trip ends the press: release both buttons to jog again.

`MOTOR_JOG_DUTY` is a starting guess, not a measurement — see the note in
`config.h`. Too low and a geared actuator under a loaded leg buzzes without
moving, which looks exactly like a seized mechanism.

## The safety nets, and what each one actually covers

The limit switches are gone, and with them went the nets that depended on
knowing where the leg was — a wiring-fault check on two switches closed at once,
a bottom-switch-only-if-it-was-clear-at-the-start belief, and an Idle-can-lower
path keyed off that belief. What is left is smaller and does not pretend to know
the leg's position: it bounds how long the motor can be driven, and it makes
sure every stop and every recovery drives the same bounded descent whether or
not that descent actually gets the leg to the bottom.

**1. Break-before-make, plus a 50 ms dead time.** `actuator.cpp` is the only
file that touches the motor pins. It always zeroes the idle half-bridge before
raising the driving one, and on a direction reversal it cuts the drive dead,
waits `DEADTIME_MS`, and drives the new direction from `actuatorTick()`. The
wait matters because commanding a reversal apart is not the same as it being
apart: a STOP press during a lift goes from UP to SafeLower's DOWN in a single
loop iteration, and without the wait a loaded, still-turning motor is slammed
into reverse in the microseconds between two writes. The BTS7960 survives that;
the gearbox and the leg on it are what the dead time protects. The same wait
covers a reversal that catches a soft stop part-way down — a fade is not a
substitute for it. The relay-era hazard it replaced was contacts that had not
released yet; the mechanism is the same, the reason is now mechanical.

**2. The travel cap, enforced twice.** `actuatorTick()` checks it every loop,
and an `esp_timer` one-shot armed on every energize cuts the drive from the
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
lands in Idle; START queues a fresh session behind it (behind a confirmation
first, if a display is linked to show one — see above), so the descent runs
first and the new LIFT begins right after. Neither is refused by a Fault, and
neither drives the actuator further in the direction that may have caused it —
descents only ever command DOWN. The lowering reports nothing of its own: the
session was already recorded when it faulted, and a second report would put a
duplicate row on the card.

There is no confirmation that the descent actually reached the bottom — with the
limit switches gone the firmware has no way to check, which is stated plainly in
`config.h`: a slipping belt, a jammed actuator and a clean stroke all look
identical to this firmware, "the motor was driven for `lowerSec`". A recovery
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

A BTS7960 output that has failed open, or a jammed actuator, leaves no way down by
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

- Motor drive: a **BTS7960** H-bridge module. RPWM GPIO 22 (P3 connector), LPWM
  GPIO 27 (CN1) — the two wires the relay coils used, so the harness is
  unchanged. `R_EN` and `L_EN` are wired **together** to GPIO 4. All three are
  active-HIGH and LOW is the safe level on each; there is no `RELAY_ACTIVE_LOW`
  for the motor any more. There are no limit switches — travel is timed by
  `liftSec`/`lowerSec`, both editable and capped by `maxTravelSec`.
- Emergency stop: a panel button that opens the whole 12 V supply — motor rail,
  lamps and the controller together. It is the machine's only hardware cut and
  the only thing that stops a shorted output FET. Not wired to any GPIO.
- Panel buttons: START GPIO 25, STOP GPIO 26. Momentary, INPUT_PULLUP, other
  side to GND; both normally-open (`BTN_START_NC` / `BTN_STOP_NC`, both `0` in
  `config.h`). GPIO 33 is free — nothing reads it and nothing reserves it.
- Panel lamps: one relay on GPIO 23, green on its NO contact and yellow on NC.
  Green while a session is in progress, yellow while idle, blinking for jog mode
  and a latched FAULT. The relay board is active-low, so `LED_ACTIVE_LOW` is set.
  GPIO 32 is free.
- UART: controller GPIO 16 (RX) / 17 (TX) ↔ CYD GPIO 22 (RX) / 27 (TX),
  cross-wired, common GND. 19200 8N1, bench-confirmed. Both RX pins enable their
  internal pull-up in `setup()`; a floating RX prefixes every frame with `0xFF`
  junk, and the ~45k internal is weak enough that a **10k to 3V3 on the CYD's
  GPIO 22** is still worth fitting — watch `rxTrimmed` to tell.
