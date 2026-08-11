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

    const stop = await createNativeNetworkStatusMonitor(network, update)

    expect(update).toHaveBeenCalledWith(false)
    notify!({ connected: true })
    expect(update).toHaveBeenLastCalledWith(true)

    await stop()
    expect(remove).toHaveBeenCalledOnce()
  })
})
