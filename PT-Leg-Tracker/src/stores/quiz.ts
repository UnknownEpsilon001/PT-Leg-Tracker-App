import { ref } from 'vue'
import { defineStore } from 'pinia'
import type { QuizResult } from '@/types'
import { loadJson, saveJson } from '@/lib/storage'

const KEY = 'quizResults'

export const useQuizStore = defineStore('quiz', () => {
  const results = ref<QuizResult[]>([])

  async function hydrate() {
    results.value = await loadJson<QuizResult[]>(KEY, [])
  }

  async function addResult(r: QuizResult) {
    results.value = [...results.value, r]
    await saveJson(KEY, results.value)
  }

  function hasTaken(quizId: string): boolean {
    return results.value.some((r) => r.quizId === quizId)
  }

  return { results, hydrate, addResult, hasTaken }
})
