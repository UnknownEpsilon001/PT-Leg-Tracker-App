# Display Audio Cues — Design

Date: 2026-08-29
Status: approved by user (brainstorming session 2026-08-29)
Board: display (CYD) only. The controller sketch is untouched apart from the
byte-identical `settings.h` that `sync-shared.ps1` keeps in step.

## Motivation

The machine is silent. An operator or patient learns that a session started,
stopped, or hit its rep target only by looking at the screen, and during therapy
neither of them is reliably looking at it — the patient is on the machine and the
operator is watching the leg. Spoken Thai prompts put those three events in the
room instead of on the panel.

The CYD carries a speaker connector on GPIO 26, which is the one pin this board
has left. Nothing else in the firmware claims it.

## Scope

In:

- Four cues: boot/ready, session start, session stop, rep target reached.
- Thai voice prompts, played from `.wav` files on the existing microSD card.
- A sound on/off switch on the settings screen, persisted to `/settings.json`.
- A conversion tool that produces files in the one format the parser accepts.

Out:

- **No fault cue.** Decided against by the user. The trigger table and the cue
  enum are shaped so adding it later is one row and one branch — see
  *Triggers*, where `LinkDone.fault` already arrives at the right place.
- No per-rep or per-phase sounds. A cue every 34 s for a whole session is noise,
  not information.
- No volume control. The switch is on/off. There is no runtime gain stage at
  all — loudness is whatever the files were recorded and normalised to, so
  making the machine louder means re-running `make-sounds.ps1`, not changing a
  constant.
- No change to the UART link protocol. `link.h`, `link.cpp` and
  `link-protocol.md` are not touched by this work.

## The hardware unknown, and why it gates everything

The user has an 8 Ω speaker and no confirmed amplifier. GPIO 26 is a DAC pin
sourcing a few milliamps; an 8 Ω voice coil across it is close to a short, and
the DC bias sitting on that coil is the second problem. Some `ESP32-2432S028R`
revisions populate an SC8002B mono amp behind the speaker JST and some ship the
pads empty.

**Nothing in this design may be wired to a speaker until bench step 0 (below)
has established which board is in hand.** If the amp is absent the firmware is
unchanged — an external PAM8403 or MAX98357A module goes between GPIO 26 and the
speaker, and the only thing that shifts is the wiring diagram.

## Decisions

| Decision | Why |
|---|---|
| `dac_continuous` driver, not `i2s_set_dac_mode` | Arduino core here is **3.3.11** (IDF 5.x). The legacy call still exists but only under `driver/deprecated/`. Every CYD audio example in circulation uses it; none of them are a model for this code. |
| `chan_mask = DAC_CHANNEL_MASK_CH1` | CH1 is GPIO 26 alone. The legacy I²S DAC path brings up **both** channels by default — its own header says so — and channel 0 is GPIO 25, which on this board is the touchscreen clock. Enabling both trades a speaker for a dead panel. |
| 22050 Hz sample rate | `dac_continuous_config_t::freq_hz` with `DAC_DIGI_CLK_SRC_DEFAULT` has a **19.6 kHz floor** on the ESP32. 16 kHz would work only by switching to APLL, which buys a smaller file and costs a clock source. |
| 8-bit unsigned mono | Native DAC width. Any other depth means converting every sample on the way out, for a signal that leaves through a 1 W mono amp into a small speaker. |
| Preload the clip to RAM, then play from RAM | See below. |
| Stop and target come from the `done` message, not a state edge | See *Triggers*. Boot and start have no such message and are handled separately. |
| Setting named `soundOn`, default true | Positive sense end to end: JSON key, struct field, switch label, and API all mean the same thing. A `muted` field behind a "เสียง" switch is an inversion, and inversions get flipped. |

### Rejected: stream from the card during playback

The obvious design keeps the `File` open and tops up the DMA from `loop()`.
It costs about 5 KB of RAM instead of 48 KB.

It was rejected because of *when* the reads land. The card is on VSPI, shared
with `sessions.csv` and `/settings.json`. The stop cue and the `sdlog` row for
the finished session are triggered by the same `done` message — so the SD reads
feeding the stop sound would interleave with an SD append, plus LVGL's flush, at
the one moment of the session that anyone is watching. A latency spike there is
an audible stutter. Preloading moves every card access to *before* the first
sample is emitted, which removes the interaction rather than tuning it.

### Rejected: bake the clips into flash

Converting the `.wav` files to C arrays, the way `logo_img.c` and `font_th_*.c`
already do, costs no heap, no card, and cannot fail at runtime. It was rejected
because changing a prompt would mean a reflash, and the user asked for the card
specifically. It remains the fallback if the card path proves unreliable on the
bench.

