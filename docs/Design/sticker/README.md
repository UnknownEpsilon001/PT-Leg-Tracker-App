# Control panel decal — 180 × 150 mm

Front-face sticker for the controller box. Dark base, red accents, PCB-trace
background. Thai labels with small English subtitles.

## Files

| File | What it is |
| --- | --- |
| `panel-sticker.svg` | Source, 1:1 mm, fonts embedded. Opens in Illustrator or Inkscape. |
| `panel-sticker.pdf` | The decal for the printer, 186 × 156 mm page. Openings print as black shapes; no cut lines. |
| `panel-sticker-preview.png` | 300 dpi flat preview. For checking only. |
| `make-panel-sticker.py` | Generates the SVG. Every dimension is asserted here. |
| `render.py` | Exports the PDF and the PNG through headless Chrome. |
| `panel-sticker-figma.svg` | The same artwork with the embedded fonts stripped, for dragging into Figma. At ~630 KB it is now too large to push through the Figma MCP in one call; import it by hand. |
| `fonts/` | Kanit woff2 subsets, vendored so the generator does not depend on `node_modules`. |
| `logos/` | Trimmed, print-sized copies of the three logos. |

## Figma

An editable copy lives at
<https://www.figma.com/design/RXQOuAGxhVc5Ig9reV1WXY>, built by importing
`panel-sticker-figma.svg` and retargeting the text to real Kanit SemiBold and
Bold. The frame is **1 unit = 1 mm**, so the cutouts read as the drawing's own
numbers — the display window sits at x 35, y 26 and the buttons at x 30, 70, 110,
150. Layers: `Artwork` and `Crop marks (cyan)` (the Figma copy predates the hand-cut change and still has `Cut lines (magenta)`),
both locked.

**The Figma copy is currently behind.** It still carries the `"pcb"` background;
the swap back to `"classic"` ran into the Starter plan's MCP tool-call limit. To
bring it level, drag `panel-sticker-figma.svg` into the file, delete the old
frame, and set the text to Kanit SemiBold and Bold — Bold on the two title lines,
the e-stop caption and the English button labels, SemiBold on everything else.

Figma has no millimetre unit, so an export from there comes out at 186 × 156 pt
rather than millimetres. Treat the Figma file as the place to try changes, and
keep `panel-sticker.pdf` from the generator as the file that goes to the printer
— it is the one whose dimensions are asserted.

Regenerate after any edit:

```bash
python make-panel-sticker.py && python render.py
```

The script fails loudly rather than emitting a decal with a hole in the wrong
place — the assertions in `check()` cover every cutout position, every stated
gap on the drawing, and every text band.

## Geometry

Taken from `../Of2XulPK.jpg`. All values in millimetres, origin at the top-left
corner of the face.

| Cutout | Position | Size |
| --- | --- | --- |
| Display window | x 35, y 26 | 70 × 51, corners R2 |
| Emergency stop | centre 141, 36 | Ø60 |
| Lamp 1 — green | centre 30, 115.5 | Ø29 |
| Lamp 2 — yellow | centre 70, 115.5 | Ø29 |
| Button 3 — red, STOP | centre 110, 115.5 | Ø29 |
| Button 4 — green, START | centre 150, 115.5 | Ø29 |

Page is 186 × 156 mm: the 180 × 150 face plus 3 mm of bleed on every side. Cyan
crop marks sit at the four trim corners to show where the outer edge is cut.

### The one correction to the drawing

The drawing writes 15 mm margins beside the bottom row, but the row then adds up
to 179 mm:

```
15 + 29 + 11 + 29 + 11 + 29 + 11 + 29 + 15 = 179
```

The holes are drilled on a 40 mm pitch, so the pitch is the dimension that
exists in metal and the margin is the one that was rounded. The decal centres
the row, which puts the margin at 15.5 mm and the four centres on 30, 70, 110
and 150 — round numbers that are easy to check against the real panel with a
rule.

Everything else on the drawing cross-checks exactly: the 6 mm gap between the
display window and the Ø60 circle, the 6 mm from the circle to the top edge, and
the 9 mm from the circle to the right edge all fall out of the stated positions.

