import type { CurrentSession } from '@/types'

export type DeviceErrorKind = 'unreachable' | 'busy' | 'idle' | 'offline'

export class DeviceError extends Error {
  kind: DeviceErrorKind
  constructor(kind: DeviceErrorKind) {
    super(kind)
    this.kind = kind
  }
}

const TIMEOUT_MS = 5000

function base(url: string): string {
  return url.replace(/\/+$/, '')
}

async function request(
  url: string,
  conflictKind: DeviceErrorKind,
  init?: RequestInit,
): Promise<unknown> {
  const ctrl = new AbortController()
  const timer = setTimeout(() => ctrl.abort(), TIMEOUT_MS)
  try {
    const res = await fetch(url, { ...init, signal: ctrl.signal })
    if (res.status === 409) throw new DeviceError(conflictKind)
    if (!res.ok) throw new DeviceError('unreachable')
    return await res.json()
  } catch (e) {
    if (e instanceof DeviceError) throw e
    throw new DeviceError('unreachable')
  } finally {
    clearTimeout(timer)
  }
}

// --- v3 server-session client (spec: server-mediated device control) ---

export async function queueStart(serverUrl: string): Promise<void> {
  await request(`${base(serverUrl)}/api/sessions/start`, 'busy', { method: 'POST' })
}

export async function queueStop(serverUrl: string): Promise<void> {
  await request(`${base(serverUrl)}/api/sessions/stop`, 'idle', { method: 'POST' })
}

export async function getCurrent(serverUrl: string): Promise<CurrentSession> {
  const d = (await request(`${base(serverUrl)}/api/sessions/current`, 'unreachable')) as Record<
    string,
    unknown
  >
  if (d.state !== 'idle' && d.state !== 'starting' && d.state !== 'running')
    throw new DeviceError('unreachable')
  return {
    sessionId: typeof d.sessionId === 'string' ? d.sessionId : null,
    state: d.state,
    elapsedSec: typeof d.elapsedSec === 'number' ? d.elapsedSec : 0,
    reps: typeof d.reps === 'number' ? d.reps : 0,
    deviceOnline: d.deviceOnline === true,
  }
}

export async function claimSession(
  serverUrl: string,
  sessionId: string,
  patientCode: string,
): Promise<void> {
  await request(
    `${base(serverUrl)}/api/sessions/${encodeURIComponent(sessionId)}/claim`,
    'unreachable',
    {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ patientCode }),
    },
  )
}
