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
  // The project's current server. A saved empty value also falls back to it.
  // [TH] เซิร์ฟเวอร์ปัจจุบันของโครงการ ถ้าค่าที่บันทึกไว้ว่างก็ใช้ค่านี้
  serverUrl: 'https://med-flex-server-audio-room.tail325fc2.ts.net',
  deviceCode: 'default',
  lastSync: null,
  alarmEnabled: false,
  alarmTime: '09:00',
}

export const useSettingsStore = defineStore('settings', () => {
  const settings = ref<Settings>({ ...DEFAULTS })

  async function hydrate() {
    const saved = await loadJson<Partial<Settings>>(KEY, {})
    if (!saved.serverUrl) delete saved.serverUrl
    settings.value = { ...DEFAULTS, ...saved }
  }

  async function update(patch: Partial<Settings>) {
    settings.value = { ...settings.value, ...patch }
    await saveJson(KEY, settings.value)
  }

  return { settings, hydrate, update }
})
