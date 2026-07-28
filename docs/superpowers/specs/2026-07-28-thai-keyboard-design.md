# Thai On-Screen Keyboard for the CYD — Design

**Status:** approved
**Date:** 2026-07-28

## Problem

The CYD's two on-screen keyboards are Latin-only. Patient names are Thai, so the
add-patient form cannot record a patient's actual name — today a name is either
transliterated or left in Latin. The device UI has been Thai since 2026-07-25;
the input has not caught up.

## Decision summary

A phone-style Thai keyboard, available on **both** keyboards, built on LVGL's
`USER_1` / `USER_2` keyboard modes. The add-patient keyboard opens on Thai, the
settings keyboard opens on Latin.

## What was measured first

Whether Thai renders at all was an open question: an earlier note claimed every
Kanit combining mark sits at `ofs_y = +15`, which would make a vowel and a tone
mark on one consonant overprint and would have made typed Thai pointless.

Measured from the generated font tables, and bench-checked by putting
`เกิ่น น้ำ ผู้ ปู่` in `patients.csv` and reading the roster on the panel:

| Font | `ิ` U+0E34 | `่` U+0E48 | Effect |
|---|---|---|---|
| thai14 | `ofs_y 8`, h2 | `ofs_y 9`, h3 | ~1 px touch, reads clean |
| thai20 | `ofs_y 12`, h2 | `ofs_y 12`, h4 | same baseline, tone over vowel |
| thai28 | `ofs_y 18`, h3 | `ofs_y 18`, h5 | same, more pronounced |

Stacks are usable. The roster draws at thai14, which is the cleanest of the
three, and all six test names rendered correctly. No font surgery is needed
before this work. Text that may carry stacks should prefer thai14.

## Layout

Ten columns — 320 px ÷ 10 = 32 px keys — over four character rows and one
control row, in a 120 px keyboard. Ten columns rather than a PC Kedmanee's
twelve, because 26 px keys on a resistive panel are the riskiest part of the
design and 32 px is the phone-like target.

```
page 1 (Thai)     ๆ ไ ำ พ ะ ั ี ร น ย
                  บ ล ฟ ห ก ด เ ้ ่ า
                  ส ว ง ผ ป แ อ ิ ื ท
              ⇧   ม ใ ฝ ถ ุ ึ ค ต   ⌫
            ?123  🌐  ─── space ───  OK
```

Page 2 (`⇧`) carries the rest: ธ ณ ญ ฐ ฏ ฤ ฆ โ ฌ ็ ๋ ษ ศ ซ ฉ ฮ ์ ฒ ฬ ภ จ ข ช ฑ ฎ ฯ.

Key roles: `⇧` flips between the two Thai pages, `🌐` switches to the Latin
pages, `?123` reaches LVGL's number/symbol page, `⌫` and `OK` behave as they do
today.

**No long-press alternates.** A phone reaches rare characters by holding a key;
LVGL's button matrix has no popup layer. Every character is therefore reachable
from page 1 or page 2, which is why page 2 is mandatory rather than a nicety.

Thai numerals (๐-๙) are omitted. Arabic numerals already exist on the `?123`
page and the roster's age field is Arabic.

## Components

| File | Responsibility |
|---|---|
| `firmware/pt-leg-display/thai_kb.h` (create) | One entry point: `void thaiKbApply(lv_obj_t* kb, bool startThai)` |
| `firmware/pt-leg-display/thai_kb.cpp` (create) | Both key maps, both control-flag arrays, the event handler |
| `firmware/pt-leg-display/ui.cpp` (modify) | Two call sites — `kb` (settings, `startThai = false`) and `kbAdd` (add-patient, `startThai = true`) |

The maps and their control arrays are roughly 80 lines of tables. `ui.cpp` is
already about 1100 lines; the tables live in their own file so it does not grow
further, and both keyboards share one implementation rather than two copies.

## The event-handler substitution

LVGL's built-in `lv_keyboard_def_event_cb` recognises only its own mode-switch
keys. A `🌐` or `?123` key defined in a user map is not one of them, so the
default handler **types the key's label into the textarea as literal text**
instead of switching pages.

`thaiKbApply` therefore removes the default handler with
`lv_obj_remove_event_cb(kb, lv_keyboard_def_event_cb)` and installs its own,
which handles:

- ordinary keys → `lv_textarea_add_text(ta, txt)`, which is UTF-8 safe
- `⌫` → `lv_textarea_del_char(ta)`
- `OK` → send `LV_EVENT_READY`, matching today's behaviour
- `⇧` → toggle `USER_1` / `USER_2`
- `🌐` → `LV_KEYBOARD_MODE_TEXT_LOWER`, and from Latin back to `USER_1`
- `?123` → `LV_KEYBOARD_MODE_SPECIAL`

This is the single most likely thing to be skipped and the single most likely
thing to produce a keyboard that types `🌐` into a patient's name.

## Behaviour

- The add-patient keyboard opens on Thai; the settings keyboard opens on Latin.
  Names are Thai, SSIDs and URLs are not.
- Script choice is per-keyboard and not remembered across opens. Reopening the
  add-patient form starts on Thai again.
- Existing screens are untouched. The keyboards keep their current size,
  position, show/hide behaviour and `LV_EVENT_READY` / `LV_EVENT_CANCEL` wiring.

## Known risk, accepted

Thai characters can now be typed into `deviceCode` and `serverUrl` on the
settings screen. The server matches `deviceCode` byte-for-byte for pairing and
`serverUrl` must be ASCII to resolve, so a Thai character in either fails
silently — the machine simply never pairs. The mitigation is that the settings
keyboard opens on Latin, making Thai a deliberate two-tap detour rather than
somewhere a user lands by accident. The user chose Thai on both keyboards with
this trade-off stated.

## Testing

1. Type a stack-heavy name (`เกิ่น`) into the patient name field, save, and
   confirm the row in `patients.csv` is valid UTF-8 with no BOM.
2. Confirm that name renders correctly in the roster and on the finish screen.
3. Confirm the settings fields still take Latin, and that SAVE still reaches the
   controller and the card.
4. Flip `⇧`, `🌐` and `?123` 20+ times and confirm the LVGL pool holds — the
   failure mode this codebase has shipped before is a silent freeze after a few
   opens.
5. Check key hit accuracy at 32 px on the resistive panel, on both pages.
6. Confirm `OK` and the back button behave as they do today on both screens.

## Out of scope

Long-press alternates, a Thai layout for the phone app, Thai numerals,
predictive text, and any change to the roster CSV format.
