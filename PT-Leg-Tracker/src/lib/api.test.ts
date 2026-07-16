import { afterEach, describe, expect, it, vi } from 'vitest'
import { fetchDeviceSessions } from './api'

afterEach(() => vi.unstubAllGlobals())

describe('fetchDeviceSessions', () => {
  it('queries /api/sessions and maps server fields to Session', async () => {
    const fn = vi.fn().mockResolvedValueOnce({
      ok: true,
      status: 200,
      json: async () => [
        {
          id: 's1',
          startedAt: '2026-07-16T01:00:00+00:00',
          endedAt: '2026-07-16T01:10:00+00:00',
          durationSec: 600,
          reps: 12,
          patientCode: 'P001',
        },
      ],
    })
    vi.stubGlobal('fetch', fn)
    await expect(fetchDeviceSessions('http://srv/', 'P001')).resolves.toEqual([
      { id: 's1', date: '2026-07-16T01:00:00+00:00', durationSec: 600, source: 'device' },
    ])
    expect(fn).toHaveBeenCalledWith('http://srv/api/sessions?patientCode=P001')
  })
  it('URL-encodes the patient code', async () => {
    const fn = vi.fn().mockResolvedValueOnce({ ok: true, status: 200, json: async () => [] })
    vi.stubGlobal('fetch', fn)
    await fetchDeviceSessions('http://srv', 'a b')
    expect(fn).toHaveBeenCalledWith('http://srv/api/sessions?patientCode=a%20b')
  })
  it('throws on HTTP error', async () => {
    vi.stubGlobal('fetch', vi.fn().mockResolvedValueOnce({ ok: false, status: 500 }))
    await expect(fetchDeviceSessions('http://srv', 'P001')).rejects.toThrow('HTTP 500')
  })
})
