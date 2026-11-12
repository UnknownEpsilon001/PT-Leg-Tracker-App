# -*- coding: utf-8 -*-
"""สร้างไฟล์สติกเกอร์หน้าปัดกล่องควบคุม ขนาดเท่าจริง 1:1 (หน่วยมิลลิเมตร)

Generates the front-face control-panel decal as a 1:1 mm SVG.
Every cutout position comes from the panel drawing in ../Of2XulPK.jpg and is
re-checked by assertions below, because a sticker whose holes are off by a
millimetre is scrap.

Run:  python make-panel-sticker.py
Out:  panel-sticker.svg  (artwork + cut layer)
"""

import base64
import math
import pathlib
import random
import struct

HERE = pathlib.Path(__file__).resolve().parent
# Kanit is vendored here rather than read out of the app's node_modules: that
# directory is not always installed, and the built copies in dist/ carry Vite
# hashes that change on every rebuild. A decal that has to be reprintable years
# from now should not depend on either.
# [TH] เก็บฟอนต์ Kanit ไว้ในโฟลเดอร์นี้เอง ไม่ดึงจาก node_modules ของแอป
#      เพราะบางครั้งไม่ได้ติดตั้งไว้ และไฟล์ใน dist/ มีแฮชที่เปลี่ยนทุกครั้งที่ build
FONT_DIR = HERE / "fonts"

# ---------------------------------------------------------------------------
# geometry, straight off the drawing (mm)
# ขนาดทั้งหมดอ้างอิงแบบร่างแผงหน้าตู้ หน่วยมิลลิเมตร
# ---------------------------------------------------------------------------
FACE_W, FACE_H = 180.0, 150.0
BLEED = 3.0           # ระยะเผื่อตัด
SAFE = 4.0            # keep text this far inside the trim line

# display window — 70 x 51, 35 from the left edge, 26 from the top
WIN_X, WIN_Y, WIN_W, WIN_H = 35.0, 26.0, 70.0, 51.0
WIN_R = 2.0           # corner radius on the cut, so the vinyl cannot tear

# emergency stop — the drawing's 60 mm is the YELLOW COLLAR outer diameter,
# confirmed against the photos, so the decal is cut to clear the collar.
ESTOP_D = 60.0
ESTOP_FROM_TOP = 6.0
ESTOP_FROM_RIGHT = 9.0

# four lamps/buttons along the bottom — 29 mm each, 11 mm apart, 20 mm up
BTN_D = 29.0
BTN_GAP = 11.0
BTN_FROM_BOTTOM = 20.0
BTN_N = 4

# The drawing writes 15 mm margins on both sides, but
#   15 + 29 + 11 + 29 + 11 + 29 + 11 + 29 + 15 = 179, not 180.
# The holes are already drilled on 40 mm pitch, so the pitch is what is real and
# the margin is what was rounded. Centre the row and the margin lands on 15.5.
# [TH] แบบเขียนขอบข้างละ 15 มม. แต่รวมได้ 179 ไม่ใช่ 180
#      ระยะห่างรูจริงคือ 40 มม. จึงยึดระยะห่าง แล้วจัดกึ่งกลาง ขอบจึงเป็น 15.5
BTN_PITCH = BTN_D + BTN_GAP
BTN_MARGIN = (FACE_W - (BTN_N * BTN_D + (BTN_N - 1) * BTN_GAP)) / 2.0

# cut tolerances — the decal clears each hole so it can never overhang the bore
CLR = 0.5             # เผื่อรอบรู (ด้านละ 0.5 มม.)
# Keep-out margin around every cutout: nothing — text, logo, rule — is drawn
# closer than this to a nominal hole edge. It is the footprint the coloured
# outline rings used to occupy (0.5 clearance + 1.2 gap + 0.6 stroke). The rings
# are gone; the margin stays at their size so no label on the decal moves.
# [TH] ระยะกันรอบรูเจาะทุกรู ห้ามวางสิ่งใดใกล้ขอบรูกว่านี้
#      เท่ากับพื้นที่ที่วงแหวนสีเคยใช้ เอาวงแหวนออกแล้วแต่คงระยะไว้ ป้ายต่างๆ จึงไม่ขยับ
KEEPOUT = CLR + 1.8

# Corner bolts on the box face, one near each corner. These positions are read
# off the photos, not measured — good to about ±2 mm — so they are keep-out zones
# only and the decal is not cut for them. Nothing is drawn within BOLT_KEEP of a
# centre: a head of about 10 mm across, plus the estimate's error.
# [TH] น็อตยึดหน้ากล่องสี่มุม ตำแหน่งประมาณจากรูปถ่าย ไม่ได้วัดจริง (คลาดได้ราว ±2 มม.)
#      จึงใช้เป็นเขตห้ามวางเท่านั้น ไม่ได้เจาะรูให้ ห้ามวางสิ่งใดใกล้จุดศูนย์กลางน็อตกว่า BOLT_KEEP
BOLTS = [(10.0, 10.0), (170.0, 10.0), (10.0, 140.0), (170.0, 140.0)]
BOLT_KEEP = 8.0
# Left edge of the header and the device name, just past the left-hand bolts.
# Both start here, so the two blocks share one column.
CLEAR_X = BOLTS[0][0] + BOLT_KEEP + 0.5

