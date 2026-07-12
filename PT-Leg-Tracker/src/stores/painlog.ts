import { ref } from 'vue'
import { defineStore } from 'pinia'
import type { PainLog } from '@/types'
import { loadJson, saveJson } from '@/lib/storage'

const KEY = 'painlogs'

export const usePainLogStore = defineStore('painlog', () => {
  const painLogs = ref<PainLog[]>([])

  async function hydrate() {
    painLogs.value = await loadJson<PainLog[]>(KEY, [])
  }

  async function add(log: PainLog) {
    painLogs.value = [...painLogs.value, log]
    await saveJson(KEY, painLogs.value)
  }

  return { painLogs, hydrate, add }
})
