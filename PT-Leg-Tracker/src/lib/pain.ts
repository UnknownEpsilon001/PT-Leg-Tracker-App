/**
 * Color for pain level 0 (green) .. 10 (red), clamped.
 *
 * Lightness is fixed at 28% (down from an earlier 38%) so that white text
 * rendered on this background meets WCAG AA contrast (>= 4.5:1) across the
 * whole hue ramp, including the yellow/olive hues around level 5 which are
 * the least favorable case. See PainScale.vue's selected-button style.
 *
 * [TH] สีตามระดับความปวด 0 (เขียว) ถึง 10 (แดง) โดยจำกัดช่วงค่า
 *      ตรึงความสว่างไว้ที่ 28% เพื่อให้ตัวอักษรสีขาวบนพื้นนี้ผ่านเกณฑ์คอนทราสต์ WCAG AA
 *      (>= 4.5:1) ทุกเฉดสี รวมถึงเฉดเหลือง/เขียวมะกอกช่วงระดับ 5 ที่อ่านยากที่สุด
 */
export function painColor(n: number): string {
  const level = Math.min(10, Math.max(0, n))
  return `hsl(${120 - level * 12} 65% 28%)`
}