# derived centres
ESTOP_R = ESTOP_D / 2.0
ESTOP_CX = FACE_W - ESTOP_FROM_RIGHT - ESTOP_R
ESTOP_CY = ESTOP_FROM_TOP + ESTOP_R
BTN_R = BTN_D / 2.0
BTN_CY = FACE_H - BTN_FROM_BOTTOM - BTN_R
BTN_CX = [BTN_MARGIN + BTN_R + i * BTN_PITCH for i in range(BTN_N)]

# ---------------------------------------------------------------------------
# palette — dark + red + electronics
# ---------------------------------------------------------------------------
BG0, BG1 = "#0a0b0e", "#14171d"
TRACE, TRACE_RED = "#28303b", "#511823"
VIA, PAD, SILK = "#44505f", "#5a4e33", "#2c333d"

# Which background to draw. "classic" is the loose diagonal routing; "pcb" is the
# stricter bundled version with land patterns. Each wants its own opacity, since
# the bundles carry far more ink per square millimetre than the loose traces do.
# [TH] เลือกลายพื้นหลัง classic คือเส้นทแยงแบบเดิม pcb คือเส้นเดินเป็นชุดพร้อมขาชิป
#      แต่ละแบบใช้ความทึบต่างกัน เพราะแบบเป็นชุดมีเส้นหนาแน่นกว่ามาก
BACKGROUND = "classic"
BACKGROUND_OPACITY = {"classic": 0.8, "pcb": 0.5}
RED, RED_DIM = "#ff2438", "#c2142a"
# The secondary greys are pitched light. On a near-black laminate under room
# lighting anything below roughly #90 goes muddy, which a screen preview hides.
# [TH] สีเทารองตั้งไว้ค่อนข้างสว่าง บนพื้นดำด้านใต้แสงห้องสีที่เข้มกว่านี้จะจมหาย
WHITE, GREY, DIM = "#f3f5f8", "#b4bcc7", "#9aa3ae"
GREEN, AMBER = "#2fd27a", "#f5b31b"

# ---------------------------------------------------------------------------
# verification — every one of these failing means a sticker that does not fit
# ---------------------------------------------------------------------------
# Thai marks that stack instead of advancing the pen — they cost no width.
# [TH] สระบนล่างและวรรณยุกต์ไม่กินความกว้าง จึงไม่นับตอนประมาณความยาวบรรทัด
THAI_COMBINING = set("ั") | {chr(c) for c in range(0x0E34, 0x0E3B)} | \
                 {chr(c) for c in range(0x0E47, 0x0E4F)}


# Average advance of one Kanit glyph, in em. Measured off a 300 dpi render of the
# full device name in Bold: 43 advancing glyphs at 6.4 mm came out 157.7 mm wide,
# which is 0.573 em. An earlier guess of 0.5 under-read widths by 15% and let
# lines through that were nearly touching the corner bracket. Rounded up so the
# asserts err towards rejecting.
# [TH] ความกว้างเฉลี่ยต่อตัวอักษรของ Kanit วัดจากภาพจริงที่ 300 dpi ได้ 0.573 em
#      ค่าเดิม 0.5 ประมาณต่ำไป 15% จึงปัดขึ้นเป็น 0.58 ให้การตรวจเข้มไว้ก่อน
KANIT_EM = 0.58


def advance_width(s, size):
    """ประมาณความกว้างข้อความ — advancing glyphs times the measured Kanit em."""
    return sum(1 for ch in s if ch not in THAI_COMBINING) * KANIT_EM * size


def png_size(path):
    """อ่านขนาดภาพจากหัวไฟล์ PNG — straight out of the IHDR, no image library."""
    head = path.read_bytes()[:24]
    assert head[:8] == b"\x89PNG\r\n\x1a\n", f"not a PNG: {path}"
    return struct.unpack(">II", head[16:24])


def header_logos():
    """Lay the two logos out across the top-left from their real pixel
    dimensions, so neither is squashed into a guessed box, centred on one line
    rather than hung from the top. Returns the placements and the x at which the
    college name beside them may start.
    [TH] วางโลโก้สองอันมุมซ้ายบนตามขนาดพิกเซลจริง จัดกึ่งกลางแนวเดียวกัน
         แล้วคืนตำแหน่งที่ชื่อวิทยาลัยข้างๆ เริ่มได้
    """
    placements, x = [], CLEAR_X
    mid = HEAD_Y + HEAD_LOGO_H / 2
    for name, scale in LOGO_FILES:
        px_w, px_h = png_size(LOGO_DIR / name)
        h = HEAD_LOGO_H * scale
        w = h * px_w / px_h
        placements.append((name, x, mid - h / 2, w, h))
        x += w + LOGO_GAP
    return placements, x - LOGO_GAP + HEAD_TEXT_GAP


def clear_of_bolts(x0, y0, x1, y1):
    """True when the box (x0, y0)-(x1, y1) stays at least BOLT_KEEP from every
    corner bolt centre — the nearest point of the box, not its middle."""
    for bx, by in BOLTS:
        dx = max(x0 - bx, 0.0, bx - x1)
        dy = max(y0 - by, 0.0, by - y1)
        if math.hypot(dx, dy) < BOLT_KEEP:
            return False
    return True


