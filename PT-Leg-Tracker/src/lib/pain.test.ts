import { describe, expect, it } from 'vitest'
import { painColor } from './pain'

describe('painColor', () => {
  it('maps 0 to green hue 120 and 10 to red hue 0', () => {
    expect(painColor(0)).toBe('hsl(120 65% 38%)')
    expect(painColor(10)).toBe('hsl(0 65% 38%)')
  })
  it('interpolates linearly (5 → hue 60)', () => {
    expect(painColor(5)).toBe('hsl(60 65% 38%)')
  })
  it('clamps out-of-range input', () => {
    expect(painColor(-3)).toBe(painColor(0))
    expect(painColor(99)).toBe(painColor(10))
  })
})
