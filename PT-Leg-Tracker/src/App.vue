<script setup lang="ts">
// คอมโพเนนต์ราก: แถบหัวเรื่องพร้อมปุ่มย้อนกลับ, สลับโหมดตัวอักษรใหญ่, และแสดงหน้าตาม router
// Root component: top bar with back button, large-font toggle, and the router outlet.
import { watchEffect } from 'vue'
import { RouterView, useRoute, useRouter } from 'vue-router'
import { useSettingsStore } from '@/stores/settings'
import AppIcon from '@/components/AppIcon.vue'

const route = useRoute()
const router = useRouter()
const settings = useSettingsStore()

// [TH] เปิด/ปิดคลาส font-large ที่ระดับราก ตามการตั้งค่าตัวอักษรใหญ่
watchEffect(() => {
  document.documentElement.classList.toggle('font-large', settings.settings.fontLarge)
})
</script>

<template>
  <div>
    <header class="topbar" v-if="route.name !== 'home' && route.name !== 'setup'">
      <button class="back" @click="router.back()" aria-label="ย้อนกลับ">
        <AppIcon name="chevron-left" /> กลับ
      </button>
      <h1>{{ route.meta.title ?? '' }}</h1>
    </header>
    <RouterView v-slot="{ Component }">
      <Transition name="page" mode="out-in">
        <component :is="Component" />
      </Transition>
    </RouterView>
  </div>
</template>

<style scoped>
.topbar {
  position: sticky;
  top: 0;
  z-index: 10;
  display: flex;
  align-items: center;
  gap: 0.75rem;
  padding: 0.5rem 1rem 0.5rem 0.5rem;
  background: var(--c-card);
  border-bottom: 1px solid var(--c-border);
}

.topbar h1 {
  font-size: 1.15rem;
  font-weight: 600;
  overflow-wrap: anywhere;
}

.back {
  flex-shrink: 0;
  background: transparent;
  border-color: transparent;
  color: var(--c-primary-dark);
  padding: 0.5rem 0.5rem 0.5rem 0.25rem;
}
</style>
