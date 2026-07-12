import { afterEach, describe, expect, it, vi } from 'vitest'
import { DeviceError, getStatus, startSession, stopSession } from './device'

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
