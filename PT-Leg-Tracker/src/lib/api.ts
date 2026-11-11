// ดึงประวัติเซสชันจากเซิร์ฟเวอร์ตามรหัสผู้ป่วย แล้วรวมเข้ากับข้อมูลในเครื่อง
// Fetch a patient's session history from the server and merge into the local log.
import type { Session } from '@/types'
import { flushClaims } from './claims'
import { mergeSessions } from './sessions'
import { useSessionsStore } from '@/stores/sessions'
import { useSettingsStore } from '@/stores/settings'
import { useProfileStore } from '@/stores/profile'

interface RemoteSession {
  id: string
  startedAt: string
  endedAt: string | null
  durationSec: number | null
  reps: number
  patientCode: string | null
}

export async function fetchDeviceSessions(
  serverUrl: string,
  patientCode: string,
): Promise<Session[]> {
  const base = serverUrl.replace(/\/+$/, '')
  const res = await fetch(`${base}/api/sessions?patientCode=${encodeURIComponent(patientCode)}`)
  if (!res.ok) throw new Error(`HTTP ${res.status}`)
  const list = (await res.json()) as RemoteSession[]
  return list.map((r) => ({
    id: r.id,
    date: r.startedAt,
    durationSec: r.durationSec ?? 0,
    source: 'device' as const,
  }))
}

/** Fetch device sessions and merge into local log. Never throws. */
// [TH] ซิงก์ทันที: ดึงเซสชันจากเครื่องมารวมกับ log ในเครื่อง ไม่โยน error (คืน false เมื่อพลาด)
export async function syncNow(): Promise<boolean> {
  const settings = useSettingsStore()
  const profile = useProfileStore()
  const sessions = useSessionsStore()
  const url = settings.settings.serverUrl
  const code = profile.profile?.patientCode
  if (!url || !code) {
    console.info('[SYNC] skipped: no server URL or patient code')
    return false
  }
  try {
    // claims first, so a session claimed now is in the list fetched below
    // [TH] ผูกเซสชันที่ค้างก่อน เซสชันที่เพิ่งผูกได้จะได้อยู่ในรายการที่ดึงด้านล่าง
    await flushClaims(url)
    const remote = await fetchDeviceSessions(url, code)
    await sessions.replaceAll(mergeSessions(sessions.sessions, remote))
    await settings.update({ lastSync: new Date().toISOString() })
    console.info(`[SYNC] ok, ${remote.length} from server`)
    return true
  } catch (err) {
    console.warn('[SYNC] failed', err)
    return false
  }
}
