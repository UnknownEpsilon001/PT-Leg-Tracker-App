# [TH] แปลงไฟล์เสียงที่อัดมาให้เป็นรูปแบบเดียวที่เฟิร์มแวร์ยอมรับ
#
# Converts a recording into the one format pt-leg-display accepts, and refuses
# to write a file it would reject: 22050 Hz, 8-bit unsigned, mono, no metadata
# chunks, silence-padded at both ends.
#
#   .\firmware\tools\make-sounds.ps1 -In .\raw\start.m4a -Out .\sd\sounds\start.wav
#   .\firmware\tools\make-sounds.ps1 -In .\raw -Out .\sd\sounds        # whole folder
#
# The padding is not cosmetic. The DAC holds the last code it was given, so a
# clip that ends on a loud sample leaves the output there until the channel is
# disabled, and disabling it then steps the output audibly. Both ends are padded
# with 0x80, which is silence for unsigned 8-bit and the DAC's rest value.
# [TH] การเติมความเงียบหัวท้ายไม่ใช่เรื่องความสวยงาม
#      DAC จะค้างค่าสุดท้ายที่ได้รับไว้ ถ้าคลิปจบด้วยตัวอย่างที่ดัง เอาต์พุตจะค้างอยู่ตรงนั้น
#      พอปิดช่องสัญญาณก็จะกระโดดจนได้ยินเป็นเสียงป๊อก จึงเติม 0x80 ทั้งสองด้าน
#      ซึ่งเป็นความเงียบของรูปแบบ 8 บิตไม่มีเครื่องหมาย และเป็นค่าพักของ DAC
param(
    [Parameter(Mandatory = $true)][string]$In,
    [Parameter(Mandatory = $true)][string]$Out,
    [int]$PadMs = 20,
    [int]$MaxBytes = 49152,   # must match AUDIO_BUF_BYTES in audio.cpp
    [switch]$NoNormalise
)
$ErrorActionPreference = "Stop"

if (-not (Get-Command ffmpeg -CommandType Application -ErrorAction SilentlyContinue)) {
    throw "ffmpeg was not found in PATH. Install it before running this script."
}

$rate = 22050
$invariant = [System.Globalization.CultureInfo]::InvariantCulture
$padSeconds = ($PadMs / 1000.0).ToString($invariant)

# Speech recorded on a phone lands well below full scale, and there is no gain
# stage anywhere after this point — the firmware plays the samples as they are.
# Loudness is decided here or not at all.
# [TH] เสียงพูดที่อัดจากโทรศัพท์มักเบากว่าระดับเต็มมาก และหลังจากขั้นตอนนี้ไม่มีการปรับความดังอีกเลย
#      เฟิร์มแวร์เล่นตัวอย่างเสียงตามที่เป็น ความดังจึงถูกกำหนดที่นี่เท่านั้น
$filters = @()
if (-not $NoNormalise) { $filters += "loudnorm=I=-16:TP=-1.5:LRA=11" }
$filters += "aresample=$rate"
$filters += "aformat=sample_fmts=u8:channel_layouts=mono"
# adelay pads the head, apad the tail. Both are in the filter chain rather than
# done by hand so the sample values are the codec's silence, not a guess.
# [TH] adelay เติมด้านหน้า apad เติมด้านท้าย ทั้งคู่อยู่ในสายฟิลเตอร์แทนการเติมเอง
#      ค่าตัวอย่างที่ได้จึงเป็นความเงียบตามรูปแบบจริง ไม่ใช่ค่าที่เดาเอง
$filters += "adelay=$PadMs"
$filters += "apad=pad_dur=$padSeconds"
$chain = $filters -join ","

function Convert-One([string]$src, [string]$dst) {
    $dir = Split-Path -Parent $dst
    if ($dir -and -not (Test-Path $dir)) { New-Item -ItemType Directory -Force $dir | Out-Null }

    # -map_metadata -1 and -fflags +bitexact keep ffmpeg from writing a LIST
    # chunk. The parser walks chunks and would cope, but only inside its 256-byte
    # scan window — not writing the chunk at all removes the question.
    # [TH] สองตัวเลือกนี้กัน ffmpeg ไม่ให้เขียนชังก์ LIST
    #      ตัวแยกไล่อ่านชังก์อยู่แล้วจึงรับมือได้ แต่รับได้เฉพาะใน 256 ไบต์แรก
    #      การไม่เขียนชังก์นั้นตั้งแต่ต้นตัดปัญหาทิ้งไปเลย
    $ffArgs = @(
        "-hide_banner", "-loglevel", "error", "-y",
        "-i", $src,
        "-af", $chain,
        "-ac", "1", "-ar", "$rate", "-acodec", "pcm_u8",
        "-map_metadata", "-1", "-fflags", "+bitexact",
        $dst
    )
    & ffmpeg $ffArgs
    if ($LASTEXITCODE -ne 0) { throw "ffmpeg failed on $src" }

    $size = (Get-Item $dst).Length
    $samples = $size - 44
    $seconds = [math]::Round($samples / $rate, 2)
    if ($samples -gt $MaxBytes) {
        Remove-Item $dst -Force
        throw ("$src is ${seconds}s = $samples bytes of audio, over the ${MaxBytes}-byte " +
               "buffer. Record a shorter prompt; the firmware would reject this file.")
    }
    Write-Host ("{0,-16} {1,6}s  {2,7} bytes" -f (Split-Path -Leaf $dst), $seconds, $samples)
}

if (Test-Path $In -PathType Container) {
    Get-ChildItem $In -File | ForEach-Object {
        Convert-One $_.FullName (Join-Path $Out ($_.BaseName + ".wav"))
    }
} else {
    Convert-One $In $Out
}
Write-Host "done" -ForegroundColor Green