## Components

### Display: `audio.h` / `audio.cpp` (new)

The whole feature, in one module with one job. Same shape as `sdlog`, `store`
and `touch_bb`.

```cpp
enum class Cue { Boot, Start, Stop, Target };

void audioBegin();            // allocate the buffer, create the DAC channel
void audioLoop();             // pump: chunk-load from SD, feed the DAC
void audioPlay(Cue c);        // request a cue; the latest request wins
void audioSetEnabled(bool on);
bool audioReady();            // false = no buffer or no DAC; permanently silent
```

Two rules the implementation must hold:

- **`audio.cpp` never touches LVGL.** The UI does not know this module exists;
  the sketch is the only caller.
- **`audioLoop()` never blocks for more than about a millisecond.** It does at
  most one 4 KB card read or one non-blocking DMA write per call and returns.

It opens the card directly, which matches `sdlog.cpp` and `settingsfile.cpp`.
The rule recorded in `store.h` — that the SD card is not touched directly — is a
rule about `ui.cpp`, not about modules.

#### Playback state machine

```
Idle ──audioPlay()──▶ Loading ──buffer full──▶ Playing ──drained──▶ Idle
         │                 │                       │
         │                 └── open/parse fails ───┤
         └── disabled, or !audioReady() ───────────┘
```

`Loading` reads 4 KB per `audioLoop()` call. A new `audioPlay()` during either
`Loading` or `Playing` abandons the current clip and restarts at `Loading` —
the newest event is the one worth hearing.

#### DAC configuration

```c
dac_continuous_config_t cfg = {
    .chan_mask = DAC_CHANNEL_MASK_CH1,   // GPIO 26 only — CH0 is GPIO 25, touch CLK
    .desc_num  = 4,
    .buf_size  = 1024,                   // 4 KB of DMA in total
    .freq_hz   = 22050,                  // >= the 19.6 kHz floor for this clock source
    .offset    = 0,
    .clk_src   = DAC_DIGI_CLK_SRC_DEFAULT,
    .chan_mode = DAC_CHANNEL_MODE_SIMUL, // one channel enabled, so this is moot
};
```

Feeding uses `dac_continuous_write(h, p, n, &loaded, 0)`. A timeout of 0 makes
it a partial, non-blocking write: it takes what fits in the DMA descriptors and
reports how much, and `audioLoop()` returns with the rest still pending.

The channel is enabled on entry to `Playing` and disabled on the way back to
`Idle`. **It must be disabled on a sample of 0x80.** The DAC holds its last
written code, so disabling on a loud sample steps the output and pops; 0x80 is
silence for unsigned 8-bit, which is why the conversion tool pads both ends of
every clip (see below).

### Display: `settings.h` (shared file — sync required)

Add one field:

```cpp
bool soundOn;  // เปิดเสียงพูดบนบอร์ดจอ — spoken cues on the display board
```

`sync-shared.ps1` enforces this file byte-identical against
`pt-leg-controller/settings.h`, so the controller's copy gets the field too and
must be synced in the same commit or `-Check` fails the next build.

The controller never reads it. That is deliberate and worth stating in the
comment: the speaker is on the display board, so the setting belongs to the
board that owns the hardware, and duplicating it into controller NVS is the
price of one shared struct.

**`soundOn` is deliberately NOT added to `LinkSettings`.** It does not cross the
wire. Two consequences, both wanted:

- No wire-format change, so `link-protocol.md` and both copies of `link.cpp`
  are untouched and no display/controller image-version skew is introduced.
- `onLinkSettingsReply()` assigns `gLast` **field by field**, never as a whole
  struct, so a field absent from `LinkSettings` survives a controller settings
  push with no extra code. This is load-bearing — anyone who "tidies" that
  function into `gLast = built` silently resets the user's sound setting on
  every reconnect.

### Display: `settingsfile.cpp`

Read and write the `soundOn` key alongside the existing ones, defaulting to
`true` when absent so an existing card keeps working and arrives with sound on.
It needs no `clampU16`-style guard — a bool read through ArduinoJson's `|`
default operator is already total.

`settingsFileDefaults()` gets `soundOn = true`.

### Display: `pt-leg-display.ino`

