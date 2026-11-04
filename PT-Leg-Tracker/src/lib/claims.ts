// คิวเซสชันที่ยังผูกกับรหัสผู้ป่วยไม่สำเร็จ เก็บไว้ลองใหม่ตอนบันทึกหรือซิงก์ครั้งถัดไป
// Queue of sessions whose claim has not landed yet, retried on the next save or sync.
// The server only lists claimed sessions for a patient, so a dropped claim would
// otherwise remove the session from that patient's history for good.
import { DeviceError, claimSession } from './device'
import { loadJson, saveJson } from './storage'

const KEY = 'pendingClaims'

// Each entry keeps the patient code it was saved under: if the phone is
// re-registered to another patient, an old session must still be claimed for
// its own patient, never for whoever holds the phone at retry time.
// [TH] เก็บรหัสผู้ป่วยไว้กับแต่ละรายการ ถ้าเครื่องถูกลงทะเบียนใหม่ให้ผู้ป่วยคนอื่น
//      เซสชันเก่าต้องยังผูกกับผู้ป่วยเจ้าของเดิม ไม่ใช่คนที่ถือเครื่องตอนลองใหม่
interface PendingClaim {
  sessionId: string
  patientCode: string
}

export async function queueClaim(sessionId: string, patientCode: string): Promise<void> {
  const list = await loadJson<PendingClaim[]>(KEY, [])
  if (!list.some((c) => c.sessionId === sessionId))
    await saveJson(KEY, [...list, { sessionId, patientCode }])
}

// [TH] ลองผูกทุกเซสชันในคิว เก็บไว้เฉพาะอันที่ยังไม่สำเร็จ ; 404 = เซิร์ฟเวอร์ไม่รู้จักเซสชันนี้ ลองไปก็ไม่สำเร็จ จึงทิ้ง
// Retries every queued claim and keeps the ones that still fail. A 404 means
// the server does not know the session, so retrying can never succeed: drop it.
export async function flushClaims(serverUrl: string): Promise<void> {
  const list = await loadJson<PendingClaim[]>(KEY, [])
  if (list.length === 0) return
  const left: PendingClaim[] = []
  for (const c of list) {
    try {
      await claimSession(serverUrl, c.sessionId, c.patientCode)
    } catch (e) {
      if (!(e instanceof DeviceError && e.kind === 'not-found')) left.push(c)
    }
  }
  await saveJson(KEY, left)
}
