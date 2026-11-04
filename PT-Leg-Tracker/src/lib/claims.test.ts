// ทดสอบคิวผูกเซสชันกับผู้ป่วย: เก็บอันที่พลาดไว้ลองใหม่ ทิ้งอันที่เซิร์ฟเวอร์ไม่รู้จัก (claims.ts)
// Tests for the claim retry queue: failures stay queued, unknown sessions are dropped.
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'

const store = new Map<string, string>()

vi.mock('@capacitor/preferences', () => ({
  Preferences: {
    get: vi.fn(async ({ key }: { key: string }) => ({ value: store.get(key) ?? null })),
    set: vi.fn(async ({ key, value }: { key: string; value: string }) => {
      store.set(key, value)
    }),
  },
}))

import { flushClaims, queueClaim } from './claims'

const res = (status: number) => ({ ok: status < 400, status, json: async () => ({}) })
const pending = () => JSON.parse(store.get('pendingClaims') ?? '[]')

beforeEach(() => store.clear())
afterEach(() => vi.unstubAllGlobals())

describe('claims queue', () => {
  it('queues each session once', async () => {
    await queueClaim('s1', 'P001')
    await queueClaim('s1', 'P001')
    expect(pending()).toEqual([{ sessionId: 's1', patientCode: 'P001' }])
  })

  it('keeps failed claims, drops claimed and unknown ones', async () => {
    await queueClaim('ok', 'P001')
    await queueClaim('down', 'P001')
    await queueClaim('gone', 'P001')
    vi.stubGlobal(
      'fetch',
      vi.fn(async (url: string) => {
        if (url.includes('/down/')) throw new TypeError('network')
        return res(url.includes('/gone/') ? 404 : 200)
      }),
    )
    await flushClaims('http://srv')
    expect(pending()).toEqual([{ sessionId: 'down', patientCode: 'P001' }])
  })

  it('claims each session for the patient it was queued under', async () => {
    await queueClaim('a', 'OLD')
    await queueClaim('b', 'NEW')
    const fn = vi.fn(async (_url: string, _init: RequestInit) => res(200))
    vi.stubGlobal('fetch', fn)
    await flushClaims('http://srv')
    const codes = fn.mock.calls.map(([, init]) => JSON.parse(init.body as string).patientCode)
    expect(codes).toEqual(['OLD', 'NEW'])
  })
})
