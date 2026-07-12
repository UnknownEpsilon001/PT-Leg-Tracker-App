<script setup lang="ts">
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

.icon-wrap {
  display: flex;
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
