import { beforeEach, describe, expect, it, vi } from 'vitest'

const store = new Map<string, string>()

vi.mock('@capacitor/preferences', () => ({
  Preferences: {
    get: vi.fn(async ({ key }: { key: string }) => ({ value: store.get(key) ?? null })),
    set: vi.fn(async ({ key, value }: { key: string; value: string }) => {
      store.set(key, value)
    }),
  },
}))

import { loadJson, saveJson } from './storage'

describe('storage', () => {
  beforeEach(() => store.clear())

  it('round-trips a value', async () => {
    await saveJson('k', { a: 1 })
    expect(await loadJson('k', { a: 0 })).toEqual({ a: 1 })
  })

  it('returns fallback when key missing', async () => {
    expect(await loadJson('missing', [])).toEqual([])
  })

  it('returns fallback on corrupt JSON instead of throwing', async () => {
    store.set('bad', '{not json')
    expect(await loadJson('bad', { ok: true })).toEqual({ ok: true })
  })
})
