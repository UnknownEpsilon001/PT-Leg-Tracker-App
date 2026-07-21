# SD-Backed Device Settings (`settings.json`) — Design

**Date:** 2026-07-21
**Status:** approved, not implemented
**Extends:** `2026-07-20-offline-patient-sd-logging-design.md` (shares the microSD
card and its VSPI/touch-bitbang prerequisite)
**Firmware plan it builds on:** `2026-07-19-esp32-firmware-design.md` (two-MCU
split, UART link, device-code pairing)

## Problem

Device settings — Wi-Fi credentials, server URL, device code, cycle timings —
live only in the controller's NVS today. Three consequences:

1. Every change is typed on a 320x240 resistive touchscreen. A WPA password is
   the worst case, and it has to be retyped whenever the machine moves to a new
   network (contest venue, different room).
2. Replacing the controller board means retyping everything, because NVS goes
   with the old board.
3. Configuring a second machine repeats the whole exercise.

## Solution

Store settings as human-editable JSON on the CYD's microSD card. The card is the
master copy; the controller keeps an NVS copy so it still runs correctly with the
screen unplugged or the card absent.

The controller cannot read the card — it is physically on the CYD's SPI bus — so
the CYD pushes settings to the controller rather than the controller pulling.

## File format

`/settings.json`, flat, one object, no nesting:

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

Field names match `DeviceSettings` members exactly. Missing keys take the default
value; unknown keys are ignored. ArduinoJson 7 is already a display-side
dependency (`link.cpp`), so no new library is needed.

Defaults, when no other source exists:

| Field | Default |
|---|---|
| `wifiSsid`, `wifiPass`, `serverUrl` | `""` |
| `deviceCode` | `"default"` |
| `maxTravelSec` | 8 |
| `holdSec` | 10 |
| `restSec` | 10 |

`deviceCode` defaulting to `"default"` matches the phone app's single-device
zero-setup flow. The controller currently defaults it to `""`; this design
changes `DEFAULT_DEVICE_CODE` on the controller to `"default"` so both ends
agree without any user setup.

## Boot flow

The CYD drives reconciliation, reusing the existing `hello` and `settings` UART
messages. **No link-protocol change.**

1. CYD mounts the SD card and reads `/settings.json`.
2. CYD sends `hello`. The controller replies `settings` with its NVS copy. That
   reply doubles as proof the controller is up, which removes any dependency on
   which board boots first.
3. **Card holds a valid file:** the CYD sends `settings` with the card's values.
   The controller applies them, saves to NVS, and reconnects Wi-Fi.
4. **Card missing, or file absent:** the CYD adopts the controller's reply as its
   working copy, and — if a card is present — writes `/settings.json` from those
   values.

Step 4 means a fresh card seeds itself from the machine's current configuration
rather than from blank defaults. Inserting a new blank card into a configured
machine does not wipe it. On a brand-new machine the controller's NVS already
holds defaults, so the result is the documented default file either way.

**Precedence rule:** a readable card wins over controller NVS. This is the single
conflict rule in the design, and it is the intended one — editing the file on a
PC has to actually take effect.

**Known consequence:** an old card carrying stale settings will overwrite good
NVS settings on boot. Mitigated, not eliminated: the settings screen shows the
loaded values and an `SD: loaded` indicator, so the state is visible rather than
silent.

## Save flow

On SAVE from the settings screen:

1. Write `/settings.json` to `/settings.tmp`, then rename over the real file, so
   a card pulled mid-write cannot leave a truncated file.
2. Push the values to the controller over the UART link (existing behavior).

If no card is mounted, the push still happens and the UI reports that the card
was not written. Settings are never silently dropped.

## Reset to defaults

A `RESET DEFAULTS` button on the settings screen:

1. Asks for confirmation. One stray tap otherwise costs the Wi-Fi password.
2. On confirm: loads defaults into the on-screen fields, writes them to
   `/settings.json`, and pushes them to the controller.

Reset writes a defaults file rather than deleting it, so the file always exists
and is always the thing being read.

## Failure handling

| Situation | Behavior |
|---|---|
| No card at boot | Run from controller NVS. Settings screen shows `SD: none`. Saves push to controller only |
| `/settings.json` malformed | Rename to `/settings.bad.json`, write a fresh file from the controller's values, carry on. The machine never refuses to run |
| Card write fails | UI reports the card write failed; the controller still received the values |
| Card inserted after boot | Remount attempted when the settings screen opens. No polling loop |
| Card present, file absent | Treated as "no file": seed from the controller's reply (step 4) |

The governing rule from the base firmware design is unchanged: **the machine
never refuses to run because of a settings, card, or link problem.** Motion and
the phone path always work; configuration convenience degrades on its own.

## Security note

The Wi-Fi password is stored in plaintext on removable media. This is inherent to
the goal — editing the password on a PC requires it to be readable there. Accepted
deliberately for a clinic/contest LAN. If that changes, the mitigation is to omit
`wifiPass` from the file and keep it NVS-only, at the cost of typing it on the
touchscreen.

## Components

New: `firmware/pt-leg-display/settingsfile.{h,cpp}`

```cpp
void settingsFileDefaults(DeviceSettings& s);      // documented defaults
bool settingsFileLoad(DeviceSettings& s);          // false: no card / no file / malformed
bool settingsFilePresent();                        // a valid file was read this boot
bool settingsFileSave(const DeviceSettings& s);    // temp + rename; false on write failure
```

Depends on the SD card already being mounted by `store.cpp` (from the offline
patient plan), so mount logic exists in exactly one place. Modified alongside:
`ui.{h,cpp}` (reset button, SD status line), `pt-leg-display.ino` (boot
reconciliation), and the controller's `config.h` (`deviceCode` default).

**LVGL memory constraint:** the reset confirmation dialog must be built once and
reused, or explicitly deleted when dismissed. The LVGL pool is 48 KB and
`LV_ASSERT_HANDLER` is `while(1)`, so a leaked dialog eventually produces a
silent hard freeze. This is not hypothetical — the settings screen shipped with
exactly this bug on 2026-07-21 and froze the device after five opens.

## Testing

No host test framework exists for the firmware; this is a bench matrix, run on
the two-board rig:

1. **No card** — boots, runs from NVS, screen shows `SD: none`, save still reaches the controller.
2. **Blank card** — file is created from the controller's current settings; contents match what the screen shows.
3. **Hand-edited card** — change the SSID in Notepad, boot, confirm the controller connects to the new network.
4. **Malformed JSON** — truncate the file mid-object; confirm `settings.bad.json` appears, a fresh file is written, machine runs.
5. **Reset** — confirm dialog appears, cancel changes nothing, confirm restores documented defaults on card, screen, and controller NVS.
6. **Persistence** — power-cycle both boards; settings survive.
7. **Cloning** — move the card to a second machine; it comes up configured, `deviceCode` edited on the card is what the app pairs with.
8. **Card pulled mid-save** — no truncated `settings.json`; the previous file or the temp file remains, never a corrupt one.
9. **Leak check** — open settings and trigger the reset dialog 20+ times; `lv_mem_monitor` free bytes stay flat.

## Out of scope

- Syncing settings to or from the server or phone app. The app configures itself, not the machine.
- Encrypting or hashing the stored password.
- Patient data and session logs — those are `/patients.csv` and `/sessions.csv` from the offline patient design.
- Any change to the UART link message schema.
