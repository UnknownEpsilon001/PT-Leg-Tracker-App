import { beforeEach, describe, expect, it, vi } from 'vitest'

const mocks = vi.hoisted(() => ({
  requestPermissions: vi.fn(),
  checkPermissions: vi.fn(),
  schedule: vi.fn(),
  cancel: vi.fn(),
}))

vi.mock('@capacitor/local-notifications', () => ({ LocalNotifications: mocks }))
vi.mock('@capacitor/core', () => ({ Capacitor: { isNativePlatform: () => true } }))

import { cancelDailyAlarm, enableDailyAlarm, syncAlarmFromSettings } from './alarm'

beforeEach(() => {
  vi.clearAllMocks()
  mocks.requestPermissions.mockResolvedValue({ display: 'granted' })
  mocks.checkPermissions.mockResolvedValue({ display: 'granted' })
  mocks.schedule.mockResolvedValue(undefined)
  mocks.cancel.mockResolvedValue(undefined)
})

describe('enableDailyAlarm', () => {
  it('schedules a daily notification at the given time', async () => {
    await expect(enableDailyAlarm('07:30')).resolves.toBe(true)
    expect(mocks.schedule).toHaveBeenCalledWith({
      notifications: [
        expect.objectContaining({
          id: 1001,
          body: 'ถึงเวลาบริหารเข่าแล้ว',
          schedule: expect.objectContaining({ on: { hour: 7, minute: 30 } }),
        }),
      ],
    })
  })
  it('returns false and does not schedule when permission denied', async () => {
    mocks.requestPermissions.mockResolvedValue({ display: 'denied' })
    await expect(enableDailyAlarm('07:30')).resolves.toBe(false)
    expect(mocks.schedule).not.toHaveBeenCalled()
  })
})

describe('cancelDailyAlarm', () => {
  it('cancels the fixed id', async () => {
    await cancelDailyAlarm()
    expect(mocks.cancel).toHaveBeenCalledWith({ notifications: [{ id: 1001 }] })
  })
})

describe('syncAlarmFromSettings', () => {
  const base = {
    fontLarge: false,
    serverUrl: '',
    lastSync: null,
    alarmEnabled: true,
    alarmTime: '09:00',
  }
  it('re-schedules when enabled and permission already granted', async () => {
    await syncAlarmFromSettings(base)
    expect(mocks.schedule).toHaveBeenCalled()
  })
  it('does nothing when disabled', async () => {
    await syncAlarmFromSettings({ ...base, alarmEnabled: false })
    expect(mocks.schedule).not.toHaveBeenCalled()
  })
  it('never throws even if the plugin explodes', async () => {
    mocks.checkPermissions.mockRejectedValue(new Error('boom'))
    await expect(syncAlarmFromSettings(base)).resolves.toBeUndefined()
  })
})
