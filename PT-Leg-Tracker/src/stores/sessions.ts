// สโตร์เก็บรายการเซสชันการออกกำลังกาย พร้อมบันทึกลงที่เก็บถาวรทุกครั้งที่เปลี่ยน
// Pinia store for exercise sessions; persists to device storage on every change.
import { ref } from 'vue'
import { defineStore } from 'pinia'
import type { Session } from '@/types'
import { loadJson, saveJson } from '@/lib/storage'

const KEY = 'sessions'  // คีย์ที่ใช้เก็บใน Preferences

export const useSessionsStore = defineStore('sessions', () => {
  const sessions = ref<Session[]>([])

  async function hydrate() {  // โหลดข้อมูลจากที่เก็บเข้าสู่ store ตอนเปิดแอป
    sessions.value = await loadJson<Session[]>(KEY, [])
  }

  async function add(s: Session) {
    sessions.value = [...sessions.value, s]
    await saveJson(KEY, sessions.value)
  }

  async function replaceAll(list: Session[]) {
    sessions.value = list
    await saveJson(KEY, list)
  }

  return { sessions, hydrate, add, replaceAll }
})
