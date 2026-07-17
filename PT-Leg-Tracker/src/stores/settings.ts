import { ref } from 'vue'
import { defineStore } from 'pinia'
import type { Settings } from '@/types'
import { loadJson, saveJson } from '@/lib/storage'

const KEY = 'settings'
export const DEFAULTS: Settings = {
  fontLarge: false,
  serverUrl: '',
  lastSync: null,
  alarmEnabled: false,
  alarmTime: '09:00',
}

export const useSettingsStore = defineStore('settings', () => {
  const settings = ref<Settings>({ ...DEFAULTS })

  async function hydrate() {
    settings.value = { ...DEFAULTS, ...(await loadJson<Partial<Settings>>(KEY, {})) }
  }

  async function update(patch: Partial<Settings>) {
    settings.value = { ...settings.value, ...patch }
    await saveJson(KEY, settings.value)
  }

  return { settings, hydrate, update }
})
