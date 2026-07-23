// ทดสอบสโตร์ค่าตั้งค่า: การเติมค่าตั้งต้นและการอัปเดต (stores/settings.ts)
// Tests for the settings store: default merging and updates.
import { describe, expect, it } from 'vitest'
import { DEFAULTS } from './settings'

describe('settings DEFAULTS', () => {
  it('has safe defaults for v2 fields', () => {
    expect(DEFAULTS.alarmEnabled).toBe(false)
    expect(DEFAULTS.alarmTime).toBe('09:00')
  })
})