### Holes are cut by hand

Each of the six openings prints as a solid black shape at the size in the table
above, 0.5 mm oversize. There is no cut path: the holes are cut by hand once the
decal is on. Stick it to the panel, then cut from behind with a knife, following
the panel's own openings. The panel does the measuring, so the holes cannot come
out in the wrong place, and the black around a 22 mm panel opening is covered by
the bezel or the yellow collar.

The black shapes are drawn into the artwork rather than knocked out of it, so
they print black whatever the printer puts behind a transparent area. Nothing
else is drawn within `KEEPOUT` (2.3 mm) of a hole edge, so cutting never goes
through text.

An earlier version also shipped a cut file for a vinyl cutter. It was dropped
because the decal is cut by hand.

The Ø60 figure is the outer diameter of the yellow emergency-stop collar, not the
bore. The decal clears the collar and tucks under nothing.

## Labels

The decal reads top to bottom as who made it, how to drive it, and what it is.

**Header, top left:** the two logos, a red divider, then **วิทยาลัยการอาชีพฝาง**
over **แผนกวิชาช่างอิเล็กทรอนิกส์**.

**Footer, bottom left:** the machine's name,
**เครื่องช่วยบริหารกล้ามเนื้อต้นขาอัตโนมัติสมาร์ทแอปพลิเคชัน**, on one line behind a short red
bar. It is spelled exactly as supplied. The bottom
band runs the full 172 mm between the safe margins, so the whole name fits on a
single line there; up top it had to be broken in two to clear the Ø60 cutout.

There is no English subtitle. The old one read `PT LEG TRACKER · แผงควบคุม ·
CONTROL PANEL`, which said the same thing twice and duplicated a name the Thai
title already carries; cutting it is what paid for the larger title. `DISPLAY`
and the `PT-LEG-01` model code went for the same reason.

| Position | Thai | English |
| --- | --- | --- |
| Beside the window, reading upward | จอแสดงผล | — |
| Under the Ø60 circle | หยุดฉุกเฉิน | — |
| Lamp 1 | กำลังทำงาน | RUNNING |
| Lamp 2 | ว่าง | IDLE |
| Button 3 | หยุด | STOP |
| Button 4 | เริ่ม | START |

จอแสดงผล sits 2.3 mm outside the window's keep-out margin (about 4.1 mm from the
cut edge) and is centred on the window's
height, so it reads as that cutout's own label rather than something floating in
the margin. Rotated to read upward, the bottoms of the letters face the window —
which is why `STRIP_GAP` is measured from the descent line.

Lamp meanings come from `firmware/pt-leg-controller/config.h`: green alone means
a session is running and the actuator may move, yellow alone means idle, and both
lit means a latched fault.

Width checks use `KANIT_EM = 0.58`, the average advance of one Kanit glyph in em.
That figure was measured, not guessed: the full machine name in Bold at 6.4 mm
rendered 157.7 mm wide over 43 advancing glyphs, which is 0.573 em. The earlier
assumption of 0.5 under-read every width by about 15%, and it let the machine name
through when it was nearly touching the corner bracket. If a font or weight
changes, re-measure before trusting the asserts.

Type is Kanit 400/600/700, Thai and Latin subsets embedded as base64 woff2 inside
the SVG. A print shop with no Thai font installed still gets the right glyphs, and
nothing needs to be converted to outlines first. The woff2 files are vendored in
`fonts/` rather than read from the app's `node_modules`, which is not always
installed, or from `dist/`, where Vite renames them on every build.

## Logos

Two marks sit in the header, top left, each sized from its own pixel dimensions so
neither is squashed:

| File | Source |
| --- | --- |
| `logos/inventor.png` | `docs/inventor_logo.png` — Junior Inventor VEC Thailand |
| `logos/college.png` | `docs/Gemini_Generated_Image_…-removebg-preview.png` — the college crest |

