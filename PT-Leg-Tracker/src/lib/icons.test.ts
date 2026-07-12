import { describe, expect, it } from 'vitest'
import { ICONS } from './icons'

const REQUIRED = [
  'book-open', 'wrench', 'activity', 'bar-chart', 'settings', 'bell',
  'save', 'pencil', 'text-size', 'refresh', 'share', 'play', 'stop',
  'check', 'chevron-left', 'clipboard', 'star', 'wifi',
]

describe('ICONS', () => {
  it('contains every required icon with non-empty path data', () => {
    for (const name of REQUIRED) {
      expect(ICONS[name], name).toBeDefined()
      expect(ICONS[name]!.length, name).toBeGreaterThan(0)
      for (const d of ICONS[name]!) expect(d.length, name).toBeGreaterThan(3)
    }
  })
})
