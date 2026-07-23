// อ่าน/เขียนข้อมูล JSON ลงที่เก็บถาวรของอุปกรณ์ผ่าน Capacitor Preferences
// Read/write JSON to device persistent storage via Capacitor Preferences.
import { Preferences } from '@capacitor/preferences'

// [TH] โหลด JSON ตามคีย์ ถ้าไม่มีหรืออ่านพลาดให้คืนค่าสำรอง (fallback)
export async function loadJson<T>(key: string, fallback: T): Promise<T> {
  try {
    const { value } = await Preferences.get({ key })
    if (value === null) return fallback
    return JSON.parse(value) as T
  } catch {
    return fallback
  }
}

// [TH] บันทึกค่าเป็นสตริง JSON ตามคีย์
export async function saveJson(key: string, value: unknown): Promise<void> {
  await Preferences.set({ key, value: JSON.stringify(value) })
}
