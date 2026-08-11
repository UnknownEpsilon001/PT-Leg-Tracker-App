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

export function createNativeNetworkStatusMonitor(
  network: NativeNetworkStatusSource,
  update: (connected: boolean) => void,
  fallback: () => void,
): () => void {
  let disposed = false
  let listener: NativeNetworkStatusListener | undefined

  void (async () => {
    try {
      const initialStatus = await network.getStatus()
      if (disposed) return
      update(initialStatus.connected)

      const resolvedListener = await network.addListener('networkStatusChange', (status) => {
        if (!disposed) update(status.connected)
      })
      listener = resolvedListener
      if (disposed) await listener.remove()
    } catch {
      if (!disposed) fallback()
    }
  })()

  return () => {
    disposed = true
    if (listener) void listener.remove()
  }
}