The project's own mark (`assets/Logo-removebg.png`) was here and has been dropped.
It is a wide lockup whose two lines of Thai turned to mush at this size, and the
words in it — the machine name and the college name — are already set in type
elsewhere on the decal.

Both files are dark ink on transparency. Dropped straight onto the near-black
laminate they would be invisible. An earlier version set them on a white plate,
which made them legible but read as a pasted box — as if the background had never
been removed. Instead each logo carries a thin `#f2f4f8` contour that hugs its
own silhouette, the way a die-cut sticker does: legible on the dark ground, and
plainly cut out. The contour is baked into the PNG, so nothing in the SVG draws
it.

Each copy is trimmed against its alpha channel, resampled to 400 px tall — about
600 dpi at the 17 mm the header prints at — and then given the contour. The
inventor original is only 274 px tall, so it is upsampled; that is the one place
where a better source file would show.

`LOGO_FILES` carries a per-logo scale alongside each filename. Matching raw
heights does not match apparent size: the inventor logo is a full circle and the
crest is a tall emblem, so the circle is held to 0.92 to make the pair look even.

Regenerate a copy after changing a source. The first `-border` is what makes
`-trim` actually see the transparent margin; the second gives the dilated contour
room so it is not clipped at the edge; `Disk:9` sets the contour's thickness:

```bash
magick docs/inventor_logo.png -alpha set -bordercolor none -border 1 -trim +repage -filter Lanczos -resize x400 -bordercolor none -border 16 \( +clone -channel A -morphology Dilate Disk:9 +channel -fill '#f2f4f8' -colorize 100 \) +swap -compose over -composite -trim +repage -strip docs/Design/sticker/logos/inventor.png
```

## Background

`BACKGROUND` at the top of the generator picks between two patterns, and
`BACKGROUND_OPACITY` carries the opacity each one needs:

- **`"classic"`** (current) — `traces()`. Loose diagonal routing with scattered
  vias, at 80%.
- **`"pcb"`** — `pcb_art()`. Traces run as parallel bundles sharing the same
  corners, every turn is exactly 45°, widths come from two classes rather than at
  random, and each via sits on the end of a trace instead of floating. Three land
  patterns give the routing somewhere to arrive, each placed so a cutout eats most
  of it — a board really does carry on underneath its components, and it keeps
  pads off the labels. This one carries far more ink per square millimetre, so it
  runs at 50%; any higher and it competes with the Thai.

## Corner bolts

The box face is held on by a bolt near each corner. `BOLTS` places them at
(10, 10), (170, 10), (10, 140) and (170, 140) mm. Those positions were read off
the photos, not measured, so they are good to about ±2 mm. They are treated as
keep-out zones only: nothing is drawn within `BOLT_KEEP` (8 mm) of a centre, and
`clear_of_bolts()` checks every logo, the header text and the device name
against all four. The decal is not cut for the bolts; take them out, apply the
decal, and push them back through the vinyl.

The header starts at `CLEAR_X` (18.5 mm), just past the top-left bolt.

The bottom row is held tighter: `BOTTOM_CLEAR` keeps 24 mm empty at each side
edge, red bar included, so nothing sits in the way of the two bottom bolts. That
leaves 132 mm for the device name, which now sets at 5.0 mm. On the render the
bar starts at 24.0 mm and the text ends at 153.6 mm, a right margin of 26.4 mm.
If the bolts are ever measured, update `BOLTS` and rerun; the asserts will say
what no longer fits.

## Still open

The display module is held on by four screws just outside the window corners, and
the drawing does not dimension them. The decal has no holes for them. Either peel
the module off, apply the decal, and pierce the four screw points by hand, or
measure the screw centres and add them to `make-panel-sticker.py` as a fifth
cutout group.

## Printing

Print `panel-sticker.pdf` at 100% scale, not "fit to page", or the 180 × 150 mm
face will not match the panel. Trim the outer edge on the crop marks, then cut
the holes as described under "Holes are cut by hand".

Ask for white vinyl with a matte laminate, printed CMYK. Matte matters: the panel
sits under room lighting at an angle and a gloss laminate will wash out the dark
background.
