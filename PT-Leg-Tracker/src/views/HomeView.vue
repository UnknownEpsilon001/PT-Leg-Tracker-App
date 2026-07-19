<script setup lang="ts">
import BigButton from '@/components/BigButton.vue'
import AppIcon from '@/components/AppIcon.vue'
import { useProfileStore } from '@/stores/profile'
import { useQuizStore } from '@/stores/quiz'
import logoUrl from '@/assets/logo.png'

const store = useProfileStore()
const quizStore = useQuizStore()
</script>

<template>
  <main class="page">
    <img :src="logoUrl" alt="ตราวิทยาลัยการอาชีพฝาง" class="logo" />
    <h1>สวัสดี คุณ{{ store.profile?.name }}</h1>
    <p class="sub">แอปดูแลข้อเข่าเสื่อม Smart OA Knee</p>
    <div v-if="!quizStore.hasTaken('pre')" class="card banner">
      <p>ก่อนเริ่มใช้งาน ทำแบบทดสอบก่อนใช้งานสั้น ๆ ก่อนนะคะ</p>
      <button class="primary" @click="$router.push({ name: 'quiz', params: { quizId: 'pre' } })">
        <AppIcon name="clipboard" /> ทำแบบทดสอบก่อนใช้งาน
      </button>
    </div>
    <BigButton icon="book-open" label="ความรู้เรื่องข้อเข่าเสื่อม" to="knowledge" />
    <BigButton icon="wrench" label="วิธีใช้เครื่องบริหารเข่า" to="guide" />
    <BigButton icon="activity" label="เริ่มออกกำลังกาย" to="session" />
    <BigButton icon="bar-chart" label="บันทึกของฉัน" to="records" />
    <button class="settings-link" @click="$router.push({ name: 'settings' })">
      <AppIcon name="settings" /> ตั้งค่า
    </button>
  </main>
</template>

<style scoped>
.logo {
  display: block;
  width: 11rem;
  max-width: 60%;
  margin: 0 auto 0.75rem;
}

@media (prefers-reduced-motion: no-preference) {
  .logo {
    animation: logo-rise 0.6s ease-out both;
  }

  @keyframes logo-rise {
    from {
      opacity: 0;
      transform: translateY(0.75rem);
    }
    to {
      opacity: 1;
      transform: none;
    }
  }
}

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

.banner {
  border-color: var(--c-primary);
}
</style>
