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
    await queueClaim('s1')
    await queueClaim('s1')
    expect(pending()).toEqual(['s1'])
  })

  it('keeps failed claims, drops claimed and unknown ones', async () => {
    await queueClaim('ok')
    await queueClaim('down')
    await queueClaim('gone')
    vi.stubGlobal(
      'fetch',
      vi.fn(async (url: string) => {
        if (url.includes('/down/')) throw new TypeError('network')
        return res(url.includes('/gone/') ? 404 : 200)
      }),
    )
    await flushClaims('http://srv', 'P001')
    expect(pending()).toEqual(['down'])
  })
})
