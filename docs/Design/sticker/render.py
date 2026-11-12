# -*- coding: utf-8 -*-
"""ส่งออกไฟล์ SVG เป็น PDF และ PNG ขนาดเท่าจริง ผ่าน Chrome headless.

Exports panel-sticker.svg to two files:
  panel-sticker.pdf          the decal for the printer, 186 x 156 mm with bleed
  panel-sticker-preview.png  300 dpi preview, for eyeballing before it goes out
The decal has no holes and no cut path: the holes are cut by hand after it is
applied. Chrome is used because it is the only renderer on this machine that
handles the embedded woff2 Thai font.
"""

import pathlib
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
SVG = HERE / "panel-sticker.svg"
WRAP = HERE / ".render.html"

PAGE_W, PAGE_H = 186.0, 156.0     # face 180x150 + 3 mm bleed all round
DPI = 300

CHROME = [
    r"C:\Program Files\Google\Chrome\Application\chrome.exe",
    r"C:\Program Files (x86)\Google\Chrome\Application\chrome.exe",
    r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
]


def chrome():
    for c in CHROME:
        if pathlib.Path(c).exists():
            return c
    sys.exit("no Chrome/Edge found")


def wrap(hide=(), bg="#000"):
    """Write the HTML page Chrome renders: the SVG inline, with the named layer
    ids hidden and `bg` behind it."""
    svg = SVG.read_text(encoding="utf-8")
    svg = svg.split("?>", 1)[1]           # drop the XML declaration for inline use
    css_hide = (",".join(f"#{i}" for i in hide) + "{display:none}") if hide else ""
    WRAP.write_text(
        "<!doctype html><meta charset='utf-8'>"
        f"<style>@page{{size:{PAGE_W}mm {PAGE_H}mm;margin:0}}"
        f"html,body{{margin:0;padding:0;background:{bg}}}"
        f"svg{{display:block;width:{PAGE_W}mm;height:{PAGE_H}mm}}{css_hide}</style>" + svg,
        encoding="utf-8",
    )


def pdf(name):
    run([f"--print-to-pdf={HERE / name}", "--no-pdf-header-footer",
         "--virtual-time-budget=4000", WRAP.as_uri()])


def run(args):
    subprocess.run(
        [chrome(), "--headless=new", "--disable-gpu", "--no-sandbox",
         "--force-device-scale-factor=1", "--hide-scrollbars"] + args,
        check=True, capture_output=True,
    )


if __name__ == "__main__":
    url = WRAP.as_uri()

    # The crop marks stay on the print: they sit in the bleed and show where to
    # trim the outer edge, then are cut away with it.
    # [TH] เครื่องหมายตัดขอบอยู่ในส่วนเผื่อตัด บอกตำแหน่งตัดขอบนอก แล้วถูกตัดทิ้งไปด้วย
    wrap()
    pdf("panel-sticker.pdf")

    # CSS px, not device px: the window is sized in CSS units and the scale
    # factor is what turns 96 dpi into 300.
    css_w = round(PAGE_W / 25.4 * 96)
    css_h = round(PAGE_H / 25.4 * 96)
    scale = DPI / 96
    wrap(hide=("crop-marks",))
    run([f"--screenshot={HERE / 'panel-sticker-preview.png'}",
         f"--window-size={css_w},{css_h}",
         f"--force-device-scale-factor={scale:.6f}",
         "--virtual-time-budget=4000", url])
    WRAP.unlink()
    print(f"pdf  {PAGE_W} x {PAGE_H} mm  (art + crop marks, no holes)")
    print(f"png  {round(css_w * scale)} x {round(css_h * scale)} px @ {DPI} dpi  (art only)")
