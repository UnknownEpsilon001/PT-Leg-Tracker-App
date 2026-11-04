// คิวเซสชันที่ยังผูกกับรหัสผู้ป่วยไม่สำเร็จ เก็บไว้ลองใหม่ตอนบันทึกหรือซิงก์ครั้งถัดไป
// Queue of sessions whose claim has not landed yet, retried on the next save or sync.
// The server only lists claimed sessions for a patient, so a dropped claim would
// otherwise remove the session from that patient's history for good.
import { DeviceError, claimSession } from './device'
import { loadJson, saveJson } from './storage'

const KEY = 'pendingClaims'

export async function queueClaim(sessionId: string): Promise<void> {
  const ids = await loadJson<string[]>(KEY, [])
  if (!ids.includes(sessionId)) await saveJson(KEY, [...ids, sessionId])
}

// [TH] ลองผูกทุกเซสชันในคิว เก็บไว้เฉพาะอันที่ยังไม่สำเร็จ ; 404 = เซิร์ฟเวอร์ไม่รู้จักเซสชันนี้ ลองไปก็ไม่สำเร็จ จึงทิ้ง
// Retries every queued claim and keeps the ones that still fail. A 404 means
// the server does not know the session, so retrying can never succeed: drop it.
export async function flushClaims(serverUrl: string, patientCode: string): Promise<void> {
  const ids = await loadJson<string[]>(KEY, [])
  if (ids.length === 0) return
  const left: string[] = []
  for (const id of ids) {
    try {
      await claimSession(serverUrl, id, patientCode)
    } catch (e) {
      if (!(e instanceof DeviceError && e.kind === 'not-found')) left.push(id)
    }
  }
  await saveJson(KEY, left)
}
