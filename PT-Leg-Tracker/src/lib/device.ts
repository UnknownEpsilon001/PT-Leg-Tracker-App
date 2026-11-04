// ไคลเอนต์สั่งงานอุปกรณ์ผ่านเซิร์ฟเวอร์ (v3): เริ่ม/หยุดเซสชัน และอ่านสถานะปัจจุบัน
// Server-mediated device-control client (v3): start/stop a session, read current state.
import type { CurrentSession } from '@/types'

// [TH] ชนิดของข้อผิดพลาดจากอุปกรณ์: ติดต่อไม่ได้ / ไม่ว่าง / ว่างอยู่ / ออฟไลน์ / ไม่ได้ตั้งรหัสอุปกรณ์ / ไม่พบ (404)
export type DeviceErrorKind =
  | 'unreachable'
  | 'busy'
  | 'idle'
  | 'offline'
  | 'no-device'
  | 'not-found'

export class DeviceError extends Error {
  kind: DeviceErrorKind
  constructor(kind: DeviceErrorKind) {
    super(kind)
    this.kind = kind
  }
}

const TIMEOUT_MS = 5000  // หมดเวลารอคำขอ (มิลลิวินาที)

function base(url: string): string {
  return url.replace(/\/+$/, '')
}

// [TH] ยิงคำขอ HTTP พร้อมตั้งเวลาหมดอายุ ; สถานะ 409 = ชนกันตามบริบท, 404 = ไม่พบ, อื่นๆ ที่ไม่ ok = ติดต่อไม่ได้
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
    if (res.status === 404) throw new DeviceError('not-found')
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
// [TH] ทุกคำสั่งต้องแนบ deviceId (รหัสอุปกรณ์) ; ถ้าไม่ได้ตั้งรหัสจะโยน no-device
function control(
  serverUrl: string,
  deviceCode: string,
  path: string,
  conflictKind: DeviceErrorKind,
  init?: RequestInit,
): Promise<unknown> {
  const code = deviceCode.trim()
  if (!code) throw new DeviceError('no-device')
  const sep = path.includes('?') ? '&' : '?'
  const url = `${base(serverUrl)}${path}${sep}deviceId=${encodeURIComponent(code)}`
  return request(url, conflictKind, init)
}

export async function queueStart(
  serverUrl: string,
  deviceCode: string,
  targetReps?: number,
): Promise<void> {
  // The target rides the start request so a session begins with the number the
  // user just picked, rather than with whatever the device happened to hold.
  // [TH] ส่งเป้าหมายไปพร้อมคำสั่งเริ่ม รอบจะได้เริ่มด้วยจำนวนที่ผู้ใช้เพิ่งเลือก
  //      ไม่ใช่ค่าที่บังเอิญค้างอยู่ในเครื่อง
  const init: RequestInit =
    targetReps === undefined
      ? { method: 'POST' }
      : {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ targetReps }),
        }
  await control(serverUrl, deviceCode, '/api/sessions/start', 'busy', init)
}

// [TH] ส่งเป้าหมายจำนวนครั้งไปที่เซิร์ฟเวอร์ ; เครื่องจะรับไปเองตอนถามคำสั่งครั้งถัดไป
export async function setTarget(
  serverUrl: string,
  deviceCode: string,
  reps: number,
): Promise<void> {
  // This route never answers 409, unlike start/stop, so there is no conflict
  // kind to report — any non-ok response is just unreachable.
  // [TH] เส้นทางนี้ไม่ตอบ 409 เหมือน start/stop จึงไม่มีสถานะชนกันให้รายงาน
  //      คำตอบที่ไม่ ok จะถือว่าติดต่อไม่ได้เท่านั้น
  await control(serverUrl, deviceCode, '/api/sessions/target', 'unreachable', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ targetReps: reps }),
  })
}

// [TH] ค่าเวลาของรอบที่แก้ได้จากแอป ส่งเฉพาะฟิลด์ที่ต้องการเปลี่ยน
export interface DeviceTimings {
  liftSec?: number
  lowerSec?: number
  holdSec?: number
  restSec?: number
  maxTravelSec?: number
}

export async function setTimings(
  serverUrl: string,
  deviceCode: string,
  timings: DeviceTimings,
): Promise<void> {
  // Same reasoning as setTarget: no 409 from this route, so 'unreachable'
  // covers every failure.
  // [TH] เหตุผลเดียวกับ setTarget: เส้นทางนี้ไม่มี 409 จึงใช้ 'unreachable'
  //      ครอบคลุมความล้มเหลวทั้งหมด
  await control(serverUrl, deviceCode, '/api/sessions/timings', 'unreachable', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(timings),
  })
}

export async function queueStop(serverUrl: string, deviceCode: string): Promise<void> {
  await control(serverUrl, deviceCode, '/api/sessions/stop', 'idle', { method: 'POST' })
}

export async function getCurrent(serverUrl: string, deviceCode: string): Promise<CurrentSession> {
  const d = (await control(serverUrl, deviceCode, '/api/sessions/current', 'unreachable')) as Record<
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
    targetReps: typeof d.targetReps === 'number' ? d.targetReps : 0,
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
