import { describe, expect, it } from 'vitest'
import { mergeSessions } from './sessions'
import type { Session } from '@/types'

const mk = (id: string, date: string, source: Session['source']): Session => ({
  id,
  date,
  durationSec: 60,
  source,
})

describe('mergeSessions', () => {
  it('unions local and remote', () => {
    const merged = mergeSessions(
      [mk('a', '2026-07-01', 'manual')],
      [mk('b', '2026-07-02', 'device')],
    )
    expect(merged.map((s) => s.id)).toEqual(['a', 'b'])
  })

  it('dedupes by id, local wins', () => {
    const local = { ...mk('a', '2026-07-01', 'manual'), durationSec: 99 }
    const remote = { ...mk('a', '2026-07-01', 'device'), durationSec: 11 }
    const merged = mergeSessions([local], [remote])
    expect(merged).toHaveLength(1)
    expect(merged[0]!.durationSec).toBe(99)
  })

  it('sorts by date ascending', () => {
    const merged = mergeSessions(
      [mk('b', '2026-07-05', 'manual')],
      [mk('a', '2026-07-01', 'device')],
    )
    expect(merged.map((s) => s.id)).toEqual(['a', 'b'])
  })
})