- `gLast`'s aggregate initializer (currently `{"", "", "", "default", 8, 4, 4,
  10, 10, 10}`) gains the new field explicitly. Leaving it short would
  value-initialize `soundOn` to `false` — sound off by default, from a line that
  does not mention sound.
- `setup()`: `audioBegin()` after `storeBegin()`, then `audioPlay(Cue::Boot)` at
  the very end. Boot is last so the sound is evidence that the whole init path
  completed, not just that the DAC came up.
- `loop()`: `audioLoop()` alongside `uiLoop()` and `gLink.poll()`.
- `onLinkState()`: the start edge (below).
- `onLinkDone()`: the stop and target cues (below).
- `onSaveFromUi()` already does `gLast = s`, and `s` comes from the settings
  screen, so the new field flows to `settingsFileSave()` with no change. It also
  calls `audioSetEnabled(s.soundOn)`.

### Display: `ui.h` / `ui.cpp`

One `lv_switch` row in `colSettings`, styled like the rows around it.

Label: **เสียง** / Sound. Chosen partly because it renders cleanly — no vowel
stacked over a tone mark, which this LVGL build cannot shape.

The switch reads and writes `DeviceSettings::soundOn` through the existing
`uiOpenSettings(const DeviceSettings&)` / `uiSetSettingsSaved(...)` pair. No new
UI callback is introduced.

### `firmware/tools/make-sounds.ps1` (new)

An `ffmpeg` wrapper, in the shape of the existing `make-logo.ps1`. Given an
input recording it emits a file the parser will accept, and refuses to emit one
it will not:

- resample to 22050 Hz, downmix to mono, convert to unsigned 8-bit PCM
- pad **20 ms of 0x80 silence at both ends**, so the clip starts and ends on the
  DAC's rest value and the channel can be enabled and disabled without a pop
- write a plain `RIFF`/`fmt `/`data` file with no `LIST` or metadata chunks
- report duration and byte size against the buffer cap, and **fail rather than
  write** a file that will not fit

Encoding note: the script must not round-trip any Thai text through PowerShell
5.1's `Get-Content`/`Set-Content`, which double-encodes and adds a BOM.

### `firmware/pin-layout.html`, `firmware/README.md`

The display board's pin table currently reads that the CYD has exactly three
free wires. GPIO 26 becomes a fourth entry — speaker, onboard amp — and the
"do not reuse" table gains it. Both tables are bilingual; the Thai column is
required, not optional.

The README gains the `/sounds` card layout and a pointer to `make-sounds.ps1`.

## Card layout and file format

```
/sounds/boot.wav      "เครื่องพร้อมใช้งาน"
/sounds/start.wav     "เริ่มการทำงาน"
/sounds/stop.wav      "หยุดการทำงาน"
/sounds/target.wav    "ครบตามเป้าหมายแล้ว"
```

Accepted format, and nothing else: RIFF WAV, `audioFormat == 1` (PCM),
1 channel, 8 bits per sample, 22050 Hz, `data` chunk no larger than the buffer.

The parser **walks the chunk list** for `fmt ` and `data`. It does not assume
the canonical 44-byte header. This matters: `ffmpeg` will happily emit a `LIST`
chunk between the two, and a parser that seeks to offset 44 would then play
metadata bytes as audio — a burst of noise whose cause is invisible from the
symptom.

Every rejection prints one serial line naming the file and the field that failed
(`sounds/start.wav: 16-bit, expected 8`). **Silence is never an acceptable
failure mode** — a silent speaker and a rejected file must be distinguishable
without an oscilloscope.

## Buffer budget

One allocation at `audioBegin()`, held for the life of the boot. Never a
per-play `malloc`, which would fragment the heap over a day of sessions.

| Size | Max clip | Role |
|---|---|---|
| 48 KB | 2.23 s | preferred |
| 24 KB | 1.11 s | first fallback |
| 16 KB | 0.74 s | last fallback |
| — | — | `audioReady()` false, machine runs silent |

This mirrors the draw-buffer allocation in `ui.cpp`, which halves until it fits
and restarts the chip only when nothing fits. Audio does not even restart: it
logs the failure and the machine runs exactly as it does today.

Loading 4 KB per `audioLoop()` puts a full 48 KB clip about 12 iterations out.
Measured latency belongs in the bench notes; the design assumes 60–180 ms, which
is below the threshold at which a cue feels detached from its event.

## Triggers

| Cue | Condition | Where |
|---|---|---|
| Boot | once, end of `setup()` | `pt-leg-display.ino` |
| Start | `running` false → true | `onLinkState()` |
| Stop | `done` arrives, `target == 0 \|\| reps < target` | `onLinkDone()` |
| Target | `done` arrives, `target != 0 && reps >= target` | `onLinkDone()` |

### Why stop and target come from `done`, not from a state edge

The `running` flag going true → false looks like the obvious stop trigger. It is
not, because the display cannot distinguish "the session ended" from "the link
dropped and the watchdog cleared the state." A machine that announces "หยุดการ
ทำงาน" every time a jumper wire glitches is worse than one that says nothing.

`LinkDone` is the right event and already exists: it is emitted exactly once per
session, it is on the retransmit list (unlike `state`, which is a periodic
snapshot deliberately excluded from it), and it carries the final rep count — so
the stop/target split is decided from the numbers the controller actually
finished with, not from the last snapshot that happened to arrive.

It also carries `fault`. The fault cue the user declined is, in this shape, one
`else if` in a handler that already runs at the right moment.

### Two guards on the start edge

Start has no one-shot message, so it must edge-detect — and both failure modes
are real:

1. **Link recovery.** Gate the edge on `gLinkUp`. A transition observed while
   the link is considered down is a reporting artifact, not a session.
2. **Booting beside a running machine.** The *first* `state` frame after boot
   seeds the previous-running value without emitting a cue. Otherwise a display
   restarted mid-session announces a start that already happened.

## Failure handling

| Condition | Behaviour |
|---|---|
| No card, or `/sounds/x.wav` missing | One serial line. That cue is a no-op; the others still work. |
| File fails format validation | One serial line naming the file and the field. Cue is a no-op. |
| Buffer allocation failed entirely | `audioReady()` false. Logged once at boot. Machine runs silent. |
| `dac_continuous_new_channels()` fails | Same. Logged with the `esp_err_t`. |
| A cue arrives while one is playing | The newer cue wins; the older is abandoned mid-clip. |
| Sound switched off | Dropped inside `audioPlay()`, before any card access. |

Nothing in this list stops the machine, blocks a session, or changes what the
screen shows. Audio is strictly additive.

## Testing

### Step 0 — the amp check, before a speaker is ever connected

With no speaker attached: power the board, `dacWrite(26, 128)`, and measure.

- GPIO 26 should read about 1.65 V.
- Probe the speaker JST. If it reads that same ~1.65 V DC, **there is no
  amplifier on this board — do not connect the 8 Ω speaker.** Fit an external
  amp module between GPIO 26 and the speaker instead.
- If an amp is present its output should sit near 0 V DC and swing with the
  signal.

Every step below assumes step 0 has been passed and the wiring settled.

### Bench matrix

| # | Check | Passes when |
|---|---|---|
| 1 | Touch after `audioBegin()` | Every settings-screen control still responds. Proves the CH1-only mask left GPIO 25 alone. |
| 2 | Boot cue | Sound plays once, after the UI is up, on a real power cycle. |
| 3 | Start cue | Fires on the panel START button, once. |
| 4 | Stop cue | Fires on a STOP before the target, once, and not on the target clip. |
| 5 | Target cue | Run with a target of 2. Target clip plays; stop clip does not. |
| 6 | Link-drop false positive | Pull a UART jumper mid-session and reseat it. No start cue. |
| 7 | Display reboot mid-session | Reset the display while the controller runs. No start cue. |
| 8 | Stop cue against the SD write | The stop cue and the `sessions.csv` append happen together. Audio must not stutter, and the row must still be written. |
| 9 | Heap | `ESP.getFreeHeap()` before `audioBegin()`, after, and after 20 plays. Flat across the last two. Drive it from `UI_TEST_HOOK`. |
| 10 | Sound switch | Off silences all cues. Survives a power cycle. Verify the value in `/settings.json` on a PC. |
| 11 | Bad file | Drop a 16-bit WAV in as `start.wav`. Serial names the file and the field; no noise; other cues unaffected. |
| 12 | No card | Every cue is a no-op; the machine runs normally. |
| 13 | Pop | Listen at the start and end of each clip. No click. |

Flashing rule, unchanged: **pull the controller↔CYD UART jumpers before any USB
flash**, then power both boards to test.

Note what the `UI_TEST_HOOK` harness cannot do here. It drives the UI and reads
back what the firmware believes; it cannot hear anything. Every row above that
says "sound plays" needs ears on the bench.

## Risks

- **The amp may not be populated.** Highest-probability risk and the reason for
  step 0. Costs an external module, not a redesign.
- **Voice quality at 8-bit / 22 kHz through a small speaker.** Thai tones carry
  meaning, and 8-bit quantisation noise is audible on speech. If the prompts are
  hard to make out, the cheapest fix is re-recording closer to the mic with
  normalisation in `make-sounds.ps1` — not a format change.
- **`sync-shared.ps1` drift.** Touching `settings.h` without syncing fails the
  next `-Check`. Both copies go in the same commit.
- **A "tidied" `onLinkSettingsReply`.** Documented above and worth a comment in
  the code: whole-struct assignment there silently resets `soundOn` on every
  controller reconnect.
- **Latency perception.** If 180 ms reads as detached from the button press, the
  fix is a shorter clip or a larger first chunk, both cheap. It is not a reason
  to go back to streaming.
