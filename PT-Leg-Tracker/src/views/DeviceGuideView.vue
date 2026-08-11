<script setup lang="ts">
// หน้าวิธีใช้เครื่อง: แนะนำการใช้งานทีละขั้น พร้อมแสดงสถานะออนไลน์/ออฟไลน์
// Device guide screen: step-by-step usage with an online/offline indicator.
import { computed, onMounted, onUnmounted, ref } from 'vue'
import { Capacitor } from '@capacitor/core'
import { Network } from '@capacitor/network'
import { loadDeviceGuide, type DeviceGuideContent } from '@/lib/content'
import { createNativeNetworkStatusMonitor } from '@/lib/network-status'

const stepIndex = ref(0)
const online = ref(navigator.onLine)
const guide = ref<DeviceGuideContent | null>(null)
const loading = ref(true)
const contentError = ref(false)
const setOnline = () => (online.value = true)
const setOffline = () => (online.value = false)
let stopNativeNetworkStatus: (() => void) | undefined

const currentStep = computed(() => guide.value?.steps[stepIndex.value] ?? null)

onMounted(() => {
  window.addEventListener('online', setOnline)
  window.addEventListener('offline', setOffline)

  if (Capacitor.isNativePlatform()) {
    stopNativeNetworkStatus = createNativeNetworkStatusMonitor(
      Network,
      (connected) => {
        online.value = connected
      },
      () => {
        online.value = navigator.onLine
      },
    )
  }

  void loadDeviceGuide()
    .then((content) => {
      guide.value = content
    })
    .catch(() => {
      contentError.value = true
    })
    .finally(() => {
      loading.value = false
    })
})

onUnmounted(() => {
  window.removeEventListener('online', setOnline)
  window.removeEventListener('offline', setOffline)
  stopNativeNetworkStatus?.()
})
</script>

<template>
  <main class="page">
    <p v-if="loading">กำลังโหลดเนื้อหา…</p>
    <p v-else-if="contentError" class="error">
      โหลดเนื้อหาไม่สำเร็จ กรุณาตรวจสอบ public/editable-content/README.md
    </p>
    <template v-else-if="guide && currentStep">
      <div class="card step">
        <img :src="currentStep.image" :alt="currentStep.title" />
        <h2>{{ currentStep.title }}</h2>
        <p>{{ currentStep.caption }}</p>
        <div class="nav">
          <button :disabled="stepIndex === 0" @click="stepIndex--">← ก่อนหน้า</button>
          <span>{{ stepIndex + 1 }} / {{ guide!.steps.length }}</span>
          <button :disabled="stepIndex === guide!.steps.length - 1" @click="stepIndex++">
            ถัดไป →
          </button>
        </div>
      </div>

      <h2>วิดีโอสาธิต</h2>
      <p v-if="guide.videos.length === 0">ยังไม่มีวิดีโอ</p>
      <template
        v-for="video in guide.videos"
        :key="video.type === 'youtube' ? `yt-${video.id}` : `file-${video.src}`"
      >
        <div v-if="video.type === 'youtube' && online" class="video">
          <iframe
            :src="`https://www.youtube.com/embed/${video.id}`"
            :title="video.title"
            allowfullscreen
          ></iframe>
        </div>
        <p v-else-if="video.type === 'youtube'" class="card">
          วิดีโอ YouTube ต้องใช้อินเทอร์เน็ต กรุณาเชื่อมต่อแล้วลองใหม่
        </p>
        <div v-else class="local-video">
          <video
            controls
            playsinline
            preload="metadata"
            :src="video.src"
            :title="video.title"
          ></video>
          <p>{{ video.title }}</p>
        </div>
      </template>
    </template>
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

.local-video {
  margin-bottom: 0.75rem;
}

.local-video video {
  display: block;
  width: 100%;
  border-radius: 0.75rem;
  background: #000;
}

.error {
  color: #b91c1c;
  font-weight: 600;
}
</style>
