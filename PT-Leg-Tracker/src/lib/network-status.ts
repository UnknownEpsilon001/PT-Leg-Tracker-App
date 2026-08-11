export interface NativeNetworkStatus {
  connected: boolean
}

export interface NativeNetworkStatusListener {
  remove(): Promise<void> | void
}

export interface NativeNetworkStatusSource {
  getStatus(): Promise<NativeNetworkStatus>
  addListener(
    eventName: 'networkStatusChange',
    listener: (status: NativeNetworkStatus) => void,
  ): Promise<NativeNetworkStatusListener> | NativeNetworkStatusListener
}

export async function createNativeNetworkStatusMonitor(
  network: NativeNetworkStatusSource,
  update: (connected: boolean) => void,
): Promise<() => Promise<void>> {
  update((await network.getStatus()).connected)
  const listener = await network.addListener('networkStatusChange', (status) => {
    update(status.connected)
  })

  return async () => {
    await listener.remove()
  }
}
