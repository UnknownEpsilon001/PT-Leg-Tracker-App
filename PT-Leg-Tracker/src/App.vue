<script setup lang="ts">
import { watchEffect } from 'vue'
import { RouterView, useRoute, useRouter } from 'vue-router'
import { useSettingsStore } from '@/stores/settings'

const route = useRoute()
const router = useRouter()
const settings = useSettingsStore()

watchEffect(() => {
  document.documentElement.classList.toggle('font-large', settings.settings.fontLarge)
})
</script>

<template>
  <div>
    <header class="topbar" v-if="route.name !== 'home' && route.name !== 'setup'">
      <button class="back" @click="router.back()" aria-label="ย้อนกลับ">← กลับ</button>
      <h1>{{ route.meta.title ?? '' }}</h1>
    </header>
    <RouterView />
  </div>
</template>

<style scoped>
.topbar {
  display: flex;
  align-items: center;
  gap: 0.75rem;
  padding: 0.75rem 1rem;
  background: var(--c-primary);
  color: #fff;
}

.topbar h1 {
  font-size: 1.2rem;
  color: #fff;
}

.back {
  background: transparent;
  border: none;
  color: #fff;
  font-size: 1rem;
}
</style>
