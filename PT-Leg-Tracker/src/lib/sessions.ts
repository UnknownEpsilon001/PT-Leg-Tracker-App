// รวมรายการเซสชันจากเครื่อง (remote) กับที่เก็บในเครื่องผู้ใช้ (local) ให้ไม่ซ้ำ ID
// Merge device (remote) sessions with locally stored ones, de-duplicated by ID.
import type { Session } from '@/types'

export function mergeSessions(local: Session[], remote: Session[]): Session[] {
  const byId = new Map<string, Session>()
  for (const s of remote) byId.set(s.id, s)
  for (const s of local) byId.set(s.id, s) // local wins — ถ้า ID ซ้ำ ให้ค่าฝั่งเครื่องผู้ใช้ชนะ
  // [TH] เรียงตามวันที่จากเก่าไปใหม่
  return [...byId.values()].sort((a, b) => a.date.localeCompare(b.date))
}
