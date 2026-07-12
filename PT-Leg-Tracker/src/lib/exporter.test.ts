import { describe, expect, it, vi } from 'vitest'

vi.mock('@capacitor/filesystem', () => ({ Filesystem: {}, Directory: { Cache: 'CACHE' } }))
vi.mock('@capacitor/share', () => ({ Share: {} }))

import { buildExport } from './exporter'

describe('buildExport', () => {
  it('includes profile, sessions, results, and metadata', () => {
    const json = buildExport(
      { name: 'ทดสอบ', age: 60, patientCode: 'P01', createdAt: '2026-07-01T00:00:00Z' },
      [
        {
          id: 's1',
          date: '2026-07-02T10:00:00Z',
          durationSec: 600,
          painAfter: 3,
          source: 'manual',
        },
      ],
      [{ quizId: 'pre', answers: [0, 1], score: 2, timestamp: '2026-07-01T01:00:00Z' }],
      [],
    )
    const parsed = JSON.parse(json)
    expect(parsed.profile.patientCode).toBe('P01')
    expect(parsed.sessions).toHaveLength(1)
    expect(parsed.quizResults[0].score).toBe(2)
    expect(parsed.exportedAt).toBeTruthy()
  })

  it('handles null profile', () => {
    const parsed = JSON.parse(buildExport(null, [], [], []))
    expect(parsed.profile).toBeNull()
    expect(parsed.sessions).toEqual([])
  })

  it('includes painLogs in export', () => {
    const json = buildExport(null, [], [], [
      { sessionId: 's1', recordedAt: '2026-07-12T10:00:00Z', painBefore: 5, painAfter: 2 },
    ])
    const data = JSON.parse(json)
    expect(data.painLogs).toHaveLength(1)
    expect(data.painLogs[0].sessionId).toBe('s1')
  })
})
