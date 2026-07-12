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
export function buildRecordRows(sessions: Session[], painLogs: PainLog[]): RecordRow[] {
  const byId = new Map(painLogs.filter((l) => l.sessionId).map((l) => [l.sessionId as string, l]))
  const rows: RecordRow[] = sessions.map((s) => {
    const log = byId.get(s.id)
    if (log) byId.delete(s.id)
    return {
      id: s.id,
      date: s.date,
      durationSec: s.durationSec,
      painBefore: log?.painBefore ?? s.painBefore,
      painAfter: log?.painAfter ?? s.painAfter,
      source: s.source,
    }
  })
  const matchedIds = new Set(sessions.map((s) => s.id))
  for (const log of painLogs) {
    if (log.sessionId && matchedIds.has(log.sessionId)) continue
    rows.push({
      id: `pain-${log.recordedAt}`,
      date: log.recordedAt,
      durationSec: null,
      painBefore: log.painBefore,
      painAfter: log.painAfter,
      source: 'manual',
    })
  }
  return rows
}
