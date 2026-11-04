# [TH] สร้างไฟล์เสียงสี่รายการของบอร์ดจอ: เสียงเอฟเฟกต์สั้นๆ ตามด้วยเสียงพูดภาษาไทยจาก edge-tts
#
# Builds the display board's four cue files: a short sound effect, then the Thai
# phrase spoken by a Microsoft neural voice. The mix is handed to make-sounds.ps1
# for the final conversion, so the format and the 49152-byte cap are checked in
# one place only.
#
#   python firmware/tools/make-cues.py                         # -> firmware/sd/sounds/
#   python firmware/tools/make-cues.py --voice th-TH-NiwatNeural
#
# Needs ffmpeg on PATH and `pip install edge-tts`. edge-tts sends the four
# phrases to Microsoft's online TTS service, so this needs internet; the device
# itself never does.
# [TH] ต้องมี ffmpeg และติดตั้ง edge-tts ก่อน edge-tts ส่งข้อความสี่วลีไปที่บริการ TTS ออนไลน์ของไมโครซอฟท์
#      จึงต้องต่ออินเทอร์เน็ตตอนสร้างไฟล์ ส่วนตัวเครื่องไม่ต้องต่อ
import argparse
import asyncio
import subprocess
from pathlib import Path

import edge_tts

ROOT = Path(__file__).resolve().parents[2]
SFX = ROOT / "assets" / "soundeffectthatifound" / "UI Sound"

# cue name -> (spoken phrase, effect played before it). The phrases match the
# table in firmware/README.md and the comments in audio.h.
# [TH] ชื่อไฟล์ -> (วลีที่พูด, เสียงเอฟเฟกต์ที่เล่นก่อน) วลีตรงกับตารางใน README และคอมเมนต์ใน audio.h
CUES = {
    "boot": ("เครื่องพร้อมใช้งาน", "UI Sound 7.mp3"),
    "start": ("เริ่มการทำงาน", "UI Sound 5.mp3"),
    "stop": ("หยุดการทำงาน", "UI Sound 8.mp3"),
    "target": ("ครบตามเป้าหมายแล้ว", "UI Sound 10.mp3"),
}

# The whole clip has 2.23 s. Speech trims to about 1.3 s, so the effect gets at
# most 0.5 s, faded out over its last 0.1 s so a cut-short effect does not click.
# [TH] คลิปทั้งหมดยาวได้ 2.23 วินาที เสียงพูดหลังตัดความเงียบยาวราว 1.3 วินาที
#      เอฟเฟกต์จึงได้ไม่เกิน 0.5 วินาที และค่อยๆ เบาลงใน 0.1 วินาทีสุดท้ายเพื่อไม่ให้มีเสียงคลิก
SFX_MAX_S = 0.5
GAP_S = 0.08

# Trim leading and trailing silence only. Reversing to trim the tail keeps the
# short pauses between Thai words, which a plain stop_periods=-1 would eat.
# [TH] ตัดความเงียบเฉพาะหัวและท้าย การกลับด้านเพื่อตัดท้ายช่วยเก็บช่วงหยุดสั้นๆ ระหว่างคำไว้
TRIM = ("silenceremove=start_periods=1:start_threshold=-45dB,areverse,"
        "silenceremove=start_periods=1:start_threshold=-45dB,areverse")


def ffmpeg(*args):
    subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", *args], check=True)


async def build(voice: str, work: Path):
    work.mkdir(parents=True, exist_ok=True)
    mixed = work / "mixed"
    mixed.mkdir(exist_ok=True)
    fade_at = SFX_MAX_S - 0.1
    for name, (text, sfx) in CUES.items():
        speech = work / f"{name}.tts.mp3"
        await edge_tts.Communicate(text, voice).save(str(speech))
        # [effect, trimmed + faded] [gap] [speech, silence-trimmed] -> one mono file
        ffmpeg("-i", str(SFX / sfx), "-i", str(speech),
               "-filter_complex",
               f"[0:a]atrim=0:{SFX_MAX_S},afade=t=out:st={fade_at}:d=0.1,"
               f"aresample=22050,aformat=channel_layouts=mono,apad=pad_dur={GAP_S}[a];"
               f"[1:a]{TRIM},aresample=22050,aformat=channel_layouts=mono[b];"
               "[a][b]concat=n=2:v=0:a=1[out]",
               "-map", "[out]", str(mixed / f"{name}.wav"))
    return mixed


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--voice", default="th-TH-PremwadeeNeural")
    ap.add_argument("--out", default=str(ROOT / "firmware" / "sd" / "sounds"))
    a = ap.parse_args()
    mixed = asyncio.run(build(a.voice, ROOT / "firmware" / ".build" / "cue-src"))
    subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                    str(ROOT / "firmware" / "tools" / "make-sounds.ps1"),
                    "-In", str(mixed), "-Out", a.out], check=True)


if __name__ == "__main__":
    main()
