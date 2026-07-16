import { afterEach, describe, expect, it, vi } from 'vitest'
import { DeviceError, getStatus, startSession, stopSession, claimSession, getCurrent, queueStart, queueStop } from './device'

function mockFetchOnce(response: Partial<Response> | Error) {
  const fn = vi.fn()
  if (response instanceof Error) fn.mockRejectedValueOnce(response)
  else fn.mockResolvedValueOnce(response as Response)
  vi.stubGlobal('fetch', fn)
  return fn
}

function jsonRes(status: number, body: unknown): Partial<Response> {
  return { ok: status >= 200 && status < 300, status, json: async () => body }
}

afterEach(() => vi.unstubAllGlobals())

describe('startSession', () => {
  it('returns sessionId on success', async () => {
    const fn = mockFetchOnce(jsonRes(200, { sessionId: 'abc' }))
    await expect(startSession('http://dev/')).resolves.toBe('abc')
    expect(fn).toHaveBeenCalledWith('http://dev/start', expect.objectContaining({ method: 'POST' }))
  })
  it('throws busy on 409', async () => {
    mockFetchOnce(jsonRes(409, {}))
    await expect(startSession('http://dev')).rejects.toMatchObject({ kind: 'busy' })
  })
  it('throws unreachable on network error', async () => {
    mockFetchOnce(new TypeError('failed'))
    await expect(startSession('http://dev')).rejects.toMatchObject({ kind: 'unreachable' })
  })
  it('throws unreachable when sessionId missing', async () => {
    mockFetchOnce(jsonRes(200, {}))
    await expect(startSession('http://dev')).rejects.toMatchObject({ kind: 'unreachable' })
  })
})

describe('getStatus', () => {
  it('parses a running status', async () => {
    mockFetchOnce(jsonRes(200, { state: 'running', elapsedSec: 42, sessionId: 's1' }))
    await expect(getStatus('http://dev')).resolves.toEqual({
      state: 'running',
      elapsedSec: 42,
      sessionId: 's1',
    })
  })
  it('throws unreachable on malformed state', async () => {
    mockFetchOnce(jsonRes(200, { state: 'weird' }))
    await expect(getStatus('http://dev')).rejects.toMatchObject({ kind: 'unreachable' })
  })
  it('throws unreachable when res.json() rejects', async () => {
    mockFetchOnce({ ok: true, status: 200, json: () => Promise.reject(new SyntaxError('bad json')) })
    await expect(getStatus('http://dev')).rejects.toMatchObject({ kind: 'unreachable' })
  })
})

describe('request timeout', () => {
  it('aborts and throws unreachable after 5s of no response', async () => {
    vi.useFakeTimers()
    try {
      const fn = vi.fn((_url: string, init?: RequestInit) => {
        return new Promise((_resolve, reject) => {
          init?.signal?.addEventListener('abort', () => {
            reject(new DOMException('The operation was aborted.', 'AbortError'))
          })
        })
      })
      vi.stubGlobal('fetch', fn)

      const result = getStatus('http://dev')
      const assertion = expect(result).rejects.toMatchObject({ kind: 'unreachable' })
      await vi.advanceTimersByTimeAsync(5000)
      await assertion
      expect(fn).toHaveBeenCalledWith(
        'http://dev/status',
        expect.objectContaining({ signal: expect.any(AbortSignal) }),
      )
    } finally {
      vi.useRealTimers()
    }
  })
})

describe('stopSession', () => {
  it('resolves on 200', async () => {
    mockFetchOnce(jsonRes(200, { ok: true }))
    await expect(stopSession('http://dev')).resolves.toBeUndefined()
  })
  it('throws idle on 409', async () => {
    mockFetchOnce(jsonRes(409, {}))
    await expect(stopSession('http://dev')).rejects.toMatchObject({ kind: 'idle' })
  })
})

describe('DeviceError', () => {
  it('is an Error with kind', () => {
    const e = new DeviceError('busy')
    expect(e).toBeInstanceOf(Error)
    expect(e.kind).toBe('busy')
  })
})

describe('queueStart', () => {
  it('POSTs to /api/sessions/start and resolves on 202', async () => {
    const fn = mockFetchOnce(jsonRes(202, { queued: true }))
    await expect(queueStart('http://srv/')).resolves.toBeUndefined()
    expect(fn).toHaveBeenCalledWith(
      'http://srv/api/sessions/start',
      expect.objectContaining({ method: 'POST' }),
    )
  })
  it('throws busy on 409', async () => {
    mockFetchOnce(jsonRes(409, { detail: 'session already running' }))
    await expect(queueStart('http://srv')).rejects.toMatchObject({ kind: 'busy' })
  })
  it('throws unreachable on network error', async () => {
    mockFetchOnce(new TypeError('failed'))
    await expect(queueStart('http://srv')).rejects.toMatchObject({ kind: 'unreachable' })
  })
})

describe('queueStop', () => {
  it('POSTs to /api/sessions/stop and resolves on 202', async () => {
    const fn = mockFetchOnce(jsonRes(202, { queued: true }))
    await expect(queueStop('http://srv/')).resolves.toBeUndefined()
    expect(fn).toHaveBeenCalledWith(
      'http://srv/api/sessions/stop',
      expect.objectContaining({ method: 'POST' }),
    )
  })
  it('throws idle on 409', async () => {
    mockFetchOnce(jsonRes(409, { detail: 'no session running' }))
    await expect(queueStop('http://srv')).rejects.toMatchObject({ kind: 'idle' })
  })
})

describe('getCurrent', () => {
  it('parses a running view', async () => {
    mockFetchOnce(
      jsonRes(200, {
        sessionId: 's1',
        state: 'running',
        elapsedSec: 42,
        reps: 7,
        deviceOnline: true,
      }),
    )
    await expect(getCurrent('http://srv')).resolves.toEqual({
      sessionId: 's1',
      state: 'running',
      elapsedSec: 42,
      reps: 7,
      deviceOnline: true,
    })
  })
  it('parses a starting view with null sessionId', async () => {
    mockFetchOnce(
      jsonRes(200, {
        sessionId: null,
        state: 'starting',
        elapsedSec: 0,
        reps: 0,
        deviceOnline: false,
      }),
    )
    await expect(getCurrent('http://srv')).resolves.toEqual({
      sessionId: null,
      state: 'starting',
      elapsedSec: 0,
      reps: 0,
      deviceOnline: false,
    })
  })
  it('throws unreachable on malformed state', async () => {
    mockFetchOnce(jsonRes(200, { state: 'weird' }))
    await expect(getCurrent('http://srv')).rejects.toMatchObject({ kind: 'unreachable' })
  })
})

describe('claimSession', () => {
  it('POSTs patientCode as JSON to the claim endpoint', async () => {
    const fn = mockFetchOnce(jsonRes(200, { ok: true }))
    await expect(claimSession('http://srv/', 's1', 'P001')).resolves.toBeUndefined()
    expect(fn).toHaveBeenCalledWith(
      'http://srv/api/sessions/s1/claim',
      expect.objectContaining({
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ patientCode: 'P001' }),
      }),
    )
  })
  it('throws unreachable on 404 (unknown session)', async () => {
    mockFetchOnce(jsonRes(404, { detail: 'unknown session' }))
    await expect(claimSession('http://srv', 'nope', 'P001')).rejects.toMatchObject({
      kind: 'unreachable',
    })
  })
})

describe('DeviceError offline kind', () => {
  it('accepts the offline kind', () => {
    expect(new DeviceError('offline').kind).toBe('offline')
  })
})
