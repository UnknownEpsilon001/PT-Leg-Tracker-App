// เสียงเอฟเฟกต์สั้นๆ ทุกครั้งที่แตะปุ่ม ลิงก์ หรือสวิตช์ (ไม่มีเสียงพูดในแอป)
// Short sound effect on every button, link or toggle press (no voice in the app).
const TAPPABLE = 'button, a, [role="button"], input[type="checkbox"], input[type="radio"], select'

export function installTapSound(): void {
  const tap = new Audio('/sounds/tap.mp3')
  tap.preload = 'auto'
  // click, not pointerdown: a finger that lands on a button to scroll the page
  // fires pointerdown too, and every swipe would tick
  // [TH] ใช้ click แทน pointerdown เพราะนิ้วที่แตะปุ่มเพื่อเลื่อนหน้าก็ทำให้เกิด pointerdown และจะมีเสียงทุกครั้งที่เลื่อน
  document.addEventListener(
    'click',
    (e) => {
      const el = (e.target as Element | null)?.closest(TAPPABLE)
      if (!el || el.matches(':disabled')) return
      tap.currentTime = 0
      tap.play().catch(() => {}) // blocked or still loading — silent — เล่นไม่ได้ก็เงียบ
    },
    { passive: true },
  )
}
