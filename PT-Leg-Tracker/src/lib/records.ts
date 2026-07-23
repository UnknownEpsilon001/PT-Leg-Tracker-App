// รวมเซสชันจากเครื่องกับบันทึกระดับความปวดของผู้ใช้ ให้เป็นแถวข้อมูลสำหรับหน้าประวัติ
// Combine device sessions with the user's pain logs into rows for the records view.
import type { PainLog, Session } from '@/types'

export interface RecordRow {
  id: string
  date: string
  durationSec: number | null
  painBefore?: number
  painAfter?: number
  source: 'device' | 'manual'
}

/** Merge device sessions and app pain logs into display rows (unsorted). */
// [TH] รวมเซสชันกับบันทึกความปวดเป็นแถวสำหรับแสดง (ยังไม่เรียงลำดับ)
export function buildRecordRows(sessions: Session[], painLogs: PainLog[]): RecordRow[] {
  // First-wins: only the earliest log for a given sessionId is eligible to merge.
  // [TH] ตัวแรกชนะ: ต่อหนึ่ง sessionId ใช้เฉพาะบันทึกความปวดอันแรกสุดในการจับคู่
  const byId = new Map<string, PainLog>()
  for (const log of painLogs) {
    if (log.sessionId && !byId.has(log.sessionId)) byId.set(log.sessionId, log)
  }
  // Track consumed logs by identity so extra logs sharing a matched sessionId
  // (or any log not actually merged) still surface as standalone rows.
  // [TH] จำบันทึกที่ถูกใช้ไปแล้ว เพื่อให้บันทึกที่เหลือ (ไม่ถูกจับคู่) ยังโผล่เป็นแถวเดี่ยวของตัวเอง
  const consumed = new Set<PainLog>()
  const rows: RecordRow[] = sessions.map((s) => {
    const log = byId.get(s.id)
    if (log) consumed.add(log)
    return {
      id: s.id,
      date: s.date,
      durationSec: s.durationSec,
      painBefore: log?.painBefore ?? s.painBefore,
      painAfter: log?.painAfter ?? s.painAfter,
      source: s.source,
    }
  })
  for (const [index, log] of painLogs.entries()) {
    if (consumed.has(log)) continue
    rows.push({
      id: `pain-${log.recordedAt}-${index}`,
      date: log.recordedAt,
      durationSec: null,
      painBefore: log.painBefore,
      painAfter: log.painAfter,
      source: 'manual',
    })
  }
  return rows
}
