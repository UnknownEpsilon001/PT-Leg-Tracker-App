import { describe, expect, it } from 'vitest'
import type { PainLog, Session } from '@/types'
import { buildRecordRows } from './records'

const session = (id: string, date: string): Session => ({
  id,
  date,
  durationSec: 600,
  source: 'device',
})

describe('buildRecordRows', () => {
  it('attaches pain log to session by exact id', () => {
    const logs: PainLog[] = [
      { sessionId: 's1', recordedAt: '2026-07-12T10:00:00Z', painBefore: 5, painAfter: 2 },
    ]
    const rows = buildRecordRows([session('s1', '2026-07-12T09:00:00Z')], logs)
    expect(rows).toHaveLength(1)
    expect(rows[0]).toMatchObject({ id: 's1', durationSec: 600, painBefore: 5, painAfter: 2 })
  })

  it('keeps unmatched pain log as standalone row', () => {
    const logs: PainLog[] = [
      { sessionId: 'gone', recordedAt: '2026-07-12T10:00:00Z', painBefore: 4, painAfter: 3 },
    ]
    const rows = buildRecordRows([], logs)
    expect(rows).toHaveLength(1)
    expect(rows[0]).toMatchObject({
      id: 'pain-2026-07-12T10:00:00Z-0',
      date: '2026-07-12T10:00:00Z',
      durationSec: null,
      painBefore: 4,
      painAfter: 3,
      source: 'manual',
    })
  })

  it('keeps legacy session pain fields when no log matches', () => {
    const s: Session = { ...session('old', '2026-07-01T09:00:00Z'), painAfter: 6, source: 'manual' }
    const rows = buildRecordRows([s], [])
    expect(rows[0]).toMatchObject({ id: 'old', painAfter: 6, source: 'manual' })
  })

  it('keeps second log as standalone row when two logs share a sessionId', () => {
    const logs: PainLog[] = [
      { sessionId: 's1', recordedAt: '2026-07-12T10:00:00Z', painBefore: 5, painAfter: 2 },
      { sessionId: 's1', recordedAt: '2026-07-12T11:00:00Z', painBefore: 7, painAfter: 4 },
    ]
    const rows = buildRecordRows([session('s1', '2026-07-12T09:00:00Z')], logs)
    expect(rows).toHaveLength(2)
    expect(rows[0]).toMatchObject({ id: 's1', durationSec: 600, painBefore: 5, painAfter: 2 })
    expect(rows[1]).toMatchObject({
      id: 'pain-2026-07-12T11:00:00Z-1',
      date: '2026-07-12T11:00:00Z',
      durationSec: null,
      painBefore: 7,
      painAfter: 4,
      source: 'manual',
    })
  })

  it('gives distinct ids to standalone logs with identical recordedAt', () => {
    const logs: PainLog[] = [
      { sessionId: 'gone1', recordedAt: '2026-07-12T10:00:00Z', painBefore: 5, painAfter: 2 },
      { sessionId: 'gone2', recordedAt: '2026-07-12T10:00:00Z', painBefore: 7, painAfter: 4 },
    ]
    const rows = buildRecordRows([], logs)
    expect(rows).toHaveLength(2)
    expect(rows[0].id).not.toBe(rows[1].id)
    expect(rows[0]).toMatchObject({
      date: '2026-07-12T10:00:00Z',
      durationSec: null,
      painBefore: 5,
      painAfter: 2,
      source: 'manual',
    })
    expect(rows[1]).toMatchObject({
      date: '2026-07-12T10:00:00Z',
      durationSec: null,
      painBefore: 7,
      painAfter: 4,
      source: 'manual',
    })
  })
})
