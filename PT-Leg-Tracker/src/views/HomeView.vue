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
      <p>ก่อนเริ่มใช้งาน ทำแบบทดสอบก่อนใช้งานสั้น ๆ ก่อน</p>
      <button class="primary" @click="$router.push({ name: 'quiz', params: { quizId: 'pre' } })">
        <AppIcon name="clipboard" /> ทำแบบทดสอบก่อนใช้งาน
      </button>
    </div>
    <BigButton
      icon="book-open"
      label="ความรู้เกี่ยวกับเรื่องข้อเข่าเสื่อมและการออกกำลังกายต้นขา"
      to="knowledge"
    />
    <BigButton icon="wrench" label="วิธีใช้เครื่องบริหารเข่า" to="guide" />
    <BigButton class="primary" icon="activity" label="เริ่มออกกำลังกาย" to="session" />
    <BigButton icon="bar-chart" label="บันทึกของฉัน" to="records" />
    <button class="settings-link" @click="$router.push({ name: 'settings' })">
      <AppIcon name="settings" /> ตั้งค่า
    </button>
  </main>
</template>

<style scoped>
.logo {
  display: block;
  width: 9rem;
  max-width: 50%;
  margin: 0.5rem auto 1.25rem;
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

h1 {
  font-size: 1.6rem;
}

.sub {
  margin: 0.25rem 0 1.5rem;
  color: var(--c-muted);
}

.settings-link {
  width: 100%;
  margin-top: 0.75rem;
  border-color: transparent;
  background: transparent;
  color: var(--c-primary-dark);
}

.banner {
  border-bottom: 1px solid var(--c-border);
  margin-bottom: 1.25rem;
}

.banner p {
  margin-bottom: 0.75rem;
}

.banner button {
  width: 100%;
}
</style>
