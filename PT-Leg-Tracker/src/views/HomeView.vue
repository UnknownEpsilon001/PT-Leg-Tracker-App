<script setup lang="ts">
import BigButton from '@/components/BigButton.vue'
import { useProfileStore } from '@/stores/profile'
import { useQuizStore } from '@/stores/quiz'

const store = useProfileStore()
const quizStore = useQuizStore()
</script>

<template>
  <main class="page">
    <h1>สวัสดี คุณ{{ store.profile?.name }}</h1>
    <p class="sub">แอปดูแลข้อเข่าเสื่อม Smart OA Knee</p>
    <div v-if="!quizStore.hasTaken('pre')" class="card banner">
      <p>ก่อนเริ่มใช้งาน ทำแบบทดสอบก่อนใช้งานสั้น ๆ ก่อนนะคะ</p>
      <button class="primary" @click="$router.push({ name: 'quiz', params: { quizId: 'pre' } })">
        📝 ทำแบบทดสอบก่อนใช้งาน
      </button>
    </div>
    <BigButton icon="📖" label="ความรู้เรื่องข้อเข่าเสื่อม" to="knowledge" />
    <BigButton icon="🦵" label="วิธีใช้เครื่องบริหารเข่า" to="guide" />
    <BigButton icon="⏱️" label="เริ่มออกกำลังกาย" to="session" />
    <BigButton icon="📊" label="บันทึกของฉัน" to="records" />
    <button class="settings-link" @click="$router.push({ name: 'settings' })">⚙ ตั้งค่า</button>
  </main>
</template>

<style scoped>
.sub {
  margin-bottom: 1.5rem;
  color: #57534e;
}

.settings-link {
  width: 100%;
  margin-top: 1rem;
  border: none;
  background: transparent;
  color: var(--c-primary-dark);
  text-decoration: underline;
}
</style>