def check():
    # the corrected row fills the face exactly
    assert abs(2 * BTN_MARGIN + BTN_N * BTN_D + (BTN_N - 1) * BTN_GAP - FACE_W) < 1e-9
    assert abs(BTN_MARGIN - 15.5) < 1e-9, BTN_MARGIN
    assert BTN_CX == [30.0, 70.0, 110.0, 150.0], BTN_CX
    assert abs(BTN_CY - 115.5) < 1e-9

    # the drawing's own cross-dimensions must fall out of the centres
    assert abs(ESTOP_CX - 141.0) < 1e-9 and abs(ESTOP_CY - 36.0) < 1e-9
    assert abs((ESTOP_CX - ESTOP_R) - (WIN_X + WIN_W) - 6.0) < 1e-9, "6 mm window->estop gap"
    assert abs((ESTOP_CY - ESTOP_R) - 6.0) < 1e-9, "6 mm estop->top gap"
    assert abs(FACE_W - (ESTOP_CX + ESTOP_R) - 9.0) < 1e-9, "9 mm estop->right gap"

    # every inked cutout, ring included, stays on the face
    assert WIN_X - KEEPOUT > 0 and WIN_Y - KEEPOUT > 0
    assert WIN_X + WIN_W + KEEPOUT < FACE_W and WIN_Y + WIN_H + KEEPOUT < FACE_H
    assert ESTOP_CY - ESTOP_R - KEEPOUT > 0
    assert ESTOP_CX + ESTOP_R + KEEPOUT < FACE_W
    for cx in BTN_CX:
        assert cx - BTN_R - KEEPOUT > 0 and cx + BTN_R + KEEPOUT < FACE_W
    assert BTN_CY + BTN_R + KEEPOUT < FACE_H

    # no two cutouts (with their keep-out margins) touch
    for a, b in zip(BTN_CX, BTN_CX[1:]):
        assert b - a > BTN_D + 2 * KEEPOUT, "button keep-out zones overlap"
    win_keepout_right = WIN_X + WIN_W + KEEPOUT
    assert ESTOP_CX - ESTOP_R - KEEPOUT > win_keepout_right, "e-stop and window keep-out zones overlap"
    for cx in BTN_CX:
        # vertical clearance from the window and the e-stop down to the buttons
        assert BTN_CY - BTN_R - KEEPOUT > WIN_Y + WIN_H + KEEPOUT
        d = math.hypot(cx - ESTOP_CX, BTN_CY - ESTOP_CY)
        assert d > BTN_R + ESTOP_R + 2 * KEEPOUT, "e-stop and a button keep-out zone overlap"

    # header, top left: two logos, then college name over department. Everything
    # stays inside the top band, and the text stops short of the window ring's
    # right edge — the Ø60 ring bulges furthest left lower down, so that edge is
    # the tighter of the two limits.
    placements, text_x = header_logos()
    assert len(placements) == len(LOGO_FILES)
    for _, _, iy, _, ih in placements:
        assert iy >= SAFE - 0.5, "a header logo rides up into the top edge"
        assert iy + ih <= BANDS["top"][1], "a header logo drops into the window ring"
    assert NAME_Y - NAME_SIZE * ASC >= SAFE
    assert DEPT_Y - DEPT_SIZE * ASC >= NAME_Y + NAME_SIZE * DESC
    assert DEPT_Y + DEPT_SIZE * DESC <= BANDS["top"][1]
    head_right = text_x + max(advance_width(NAME, NAME_SIZE),
                              advance_width(DEPT, DEPT_SIZE))
    assert head_right <= WIN_X + WIN_W + KEEPOUT, "header text runs into the e-stop keep-out"
    for name, ix, iy, iw, ih in placements:
        assert clear_of_bolts(ix, iy, ix + iw, iy + ih), f"{name} sits on a corner bolt"
    assert clear_of_bolts(text_x - 2.4, NAME_Y - NAME_SIZE * ASC,
                          head_right, DEPT_Y + DEPT_SIZE * DESC), "header text sits on a corner bolt"

    # emergency-stop caption: fits its band, and stays between the window ring
    # and the right safe edge once centred under the Ø60 cutout
    assert ESTOP_Y - ESTOP_SIZE * ASC >= BANDS["estop"][0]
    assert ESTOP_Y + ESTOP_SIZE * DESC <= BANDS["estop"][1]
    estop_room = 2 * min(ESTOP_CX - (WIN_X + WIN_W + KEEPOUT), FACE_W - SAFE - ESTOP_CX)
    assert advance_width(ESTOP_LABEL, ESTOP_SIZE) <= estop_room, "e-stop caption too wide"

    # column labels: Thai over English over the tick, all inside the band, and
    # each column narrower than the pitch so neighbours never touch
    assert LABEL_TH_Y - LABEL_TH * ASC >= BANDS["labels"][0]
    assert LABEL_EN_Y - LABEL_EN * ASC >= LABEL_TH_Y + LABEL_TH * DESC
    assert LABEL_EN_Y + LABEL_EN * DESC <= LABEL_TICK_Y
    assert LABEL_TICK_Y + 0.7 <= BANDS["labels"][1]
    for th, en, _ in COLUMNS:
        assert advance_width(th, LABEL_TH) <= BTN_PITCH - 2, f"column label too wide: {th}"
        assert advance_width(en, LABEL_EN) <= BTN_PITCH - 2, f"column label too wide: {en}"

    # bottom edge: the device's own name on one line, clear of the corner bracket
    assert DEVICE_Y - DEVICE_SIZE * ASC >= BANDS["bottom"][0]
    assert DEVICE_Y + DEVICE_SIZE * DESC <= BANDS["bottom"][1]
    device_right = DEVICE_X + advance_width(DEVICE_NAME, DEVICE_SIZE)
    bracket_left = FACE_W - SAFE / 2 - BRACKET_LEN
    assert device_right + 6 <= bracket_left, "device name runs into the corner bracket"
    assert device_right <= FACE_W - BOTTOM_CLEAR, "device name runs into the right 24 mm margin"
    assert clear_of_bolts(BOTTOM_CLEAR, DEVICE_Y - DEVICE_SIZE * ASC,
                          device_right, DEVICE_Y + DEVICE_SIZE * DESC), "device name sits on a corner bolt"

    # the three text bands must fit in the gaps the cutouts leave
    assert BANDS["top"][1] <= WIN_Y - KEEPOUT
    assert BANDS["labels"][0] >= WIN_Y + WIN_H + KEEPOUT
    assert BANDS["labels"][1] <= BTN_CY - BTN_R - KEEPOUT
    assert BANDS["estop"][0] >= ESTOP_CY + ESTOP_R + KEEPOUT
    assert BANDS["estop"][1] <= BANDS["labels"][0]
    assert BANDS["bottom"][0] >= BTN_CY + BTN_R + KEEPOUT
    assert BANDS["bottom"][1] <= FACE_H - SAFE
    # the left strip sits beside the window, clear of its ring
    assert STRIP_BASE_X + STRIP_TH * DESC <= WIN_X - KEEPOUT - STRIP_GAP + 1e-9
    assert STRIP_BASE_X - STRIP_TH * ASC >= SAFE, "จอแสดงผล runs off the left edge"
    assert advance_width("จอแสดงผล", STRIP_TH) <= WIN_H, "จอแสดงผล is taller than the window"


