// เสียงเอฟเฟกต์สั้นๆ ทุกครั้งที่แตะปุ่ม ลิงก์ หรือสวิตช์ (ไม่มีเสียงพูดในแอป)
// Short sound effect on every button, link or toggle press (no voice in the app).
const TAPPABLE = 'button, a, [role="button"], input[type="checkbox"], input[type="radio"], select'

export function installTapSound(): void {
  const tap = new Audio('/sounds/tap.mp3')
  tap.preload = 'auto'
  // pointerdown, not click: the sound lands with the finger, not after release
  // [TH] ใช้ pointerdown แทน click เสียงจะดังตอนนิ้วแตะ ไม่ใช่ตอนปล่อย
  document.addEventListener(
    'pointerdown',
    (e) => {
      const el = (e.target as Element | null)?.closest(TAPPABLE)
      if (!el || el.matches(':disabled')) return
      tap.currentTime = 0
      tap.play().catch(() => {}) // blocked or still loading — silent — เล่นไม่ได้ก็เงียบ
    },
    { passive: true },
  )
}
