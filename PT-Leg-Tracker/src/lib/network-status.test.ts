import { describe, expect, it, vi } from 'vitest'
import { createNativeNetworkStatusMonitor } from './network-status'

describe('createNativeNetworkStatusMonitor', () => {
  it('uses the initial native status, receives changes, and removes its listener', async () => {
    const update = vi.fn()
    const remove = vi.fn().mockResolvedValue(undefined)
    let notify: (status: { connected: boolean }) => void
    const network = {
      getStatus: vi.fn().mockResolvedValue({ connected: false }),
      addListener: vi.fn((_event: 'networkStatusChange', listener: typeof notify) => {
        notify = listener
        return { remove }
      }),
    }

    const stop = createNativeNetworkStatusMonitor(network, update, vi.fn())

    await vi.waitFor(() => expect(update).toHaveBeenCalledWith(false))

    notify!({ connected: true })
    expect(update).toHaveBeenLastCalledWith(true)

    stop()
    await vi.waitFor(() => expect(remove).toHaveBeenCalledOnce())
  })

  it('falls back without blocking when native initialization rejects', async () => {
    const update = vi.fn()
    const fallback = vi.fn()
    const network = {
      getStatus: vi.fn().mockRejectedValue(new Error('native unavailable')),
      addListener: vi.fn(),
    }

    const stop = createNativeNetworkStatusMonitor(network, update, fallback)

    expect(stop).toBeTypeOf('function')
    await vi.waitFor(() => expect(fallback).toHaveBeenCalledOnce())
    expect(update).not.toHaveBeenCalled()
  })

  it('removes a delayed listener after disposal without updating disposed state', async () => {
    const update = vi.fn()
    const fallback = vi.fn()
    const remove = vi.fn().mockResolvedValue(undefined)
    let resolveListener: (listener: { remove: () => Promise<void> }) => void
    let notify: (status: { connected: boolean }) => void
    const listener = new Promise<{ remove: () => Promise<void> }>((resolve) => {
      resolveListener = resolve
    })
    const network = {
      getStatus: vi.fn().mockResolvedValue({ connected: true }),
      addListener: vi.fn((_event: 'networkStatusChange', callback: typeof notify) => {
        notify = callback
        return listener
      }),
    }

    const stop = createNativeNetworkStatusMonitor(network, update, fallback)
    await vi.waitFor(() => expect(network.addListener).toHaveBeenCalledOnce())
    stop()
    resolveListener!({ remove })
    await vi.waitFor(() => expect(remove).toHaveBeenCalledOnce())
    notify!({ connected: false })

    expect(update).toHaveBeenCalledTimes(1)
    expect(update).toHaveBeenLastCalledWith(true)
    expect(fallback).not.toHaveBeenCalled()
  })
})