# ชื่อเครื่อง — along the bottom edge on a single line. The bottom band runs the
# full 172 mm between the safe margins, where the top band stops at the window
# ring, so the whole name fits in one line here and needs no break.
# [TH] ชื่อเครื่องอยู่ขอบล่างบรรทัดเดียว แถบล่างกว้างเต็ม 172 มม.
#      ส่วนแถบบนถูกวงแหวนจอบังอยู่ ชื่อเต็มจึงวางบรรทัดเดียวได้ที่ขอบล่างโดยไม่ต้องตัดคำ
DEVICE_NAME = "เครื่องช่วยบริหารกล้ามเนื้อต้นขาอัตโนมัติสมาร์ทแอปพลิเคชัน"
# The bottom row stays 24 mm clear of each side edge, red bar included, so the
# two bottom bolts have nothing in their way.
# [TH] แถวล่างเว้นห่างขอบซ้ายและขวาข้างละ 24 มม. รวมแถบสีแดง ให้น็อตสองตัวล่างไม่มีอะไรขวาง
BOTTOM_CLEAR = 24.0
DEVICE_SIZE, DEVICE_Y = 5.0, 141.3
DEVICE_X = BOTTOM_CLEAR + 4.0      # leaves room for the red bar in front of it

# corner brackets, top left and bottom right
BRACKET_LEN = 11.0

# How far Kanit's Thai reaches above and below the baseline, as a fraction of the
# font size. The ascender figure covers a stacked vowel plus tone mark.
ASC, DESC = 0.98, 0.28

# No English here. The yellow collar around the Ø60 knob already carries
# "EMERGENCY STOP" moulded into it, so dropping the words buys the Thai another
# two millimetres of type on the one label that most needs to be read fast.
# [TH] ไม่ใส่ภาษาอังกฤษ เพราะวงแหวนเหลืองรอบปุ่มมีคำว่า EMERGENCY STOP อยู่แล้ว
#      พื้นที่ที่ได้คืนเอาไปขยายตัวอักษรไทยของป้ายที่ต้องอ่านให้ทันที่สุด
ESTOP_LABEL = "หยุดฉุกเฉิน"
NAME = "วิทยาลัยการอาชีพฝาง"
DEPT = "แผนกวิชาช่างอิเล็กทรอนิกส์"

