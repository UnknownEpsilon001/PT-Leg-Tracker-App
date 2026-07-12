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
  // First-wins: only the earliest log for a given sessionId is eligible to merge.
  const byId = new Map<string, PainLog>()
  for (const log of painLogs) {
    if (log.sessionId && !byId.has(log.sessionId)) byId.set(log.sessionId, log)
  }
  // Track consumed logs by identity so extra logs sharing a matched sessionId
  // (or any log not actually merged) still surface as standalone rows.
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
  for (const log of painLogs) {
    if (consumed.has(log)) continue
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
