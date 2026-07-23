// สโตร์เก็บค่าตั้งค่าของแอป (ฟอนต์ใหญ่ เซิร์ฟเวอร์ รหัสอุปกรณ์ การเตือน) พร้อมค่าตั้งต้น
// Pinia store for app settings, with defaults merged over any saved partial.
import { ref } from 'vue'
import { defineStore } from 'pinia'
import type { Settings } from '@/types'
import { loadJson, saveJson } from '@/lib/storage'

const KEY = 'settings'
// [TH] ค่าตั้งต้น ใช้เมื่อยังไม่เคยบันทึก หรือเติมฟิลด์ที่ขาดจากข้อมูลเดิม
export const DEFAULTS: Settings = {
  fontLarge: false,
  serverUrl: '',
  deviceCode: 'default',
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