# Header, top left. Both logo files are dark ink on transparency, so on a
# near-black laminate they would vanish. Rather than set them on a plate — which
# reads as a pasted box, as if the background were never removed — each carries
# a thin light contour hugging its own silhouette, the way a die-cut sticker
# does. The contour is baked into the PNGs in logos/; see the README for the
# command.
# [TH] หัวสติกเกอร์มุมซ้ายบน โลโก้ทั้งสองเป็นลายสีเข้มบนพื้นโปร่งใส วางบนพื้นดำจะจมหาย
#      แทนที่จะวางบนแผ่นพื้นขาว (ซึ่งดูเหมือนยังไม่ได้ลบพื้นหลัง)
#      จึงใส่เส้นขอบสีอ่อนบางๆ ตามรูปทรงของโลโก้เอง แบบสติกเกอร์ไดคัท
LOGO_DIR = HERE / "logos"

# Per-logo scale, because matching raw heights does not match apparent size: a
# full circle reads larger than a tall emblem of the same height, so the circle
# is held back a little.
# [TH] ปรับขนาดรายโลโก้ เพราะสูงเท่ากันไม่ได้แปลว่าดูใหญ่เท่ากัน
#      โลโก้ทรงกลมเต็มวงจะดูใหญ่กว่าตราที่สูงเรียว จึงย่อวงกลมลงเล็กน้อย
LOGO_FILES = [("inventor.png", 0.92), ("college.png", 1.0)]
HEAD_Y = 4.5                       # top of the tallest logo
HEAD_LOGO_H = 17.0
LOGO_GAP = 3.0
HEAD_TEXT_GAP = 4.5                # logos to the red divider and name

# Type sizes for the rest of the decal. These are read at arm's length on a
# machine, not on a page, so Thai sits a good deal larger than it would in print
# — the vowel and tone marks are what fail first when the type is too small.
# [TH] ขนาดตัวอักษรส่วนที่เหลือ อ่านระยะยืนใช้งานจริง จึงต้องใหญ่กว่างานพิมพ์ทั่วไป
#      สระและวรรณยุกต์คือส่วนที่อ่านไม่ออกก่อนเพื่อนเมื่อตัวเล็กเกินไป
STRIP_TH = 6.5                     # จอแสดงผล
ESTOP_SIZE, ESTOP_Y = 6.0, 76.6    # หยุดฉุกเฉิน
LABEL_TH, LABEL_TH_Y = 6.0, 86.0   # กำลังทำงาน / ว่าง / หยุด / เริ่ม
LABEL_EN, LABEL_EN_Y = 3.2, 91.4
LABEL_TICK_Y = 94.0
NAME_SIZE, NAME_Y = 5.6, 12.0     # วิทยาลัยการอาชีพฝาง, beside the logos
DEPT_SIZE, DEPT_Y = 4.4, 19.4     # แผนกวิชาช่างอิเล็กทรอนิกส์

# free bands between the cutouts, as (top, bottom) in mm
BANDS = {
    "top": (SAFE, 23.5),
    "estop": (68.5, 79.0),
    "labels": (79.5, 98.5),
    "bottom": (132.5, FACE_H - SAFE),
}
# จอแสดงผล runs bottom-to-top beside the window. Rotated that way the glyph
# bottoms face the window, so its descent is what sets the gap to the ring.
# Centred on the window's height so it reads as that cutout's label.
# [TH] ป้ายจอแสดงผลวางแนวตั้งชิดจอ หมุนแล้วฐานตัวอักษรหันเข้าหาจอ
#      ระยะห่างจากวงแหวนจึงวัดจากส่วนล่างของตัวอักษร จัดกึ่งกลางตามความสูงของจอ
# Measured from the descent line, which จอแสดงผล never reaches — it has no
# below-baseline marks — so the gap the eye sees is this plus the unused descent,
# about 2.4 mm.
STRIP_GAP = 0.6
STRIP_BASE_X = WIN_X - KEEPOUT - STRIP_GAP - STRIP_TH * DESC
STRIP_MID_Y = WIN_Y + WIN_H / 2


# ---------------------------------------------------------------------------
# fonts — embedded so a print shop with no Thai font still renders it right
# [TH] ฝังฟอนต์ไว้ในไฟล์ ร้านพิมพ์ที่ไม่มีฟอนต์ไทยก็ยังได้ตัวอักษรถูกต้อง
# ---------------------------------------------------------------------------
THAI_RANGE = "U+0E01-0E5B"
LATIN_RANGE = ("U+0000-00FF,U+0131,U+0152-0153,U+02BB-02BC,U+02C6,U+02DA,U+02DC,"
               "U+2000-206F,U+2074,U+20AC,U+2122,U+2191,U+2193,U+2212,U+2215,U+FEFF,U+FFFD")


def font_faces():
    out = []
    for weight in (400, 600, 700):
        for subset, rng in (("thai", THAI_RANGE), ("latin", LATIN_RANGE)):
            path = FONT_DIR / f"kanit-{subset}-{weight}-normal.woff2"
            b64 = base64.b64encode(path.read_bytes()).decode("ascii")
            out.append(
                "@font-face{font-family:'Kanit';font-style:normal;"
                f"font-weight:{weight};unicode-range:{rng};"
                f"src:url(data:font/woff2;base64,{b64}) format('woff2');}}"
            )
    return "\n".join(out)


