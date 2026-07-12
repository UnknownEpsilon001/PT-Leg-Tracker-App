/**
 * Color for pain level 0 (green) .. 10 (red), clamped.
 *
 * Lightness is fixed at 28% (down from an earlier 38%) so that white text
 * rendered on this background meets WCAG AA contrast (>= 4.5:1) across the
 * whole hue ramp, including the yellow/olive hues around level 5 which are
 * the least favorable case. See PainScale.vue's selected-button style.
 */
export function painColor(n: number): string {
  const level = Math.min(10, Math.max(0, n))
  return `hsl(${120 - level * 12} 65% 28%)`
}
