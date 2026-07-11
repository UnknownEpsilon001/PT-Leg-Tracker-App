<script setup lang="ts">
import { ref, computed, onMounted, onUnmounted } from 'vue'
import guide from '@/content/deviceGuide.json'

const stepIndex = ref(0)
const online = ref(navigator.onLine)
const setOnline = () => (online.value = true)
const setOffline = () => (online.value = false)

onMounted(() => {
  window.addEventListener('online', setOnline)
  window.addEventListener('offline', setOffline)
})

onUnmounted(() => {
  window.removeEventListener('online', setOnline)
  window.removeEventListener('offline', setOffline)
})

const currentStep = computed(() => guide.steps[stepIndex.value]!)
</script>

<template>
  <main class="page">
    <div class="card step">
      <img :src="currentStep.image" :alt="currentStep.title" />
      <h2>{{ currentStep.title }}</h2>
      <p>{{ currentStep.caption }}</p>
      <div class="nav">
        <button :disabled="stepIndex === 0" @click="stepIndex--">← ก่อนหน้า</button>
        <span>{{ stepIndex + 1 }} / {{ guide.steps.length }}</span>
        <button :disabled="stepIndex === guide.steps.length - 1" @click="stepIndex++">
          ถัดไป →
        </button>
      </div>
    </div>

    <h2>วิดีโอสาธิต</h2>
    <p v-if="guide.videoIds.length === 0">ยังไม่มีวิดีโอ</p>
    <template v-else-if="online">
      <div v-for="id in guide.videoIds" :key="id" class="video">
        <iframe
          :src="`https://www.youtube.com/embed/${id}`"
          title="วิดีโอสาธิต"
          allowfullscreen
        ></iframe>
      </div>
    </template>
    <p v-else class="card">📶 วิดีโอต้องใช้อินเทอร์เน็ต กรุณาเชื่อมต่อแล้วลองใหม่</p>
  </main>
</template>

<style scoped>
.step img {
  width: 100%;
  border-radius: 0.75rem;
}

.nav {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-top: 0.75rem;
}

.video {
  position: relative;
  padding-top: 56.25%;
  margin-bottom: 0.75rem;
}

.video iframe {
  position: absolute;
  inset: 0;
  width: 100%;
  height: 100%;
  border: 0;
  border-radius: 0.75rem;
}
</style>