# ---------------------------------------------------------------------------
# drawing helpers
# ---------------------------------------------------------------------------
def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def text(x, y, s, size, fill, weight=600, anchor="start", ls=0, extra=""):
    sp = f' letter-spacing="{ls}"' if ls else ""
    return (f'<text x="{x:.3f}" y="{y:.3f}" font-family="Kanit" font-size="{size}" '
            f'font-weight="{weight}" fill="{fill}" text-anchor="{anchor}"{sp}{extra}>'
            f'{esc(s)}</text>')


def traces():
    """วาดลายวงจรจางๆ เป็นพื้นหลัง — diagonal PCB routing, drawn under everything."""
    rnd = random.Random(1309)
    parts = []
    y = -BLEED
    while y < FACE_H + BLEED:
        x0 = -BLEED
        run = rnd.uniform(14, 30)
        seg = [f"M{x0:.2f},{y:.2f}"]
        x, yy = x0, y
        while x < FACE_W + BLEED:
            x2 = x + run
            parts_dir = rnd.choice((-1, 1))
            dy = rnd.choice((0, 0, 6, 10)) * parts_dir
            if dy:
                seg.append(f"L{x2 - abs(dy):.2f},{yy:.2f} L{x2:.2f},{yy + dy:.2f}")
                yy += dy
            else:
                seg.append(f"L{x2:.2f},{yy:.2f}")
            x = x2
            run = rnd.uniform(14, 30)
        col = TRACE_RED if rnd.random() < 0.22 else TRACE
        parts.append(f'<path d="{" ".join(seg)}" fill="none" stroke="{col}" '
                     f'stroke-width="{rnd.choice((0.45, 0.6, 0.8))}" stroke-linejoin="round"/>')
        y += rnd.uniform(7, 13)

    # vias / pads
    for _ in range(46):
        vx = rnd.uniform(-BLEED, FACE_W + BLEED)
        vy = rnd.uniform(-BLEED, FACE_H + BLEED)
        parts.append(f'<circle cx="{vx:.2f}" cy="{vy:.2f}" r="1.0" fill="none" '
                     f'stroke="{TRACE}" stroke-width="0.45"/>')
    return "\n".join(parts)


def pcb_art():
    """ลายแผ่นวงจรพื้นหลัง — what makes a board look like a board, not a map:
    traces run as parallel bundles, every turn is exactly 45 degrees, widths come
    from two classes rather than at random, and vias sit ON a trace instead of
    floating loose. A couple of real footprints give the routing somewhere to go.
    [TH] เส้นลายวงจรเดินเป็นชุดขนาน หักมุม 45 องศาเท่านั้น ความกว้างมีสองระดับ
         via อยู่บนเส้นจริง ไม่ลอยเดี่ยวๆ และมีขาชิปให้เส้นวิ่งเข้าออก
    """
    rnd = random.Random(20260117)
    parts = []
    X0, X1 = -BLEED, FACE_W + BLEED

    def trace(y, jogs, width, col):
        """One trace running left to right. A jog is (x, dy); the 45 degrees
        comes from stepping back |dy| before dropping dy."""
        d, cy = [f"M{X0:.2f},{y:.2f}"], y
        for jx, dy in jogs:
            d.append(f"L{jx - abs(dy):.2f},{cy:.2f}")
            cy += dy
            d.append(f"L{jx:.2f},{cy:.2f}")
        d.append(f"L{X1:.2f},{cy:.2f}")
        return (f'<path d="{" ".join(d)}" fill="none" stroke="{col}" stroke-width="{width}" '
                f'stroke-linejoin="round" stroke-linecap="round"/>'), cy

    def via(x, y):
        # annular ring only — the hole is just background showing through
        return (f'<circle cx="{x:.2f}" cy="{y:.2f}" r="0.95" fill="none" '
                f'stroke="{VIA}" stroke-width="0.55"/>')

    def footprint(x, y, pins=6):
        """A SOIC-ish land pattern: two rows of pads inside a silkscreen box."""
        pitch, pw, ph, span = 1.9, 0.8, 2.1, 5.0
        w = (pins - 1) * pitch
        out = [f'<rect x="{x - 1.4:.2f}" y="{y - 1.4:.2f}" width="{w + 2.8:.2f}" '
               f'height="{span + ph + 2.8:.2f}" rx="0.5" fill="none" stroke="{SILK}" '
               f'stroke-width="0.3"/>']
        for i in range(pins):
            px = x + i * pitch - pw / 2
            out.append(f'<rect x="{px:.2f}" y="{y:.2f}" width="{pw}" height="{ph}" '
                       f'rx="0.2" fill="{PAD}"/>')
            out.append(f'<rect x="{px:.2f}" y="{y + span:.2f}" width="{pw}" '
                       f'height="{ph}" rx="0.2" fill="{PAD}"/>')
        return out

    # Seven bundles spread down the face. Each is 3-5 traces on 1.8 mm centres
    # sharing the same jogs, so the diagonals stay parallel through every corner.
    bundles = [
        (6.0, [(44, -8), (96, 6), (150, -6)]),
        (27.0, [(30, 6), (84, -10), (138, 6)]),
        (49.0, [(22, -6), (70, 8), (126, -8), (164, 6)]),
        (71.0, [(38, 8), (92, -6), (148, 8)]),
        (95.0, [(26, -8), (78, 6), (134, -6)]),
        (119.0, [(48, 6), (104, -8), (158, 6)]),
        (141.0, [(34, -6), (88, 8), (142, -8)]),
    ]
    for by, jogs in bundles:
        n = rnd.choice((3, 4, 5))
        power = rnd.random() < 0.3          # a fat pair instead of signal traces
        for i in range(n):
            col = TRACE_RED if power else TRACE
            width = 0.9 if power else 0.45
            path, end_y = trace(by + i * 1.8, jogs, width, col)
            parts.append(path)
            # one via where this trace settles after its last turn
            if i == n - 1:
                vx = jogs[-1][0] + rnd.uniform(8, 22)
                parts.append(via(vx, end_y))

    # Footprints are placed so a cutout eats most of each one. A board really
    # does continue underneath its components, and it keeps the pads from
    # landing on a label — which is where they read as clutter, not as a board.
    # [TH] วางขาชิปให้ถูกรูเจาะกินไปเกือบหมด เหมือนแผ่นวงจรจริงที่เดินลอดใต้ตัวอุปกรณ์
    #      และกันไม่ให้ไปทับป้ายข้อความ
    for fx, fy in ((120.0, 58.0), (46.0, 68.0), (24.0, 104.0)):
        parts.extend(footprint(fx, fy, pins=rnd.choice((5, 6, 8))))
    return "\n".join(parts)


