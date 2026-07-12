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
