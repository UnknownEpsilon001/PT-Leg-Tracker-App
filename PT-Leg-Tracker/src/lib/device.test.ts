// ทดสอบไคลเอนต์สั่งงานอุปกรณ์ผ่านเซิร์ฟเวอร์ และการจัดการข้อผิดพลาด (device.ts)
// Tests for the server-mediated device-control client and its error handling.
import { afterEach, describe, expect, it, vi } from 'vitest'
import {
  DeviceError,
  claimSession,
  getCurrent,
  queueStart,
  queueStop,
  setTarget,
  setTimings,
} from './device'

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

      const result = getCurrent('http://dev', 'D1')
      const assertion = expect(result).rejects.toMatchObject({ kind: 'unreachable' })
      await vi.advanceTimersByTimeAsync(5000)
      await assertion
      expect(fn).toHaveBeenCalledWith(
        'http://dev/api/sessions/current?deviceId=D1',
        expect.objectContaining({ signal: expect.any(AbortSignal) }),
      )
    } finally {
      vi.useRealTimers()
    }
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
    await expect(queueStart('http://srv/', 'D1')).resolves.toBeUndefined()
    expect(fn).toHaveBeenCalledWith(
      'http://srv/api/sessions/start?deviceId=D1',
      expect.objectContaining({ method: 'POST' }),
    )
  })
  it('throws busy on 409', async () => {
    mockFetchOnce(jsonRes(409, { detail: 'session already running' }))
    await expect(queueStart('http://srv', 'D1')).rejects.toMatchObject({ kind: 'busy' })
  })
  it('throws unreachable on network error', async () => {
    mockFetchOnce(new TypeError('failed'))
    await expect(queueStart('http://srv', 'D1')).rejects.toMatchObject({ kind: 'unreachable' })
  })
})

describe('setTarget', () => {
  it('POSTs the target and resolves on 202', async () => {
    const fn = mockFetchOnce(jsonRes(202, { queued: true }))
    await expect(setTarget('http://srv/', 'D1', 6)).resolves.toBeUndefined()
    expect(fn).toHaveBeenCalledWith(
      'http://srv/api/sessions/target?deviceId=D1',
      expect.objectContaining({
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ targetReps: 6 }),
      }),
    )
  })

  it('throws no-device when the device code is blank', async () => {
    await expect(setTarget('http://srv', '  ', 6)).rejects.toMatchObject({ kind: 'no-device' })
  })

  it('sends a target of 0 rather than treating it as unset', async () => {
    const fn = mockFetchOnce(jsonRes(202, { queued: true }))
    await expect(setTarget('http://srv/', 'D1', 0)).resolves.toBeUndefined()
    expect(fn).toHaveBeenCalledWith(
      'http://srv/api/sessions/target?deviceId=D1',
      expect.objectContaining({ body: JSON.stringify({ targetReps: 0 }) }),
    )
  })
})

describe('queueStart with a target', () => {
  it('sends the target in the body', async () => {
    const fn = mockFetchOnce(jsonRes(202, { queued: true }))
    await queueStart('http://srv', 'D1', 4)
    expect(fn).toHaveBeenCalledWith(
      'http://srv/api/sessions/start?deviceId=D1',
      expect.objectContaining({ body: JSON.stringify({ targetReps: 4 }) }),
    )
  })

  it('sends no body when no target is given', async () => {
    const fn = mockFetchOnce(jsonRes(202, { queued: true }))
    await queueStart('http://srv', 'D1')
    expect(fn).toHaveBeenCalledTimes(1)
    expect(fn.mock.calls[0]![1]).not.toHaveProperty('body')
  })

  it('sends a target of 0 (unlimited) rather than treating it as no target', async () => {
    const fn = mockFetchOnce(jsonRes(202, { queued: true }))
    await queueStart('http://srv', 'D1', 0)
    expect(fn).toHaveBeenCalledWith(
      'http://srv/api/sessions/start?deviceId=D1',
      expect.objectContaining({ body: JSON.stringify({ targetReps: 0 }) }),
    )
  })
})

describe('queueStop', () => {
  it('POSTs to /api/sessions/stop and resolves on 202', async () => {
    const fn = mockFetchOnce(jsonRes(202, { queued: true }))
    await expect(queueStop('http://srv/', 'D1')).resolves.toBeUndefined()
    expect(fn).toHaveBeenCalledWith(
      'http://srv/api/sessions/stop?deviceId=D1',
      expect.objectContaining({ method: 'POST' }),
    )
  })
  it('throws idle on 409', async () => {
    mockFetchOnce(jsonRes(409, { detail: 'no session running' }))
    await expect(queueStop('http://srv', 'D1')).rejects.toMatchObject({ kind: 'idle' })
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
    await expect(getCurrent('http://srv', 'D1')).resolves.toEqual({
      sessionId: 's1',
      state: 'running',
      elapsedSec: 42,
      reps: 7,
      deviceOnline: true,
      targetReps: 0,
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
    await expect(getCurrent('http://srv', 'D1')).resolves.toEqual({
      sessionId: null,
      state: 'starting',
      elapsedSec: 0,
      reps: 0,
      deviceOnline: false,
      targetReps: 0,
    })
  })
  it('throws unreachable on malformed state', async () => {
    mockFetchOnce(jsonRes(200, { state: 'weird' }))
    await expect(getCurrent('http://srv', 'D1')).rejects.toMatchObject({ kind: 'unreachable' })
  })
})

describe('getCurrent targetReps', () => {
  it('reads the target', async () => {
    mockFetchOnce(jsonRes(200, { state: 'idle', targetReps: 8 }))
    await expect(getCurrent('http://srv', 'D1')).resolves.toMatchObject({ targetReps: 8 })
  })

  it('defaults to 0 when the server omits it', async () => {
    mockFetchOnce(jsonRes(200, { state: 'idle' }))
    await expect(getCurrent('http://srv', 'D1')).resolves.toMatchObject({ targetReps: 0 })
  })
})

describe('setTimings', () => {
  it('POSTs only the fields it was given', async () => {
    const fn = mockFetchOnce(jsonRes(202, { queued: true }))
    await expect(setTimings('http://srv', 'D1', { liftSec: 3 })).resolves.toBeUndefined()
    expect(fn).toHaveBeenCalledWith(
      'http://srv/api/sessions/timings?deviceId=D1',
      expect.objectContaining({ body: JSON.stringify({ liftSec: 3 }) }),
    )
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

describe('deviceId scoping', () => {
  it('appends deviceId to control requests', async () => {
    const calls: string[] = []
    vi.stubGlobal('fetch', (url: string) => {
      calls.push(url)
      return Promise.resolve(new Response(JSON.stringify({ queued: true }), { status: 202 }))
    })
    await queueStart('http://s', 'KNEE-01')
    expect(calls[0]).toContain('/api/sessions/start?deviceId=KNEE-01')
  })

  it('throws no-device when device code is blank', async () => {
    const fetchSpy = vi.fn()
    vi.stubGlobal('fetch', fetchSpy)
    await expect(queueStart('http://s', '  ')).rejects.toMatchObject({ kind: 'no-device' })
    expect(fetchSpy).not.toHaveBeenCalled()
  })
})