def ring(shape):
    """วงแหวนสีแดงรอบรู — hides up to 1.2 mm of application misalignment."""
    return shape


def corner_bracket(x, y, sx, sy, ln=BRACKET_LEN, w=0.9):
    return (f'<path d="M{x + sx * ln:.2f},{y:.2f} L{x:.2f},{y:.2f} L{x:.2f},{y + sy * ln:.2f}" '
            f'fill="none" stroke="{RED}" stroke-width="{w}" stroke-linecap="square"/>')


# label columns, left to right, matching the hardware:
#   green lamp = running, yellow lamp = idle, red button = stop, green button = start
# [TH] เรียงตามของจริงบนหน้าปัด ไฟเขียว ไฟเหลือง ปุ่มแดง ปุ่มเขียว
COLUMNS = [
    ("กำลังทำงาน", "RUNNING", GREEN),
    ("ว่าง", "IDLE", AMBER),
    ("หยุด", "STOP", RED),
    ("เริ่ม", "START", GREEN),
]


def build():
    check()
    assert BACKGROUND in BACKGROUND_OPACITY, BACKGROUND
    art = traces() if BACKGROUND == "classic" else pcb_art()
    art_opacity = BACKGROUND_OPACITY[BACKGROUND]
    vb_x, vb_y = -BLEED, -BLEED
    vb_w, vb_h = FACE_W + 2 * BLEED, FACE_H + 2 * BLEED

    # Each opening prints as a solid black shape — no cut path. The holes are cut
    # by hand once the decal is on; the black marks where each one goes. Drawn in
    # the artwork rather than knocked out of it, so they print black whatever
    # the printer puts behind a transparent area.
    # [TH] ตำแหน่งรูพิมพ์เป็นสีดำทึบ ไม่มีเส้นตัด เจาะรูเองหลังติดสติกเกอร์
    #      วาดเป็นรูปดำในงานพิมพ์เลย ไม่ใช่เว้นโปร่งใส จึงพิมพ์ออกมาเป็นสีดำเสมอ
    holes = [
        f'<rect x="{WIN_X - CLR:.3f}" y="{WIN_Y - CLR:.3f}" width="{WIN_W + 2 * CLR:.3f}" '
        f'height="{WIN_H + 2 * CLR:.3f}" rx="{WIN_R}" fill="#000"/>',
        f'<circle cx="{ESTOP_CX:.3f}" cy="{ESTOP_CY:.3f}" r="{ESTOP_R + CLR:.3f}" fill="#000"/>',
    ] + [f'<circle cx="{cx:.3f}" cy="{BTN_CY:.3f}" r="{BTN_R + CLR:.3f}" fill="#000"/>'
         for cx in BTN_CX]

    # --- text ----------------------------------------------------------------
    t = []
    # header, above the display window: logos, a red divider, then who made it
    placements, text_x = header_logos()
    for name, ix, iy, iw, ih in placements:
        b64 = base64.b64encode((LOGO_DIR / name).read_bytes()).decode("ascii")
        t.append(f'<image x="{ix:.2f}" y="{iy:.2f}" width="{iw:.2f}" '
                 f'height="{ih:.2f}" preserveAspectRatio="xMidYMid meet" '
                 f'xlink:href="data:image/png;base64,{b64}"/>')
    rule_top = NAME_Y - NAME_SIZE * ASC
    rule_h = DEPT_Y + DEPT_SIZE * DESC - rule_top
    t.append(f'<rect x="{text_x - 2.4:.2f}" y="{rule_top:.2f}" width="0.7" '
             f'height="{rule_h:.2f}" fill="{RED}"/>')
    t.append(text(text_x, NAME_Y, NAME, NAME_SIZE, WHITE, 700))
    t.append(text(text_x, DEPT_Y, DEPT, DEPT_SIZE, GREY, 600))

    # display window label, in the strip to its left, reading bottom-to-top
    t.append(f'<g transform="translate({STRIP_BASE_X:.2f},{STRIP_MID_Y:.2f}) rotate(-90)">'
             + text(0, 0, "จอแสดงผล", STRIP_TH, WHITE, 600, anchor="middle") + '</g>')

    # emergency stop, under the Ø60 collar. No arrow glyph — at this size the
    # label sits directly under the knob and the ring already points at it.
    t.append(text(ESTOP_CX, ESTOP_Y, ESTOP_LABEL, ESTOP_SIZE, RED, 700,
                  anchor="middle", ls=0.2))

    # the four column labels
    for cx, (th, en, col) in zip(BTN_CX, COLUMNS):
        t.append(text(cx, LABEL_TH_Y, th, LABEL_TH, WHITE, 600, anchor="middle"))
        t.append(text(cx, LABEL_EN_Y, en, LABEL_EN, col, 700, anchor="middle", ls=0.5))
        t.append(f'<rect x="{cx - 6:.2f}" y="{LABEL_TICK_Y}" width="12" height="0.7" '
                 f'fill="{col}"/>')

    # bottom left: the device's own name, behind a red bar like the display strip
    bar_top = DEVICE_Y - DEVICE_SIZE * ASC
    bar_h = DEVICE_SIZE * (ASC + DESC)
    t.append(f'<rect x="{BOTTOM_CLEAR:.2f}" y="{bar_top:.2f}" width="1.6" '
             f'height="{bar_h:.2f}" fill="{RED}"/>')
    t.append(text(DEVICE_X, DEVICE_Y, DEVICE_NAME, DEVICE_SIZE, WHITE, 700))

    crop = []
    for cx, cy in ((0, 0), (FACE_W, 0), (0, FACE_H), (FACE_W, FACE_H)):
        dx = -1 if cx == 0 else 1
        dy = -1 if cy == 0 else 1
        crop.append(f'<path d="M{cx + dx * 1.2:.2f},{cy:.2f} L{cx + dx * BLEED:.2f},{cy:.2f} '
                    f'M{cx:.2f},{cy + dy * 1.2:.2f} L{cx:.2f},{cy + dy * BLEED:.2f}" '
                    f'stroke="#00a0ff" stroke-width="0.15" fill="none"/>')

    return f"""<?xml version="1.0" encoding="UTF-8"?>
<!-- LEO WAS HERE ;3 -->
<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" version="1.1"
     width="{vb_w}mm" height="{vb_h}mm"
     viewBox="{vb_x} {vb_y} {vb_w} {vb_h}">
<title>PT Leg Tracker — control panel decal 180 x 150 mm</title>
<desc>สติกเกอร์หน้าปัดกล่องควบคุม 180 x 150 มม. เผื่อตัด {BLEED} มม. ขนาด 1:1</desc>
<defs>
<style type="text/css"><![CDATA[
{font_faces()}
text {{ dominant-baseline: alphabetic; }}
]]></style>
<linearGradient id="bg" x1="0" y1="0" x2="0.35" y2="1">
  <stop offset="0" stop-color="{BG1}"/><stop offset="0.55" stop-color="{BG0}"/>
  <stop offset="1" stop-color="#0d0f13"/>
</linearGradient>
<radialGradient id="glow" cx="0.78" cy="0.2" r="0.6">
  <stop offset="0" stop-color="{RED_DIM}" stop-opacity="0.18"/>
  <stop offset="1" stop-color="{RED_DIM}" stop-opacity="0"/>
</radialGradient>
</defs>

<g id="artwork">
  <rect x="{vb_x}" y="{vb_y}" width="{vb_w}" height="{vb_h}" fill="url(#bg)"/>
  <g opacity="{art_opacity}">
{art}
  </g>
  <rect x="{vb_x}" y="{vb_y}" width="{vb_w}" height="{vb_h}" fill="url(#glow)"/>
  <rect x="{SAFE / 2}" y="{SAFE / 2}" width="{FACE_W - SAFE}" height="{FACE_H - SAFE}"
        rx="1.5" fill="none" stroke="#242a33" stroke-width="0.3"/>
  {corner_bracket(SAFE / 2, SAFE / 2, 1, 1)}
  {corner_bracket(FACE_W - SAFE / 2, FACE_H - SAFE / 2, -1, -1)}
  <g id="holes">
{chr(10).join("    " + h for h in holes)}
  </g>
{chr(10).join("  " + s for s in t)}
</g>

<g id="crop-marks">
{chr(10).join("  " + c for c in crop)}
</g>
</svg>
"""


if __name__ == "__main__":
    out = HERE / "panel-sticker.svg"
    out.write_text(build(), encoding="utf-8")
    print(f"ok  {out}")
    print(f"    face {FACE_W}x{FACE_H} mm, bleed {BLEED} mm, hole clearance {CLR} mm/side")
    print(f"    button centres x = {BTN_CX}, y = {BTN_CY}  (margin {BTN_MARGIN})")
    print(f"    e-stop centre  x = {ESTOP_CX}, y = {ESTOP_CY}, cut r = {ESTOP_R + CLR}")
