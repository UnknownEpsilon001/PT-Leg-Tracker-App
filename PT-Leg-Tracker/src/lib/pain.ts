/** Color for pain level 0 (green) .. 10 (red), clamped. */
export function painColor(n: number): string {
  const level = Math.min(10, Math.max(0, n))
  return `hsl(${120 - level * 12} 65% 38%)`
}
