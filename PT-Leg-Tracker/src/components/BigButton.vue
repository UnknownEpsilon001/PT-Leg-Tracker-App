<script setup lang="ts">
// ปุ่มเมนูขนาดใหญ่พร้อมไอคอน สำหรับหน้าแรก กดแล้วไปยังหน้าที่กำหนด (ออกแบบให้ผู้สูงอายุกดง่าย)
// Large icon menu button for the home screen; navigates to the given route.
import { useRouter } from 'vue-router'
import AppIcon from '@/components/AppIcon.vue'

const props = defineProps<{ icon: string; label: string; to: string }>()
const router = useRouter()
</script>

<template>
  <button class="big" @click="router.push({ name: props.to })">
    <span class="icon-wrap"><AppIcon :name="icon" /></span>
    <span class="label">{{ label }}</span>
  </button>
</template>

<style scoped>
.big {
  width: 100%;
  min-height: 5.5rem;
  display: flex;
  align-items: center;
  justify-content: flex-start;
  gap: 1rem;
  padding: 1rem 1.25rem;
  margin-bottom: 0.75rem;
  font-size: 1.2rem;
  font-weight: 600;
  text-align: left;
}

/* [TH] ป้ายยาว ๆ ต้องตัดบรรทัดได้ ไม่ล้นปุ่ม (ไทยไม่มีเว้นวรรคระหว่างคำ) */
/* Long Thai labels must wrap; Thai has no inter-word spaces. */
.label {
  overflow-wrap: anywhere;
  line-height: 1.35;
}

.icon-wrap {
  display: flex;
  flex-shrink: 0;
  align-items: center;
  justify-content: center;
  width: 3rem;
  height: 3rem;
  border-radius: 0.75rem;
  background: color-mix(in srgb, var(--c-primary) 12%, white);
  color: var(--c-primary-dark);
  font-size: 1.5rem;
}
</style>
