import { Capacitor } from '@capacitor/core'
import { LocalNotifications } from '@capacitor/local-notifications'
import type { Settings } from '@/types'

const ALARM_ID = 1001

function parseTime(time: string): { hour: number; minute: number } {
  const [h, m] = time.split(':')
  return { hour: Number(h) || 0, minute: Number(m) || 0 }
}

async function schedule(time: string): Promise<void> {
  await LocalNotifications.cancel({ notifications: [{ id: ALARM_ID }] })
  await LocalNotifications.schedule({
    notifications: [
      {
        id: ALARM_ID,
        title: 'Smart OA Knee',
        body: 'ถึงเวลาบริหารเข่าแล้ว',
        schedule: { on: parseTime(time), allowWhileIdle: true },
      },
    ],
  })
}

/** Request permission and schedule the daily reminder. False = permission denied. */
export async function enableDailyAlarm(time: string): Promise<boolean> {
  const perm = await LocalNotifications.requestPermissions()
  if (perm.display !== 'granted') return false
  await schedule(time)
  return true
}

export async function cancelDailyAlarm(): Promise<void> {
  await LocalNotifications.cancel({ notifications: [{ id: ALARM_ID }] })
}

/** Best-effort re-registration on app launch. Never throws; inert on web. */
export async function syncAlarmFromSettings(settings: Settings): Promise<void> {
  if (!Capacitor.isNativePlatform()) return
  if (!settings.alarmEnabled || !settings.alarmTime) return
  try {
    const perm = await LocalNotifications.checkPermissions()
    if (perm.display !== 'granted') return
    await schedule(settings.alarmTime)
  } catch {
    // notification re-registration is best-effort
  }
}
