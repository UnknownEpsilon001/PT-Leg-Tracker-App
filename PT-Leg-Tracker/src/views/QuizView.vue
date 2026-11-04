<script setup lang="ts">
// หน้าแบบทดสอบความรู้ (ก่อน/หลัง/ความพึงพอใจ): ทำทีละข้อ ให้คะแนน และบันทึกผล
// Quiz screen (pre/post/satisfaction): answer per question, score, and save the result.
import { computed, onMounted, ref, watch } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { loadQuizContent, type QuizContent } from '@/lib/content'
import { scoreQuiz } from '@/lib/quiz'
import { useQuizStore } from '@/stores/quiz'

const route = useRoute()
const router = useRouter()
const store = useQuizStore()

const data = ref<QuizContent | null>(null)
const loading = ref(true)
const contentError = ref(false)
const quiz = computed(() => data.value?.quizzes.find((q) => q.id === route.params.quizId))
const qIndex = ref(0)
const answers = ref<number[]>([])
const done = ref(false)
const finalScore = ref(0)

onMounted(async () => {
  try {
    data.value = await loadQuizContent()
  } catch {
    contentError.value = true
  } finally {
    loading.value = false
  }
})

watch(
  () => route.params.quizId,
  () => {
    qIndex.value = 0
    answers.value = []
    done.value = false
    finalScore.value = 0
  },
)

let submitting = false // a double-tap on the last choice must not save twice

async function answer(choiceIndex: number) {
  if (!quiz.value || submitting) return
  answers.value[qIndex.value] = choiceIndex
  if (qIndex.value < quiz.value.questions.length - 1) {
    qIndex.value++
    return
  }
  submitting = true
  finalScore.value = scoreQuiz(quiz.value.questions as { correctIndex?: number }[], answers.value)
  try {
    await store.addResult({
      quizId: quiz.value.id,
      answers: [...answers.value],
      score: finalScore.value,
      timestamp: new Date().toISOString(),
    })
    done.value = true
  } finally {
    submitting = false
  }
}
</script>

<template>
  <main class="page">
    <p v-if="loading">กำลังโหลดเนื้อหา…</p>
    <p v-else-if="contentError" class="error">
      โหลดเนื้อหาไม่สำเร็จ กรุณาตรวจสอบ public/editable-content/README.md
    </p>
    <p v-else-if="!quiz">ไม่พบแบบทดสอบ</p>

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
        <h2>เสร็จเรียบร้อย ขอบคุณค่ะ</h2>
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

.error {
  color: #b91c1c;
  font-weight: 600;
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
