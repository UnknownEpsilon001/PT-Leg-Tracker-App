<script setup lang="ts">
// หน้าแรก: ทักทายผู้ป่วยและรวมเมนูหลัก (ความรู้ วิธีใช้เครื่อง ออกกำลังกาย บันทึก แบบทดสอบ ตั้งค่า)
// Home screen: greeting + main menu buttons.
import BigButton from '@/components/BigButton.vue'
import AppIcon from '@/components/AppIcon.vue'
import { useProfileStore } from '@/stores/profile'
import { useQuizStore } from '@/stores/quiz'
import { onMounted, ref } from 'vue'
import { loadAppContent, type AppContent } from '@/lib/content'

const store = useProfileStore()
const quizStore = useQuizStore()
const appContent = ref<AppContent | null>(null)
const loading = ref(true)
const contentError = ref(false)

onMounted(async () => {
  try {
    appContent.value = await loadAppContent()
  } catch {
    contentError.value = true
  } finally {
    loading.value = false
  }
})
</script>

<template>
  <main class="page">
    <p v-if="loading">กำลังโหลดเนื้อหา…</p>
    <p v-else-if="contentError" class="error">
      โหลดเนื้อหาไม่สำเร็จ กรุณาตรวจสอบ public/editable-content/README.md
    </p>
    <img v-if="appContent" :src="appContent.logo" :alt="appContent.logoAlt" class="logo" />
    <h1>สวัสดี คุณ{{ store.profile?.name }}</h1>
    <p v-if="appContent" class="sub">{{ appContent.homeSubtitle }}</p>
    <div v-if="!quizStore.hasTaken('pre')" class="card banner">
      <p>ก่อนเริ่มใช้งาน ทำแบบทดสอบก่อนใช้งานสั้น ๆ ก่อนนะคะ</p>
      <button class="primary" @click="$router.push({ name: 'quiz', params: { quizId: 'pre' } })">
        <AppIcon name="clipboard" /> ทำแบบทดสอบก่อนใช้งาน
      </button>
    </div>
    <BigButton
      class="stagger"
      style="--i: 0"
      icon="book-open"
      label="ความรู้เกี่ยวกับเรื่องข้อเข่าเสื่อมและการออกกำลังกายต้นขา"
      to="knowledge"
    />
    <BigButton
      class="stagger"
      style="--i: 1"
      icon="wrench"
      label="วิธีใช้เครื่องบริหารเข่า"
      to="guide"
    />
    <BigButton
      class="stagger"
      style="--i: 2"
      icon="activity"
      label="เริ่มออกกำลังกาย"
      to="session"
    />
    <BigButton class="stagger" style="--i: 3" icon="bar-chart" label="บันทึกของฉัน" to="records" />
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

.error {
  color: #b91c1c;
  font-weight: 600;
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
