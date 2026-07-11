import { ref } from 'vue'
import { defineStore } from 'pinia'
import type { Session } from '@/types'
import { loadJson, saveJson } from '@/lib/storage'

const KEY = 'sessions'

export const useSessionsStore = defineStore('sessions', () => {
  const sessions = ref<Session[]>([])

  async function hydrate() {
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
