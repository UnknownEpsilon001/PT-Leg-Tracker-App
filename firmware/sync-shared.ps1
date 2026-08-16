# [TH] คัดลอกไฟล์ที่ใช้ร่วมกันจากสเก็ตช์คอนโทรลเลอร์ (ถือเป็นต้นฉบับ) ไปยังสเก็ตช์จอ
#      เพราะ Arduino ไม่ให้ include โค้ดข้ามโฟลเดอร์สเก็ตช์ จึงต้องเก็บเป็นไฟล์ซ้ำ
#      รันแบบ -Check ก่อนปล่อยรุ่น เพื่อกันสองบอร์ดเข้าใจโครงสร้างข้อมูลไม่ตรงกัน
#
# Copies the files shared by both sketches from the controller (the source of
# truth) into the display sketch. Arduino gives a sketch no way to include code
# from outside its own directory, so these exist as duplicates.
#
#   .\firmware\sync-shared.ps1          copy controller -> display
#   .\firmware\sync-shared.ps1 -Check   report drift, change nothing (exit 1 if any)
#
# Run the -Check form before building a release. Silent drift here means the
# UART settings message loses a field and the two boards disagree about the
# machine's configuration.

param([switch]$Check)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$from = Join-Path $root "pt-leg-controller"
$to = Join-Path $root "pt-leg-display"
# session.h is deliberately NOT here. The display's copy is a reduced one — the
# Phase enum alone, because that board receives a phase number over the link and
# never runs the state machine — so the two files are meant to differ. Listing it
# made -Check report DRIFT on a clean tree, and a gate that is red for a benign
# reason is a gate people stop reading, which is exactly how real link.h drift
# reaches hardware. Worse, the documented remedy (run without -Check) would have
# copied the controller's whole state machine over the display's minimal header.
# The one thing that must still match is the enumerator ORDER, since the wire's
# "phase":int is that order; both copies carry a comment saying so.
# [TH] จงใจไม่ใส่ session.h ไว้ตรงนี้ ฝั่งจอใช้สำเนาแบบย่อ คือมีแต่ enum ของสถานะ
#      เพราะบอร์ดนั้นรับหมายเลขสถานะมาทางลิงก์และไม่ได้เดินเครื่องสถานะเอง สองไฟล์จึงตั้งใจให้ต่างกัน
#      การใส่ไว้ทำให้ -Check รายงานว่าไฟล์ไม่ตรงกันทั้งที่ยังไม่มีการแก้อะไร
#      และประตูตรวจที่ขึ้นแดงด้วยเหตุผลที่ไม่เป็นเรื่อง คือประตูที่คนเลิกอ่าน
#      ซึ่งเป็นทางที่ความไม่ตรงกันจริงๆ ของ link.h จะหลุดไปถึงฮาร์ดแวร์
#      แย่กว่านั้นคือวิธีแก้ที่เขียนไว้ (รันโดยไม่ใส่ -Check) จะเอาเครื่องสถานะทั้งหมดของคอนโทรลเลอร์
#      ไปทับไฟล์แบบย่อของจอ สิ่งเดียวที่ยังต้องตรงกันคือลำดับของค่าใน enum
#      เพราะ "phase":int บนสายอ้างอิงลำดับนั้นโดยตรง ทั้งสองไฟล์มีคอมเมนต์กำกับไว้แล้ว
$shared = @("link.h", "link.cpp", "settings.h")

$drift = @()
foreach ($f in $shared) {
    $src = Join-Path $from $f
    $dst = Join-Path $to $f
    if (-not (Test-Path $src)) { throw "missing source: $src" }

    $same = (Test-Path $dst) -and
            ((Get-FileHash $src).Hash -eq (Get-FileHash $dst).Hash)
    if ($same) { continue }

    if ($Check) {
        $drift += $f
        Write-Host "DRIFT: $f" -ForegroundColor Yellow
    } else {
        Copy-Item $src $dst -Force
        Write-Host "synced: $f" -ForegroundColor Green
    }
}

if ($Check) {
    if ($drift.Count -gt 0) {
        Write-Host "$($drift.Count) shared file(s) differ. Run without -Check to sync." -ForegroundColor Red
        exit 1
    }
    Write-Host "shared files in sync" -ForegroundColor Green
} elseif ($drift.Count -eq 0) {
    Write-Host "nothing to do" -ForegroundColor DarkGray
}
