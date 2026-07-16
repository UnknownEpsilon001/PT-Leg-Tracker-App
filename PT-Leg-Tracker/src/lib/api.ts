import type { Session } from '@/types'
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
export async function syncNow(): Promise<boolean> {
  const settings = useSettingsStore()
  const profile = useProfileStore()
  const sessions = useSessionsStore()
  const url = settings.settings.serverUrl
  const code = profile.profile?.patientCode
  if (!url || !code) return false
  try {
    const remote = await fetchDeviceSessions(url, code)
    await sessions.replaceAll(mergeSessions(sessions.sessions, remote))
    await settings.update({ lastSync: new Date().toISOString() })
    return true
  } catch {
    return false
  }
}
