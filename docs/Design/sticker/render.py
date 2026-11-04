# -*- coding: utf-8 -*-
"""ส่งออกไฟล์ SVG เป็น PDF และ PNG ขนาดเท่าจริง ผ่าน Chrome headless.

Exports panel-sticker.svg to a 1:1 PDF (for the print shop) and a 300 dpi PNG
(for eyeballing before it goes out). Chrome is used because it is the only
renderer on this machine that handles the embedded woff2 Thai font.
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


def wrap(show_cut=True):
    svg = SVG.read_text(encoding="utf-8")
    svg = svg.split("?>", 1)[1]           # drop the XML declaration for inline use
    # The cut layer sits exactly on top of the accent rings, so the art preview
    # hides it — the cut geometry is proven by the assertions in the generator.
    hide = "" if show_cut else "#cut-lines,#crop-marks{display:none}"
    WRAP.write_text(
        "<!doctype html><meta charset='utf-8'>"
        f"<style>@page{{size:{PAGE_W}mm {PAGE_H}mm;margin:0}}"
        "html,body{margin:0;padding:0;background:#000}"
        f"svg{{display:block;width:{PAGE_W}mm;height:{PAGE_H}mm}}{hide}</style>" + svg,
        encoding="utf-8",
    )


def run(args):
    subprocess.run(
        [chrome(), "--headless=new", "--disable-gpu", "--no-sandbox",
         "--force-device-scale-factor=1", "--hide-scrollbars"] + args,
        check=True, capture_output=True,
    )


if __name__ == "__main__":
    url = WRAP.as_uri()

    wrap(show_cut=True)
    run([f"--print-to-pdf={HERE / 'panel-sticker.pdf'}", "--no-pdf-header-footer",
         "--virtual-time-budget=4000", url])

    # CSS px, not device px: the window is sized in CSS units and the scale
    # factor is what turns 96 dpi into 300.
    css_w = round(PAGE_W / 25.4 * 96)
    css_h = round(PAGE_H / 25.4 * 96)
    scale = DPI / 96
    wrap(show_cut=False)
    run([f"--screenshot={HERE / 'panel-sticker-preview.png'}",
         f"--window-size={css_w},{css_h}",
         f"--force-device-scale-factor={scale:.6f}",
         "--virtual-time-budget=4000", url])
    WRAP.unlink()
    print(f"pdf  {PAGE_W} x {PAGE_H} mm  (with cut + crop marks)")
    print(f"png  {round(css_w * scale)} x {round(css_h * scale)} px @ {DPI} dpi  (art only)")
