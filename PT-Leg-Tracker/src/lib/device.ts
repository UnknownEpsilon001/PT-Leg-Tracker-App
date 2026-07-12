import type { DeviceStatus } from '@/types'

export type DeviceErrorKind = 'unreachable' | 'busy' | 'idle'

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

export async function startSession(deviceUrl: string): Promise<string> {
  const data = (await request(`${base(deviceUrl)}/start`, 'busy', { method: 'POST' })) as {
    sessionId?: unknown
  }
  if (typeof data.sessionId !== 'string' || data.sessionId === '') throw new DeviceError('unreachable')
  return data.sessionId
}

export async function getStatus(deviceUrl: string): Promise<DeviceStatus> {
  const d = (await request(`${base(deviceUrl)}/status`, 'unreachable')) as Record<string, unknown>
  if (d.state !== 'idle' && d.state !== 'running') throw new DeviceError('unreachable')
  return {
    state: d.state,
    elapsedSec: typeof d.elapsedSec === 'number' ? d.elapsedSec : 0,
    sessionId: typeof d.sessionId === 'string' ? d.sessionId : null,
  }
}

export async function stopSession(deviceUrl: string): Promise<void> {
  await request(`${base(deviceUrl)}/stop`, 'idle', { method: 'POST' })
}
