<script setup lang="ts">
import { computed, ref } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import data from '@/content/quizzes.json'
import { scoreQuiz } from '@/lib/quiz'
import { useQuizStore } from '@/stores/quiz'
import type { QuizResult } from '@/types'

const route = useRoute()
const router = useRouter()
const store = useQuizStore()

const quiz = computed(() => data.quizzes.find((q) => q.id === route.params.quizId))
const qIndex = ref(0)
const answers = ref<number[]>([])
const done = ref(false)
const finalScore = ref(0)

async function answer(choiceIndex: number) {
  if (!quiz.value) return
  answers.value[qIndex.value] = choiceIndex
  if (qIndex.value < quiz.value.questions.length - 1) {
    qIndex.value++
    return
  }
  finalScore.value = scoreQuiz(
    quiz.value.questions as { correctIndex?: number }[],
    answers.value,
  )
  await store.addResult({
    quizId: quiz.value.id as QuizResult['quizId'],
    answers: [...answers.value],
    score: finalScore.value,
    timestamp: new Date().toISOString(),
  })
  done.value = true
}
</script>

<template>
  <main class="page">
    <p v-if="!quiz">ไม่พบแบบทดสอบ</p>

    <template v-else-if="!done">
      <p class="progress">ข้อ {{ qIndex + 1 }} / {{ quiz.questions.length }}</p>
      <h2 class="card">{{ quiz!.questions[qIndex]!.text }}</h2>
      <button
        v-for="(choice, i) in quiz!.questions[qIndex]!.choices"
        :key="i"
        class="choice"
        @click="answer(i)"
      >
        {{ choice }}
      </button>
    </template>

    <template v-else>
      <div class="card center">
        <h2>เสร็จเรียบร้อย ขอบคุณค่ะ 🎉</h2>
        <p v-if="quiz.id !== 'satisfaction'">
          ได้ {{ finalScore }} จาก {{ quiz.questions.length }} คะแนน
        </p>
      </div>
      <button class="primary wide" @click="router.replace({ name: 'home' })">กลับหน้าหลัก</button>
    </template>
  </main>
</template>

<style scoped>
.progress {
  color: #57534e;
  margin-bottom: 0.5rem;
}

.choice {
  display: block;
  width: 100%;
  text-align: left;
  font-size: 1.1rem;
  margin-bottom: 0.75rem;
  min-height: 3.5rem;
}

.center {
  text-align: center;
}

.wide {
  width: 100%;
}
</style>
