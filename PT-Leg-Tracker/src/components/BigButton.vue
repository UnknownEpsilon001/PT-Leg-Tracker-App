<script setup lang="ts">
import { useRouter } from 'vue-router'

const props = defineProps<{ icon: string; label: string; to: string }>()
const router = useRouter()
</script>

<template>
  <button class="big" @click="router.push({ name: props.to })">
    <span class="icon">{{ icon }}</span>
    <span class="label">{{ label }}</span>
  </button>
</template>

<style scoped>
.big {
  width: 100%;
  min-height: 5.5rem;
  display: flex;
  align-items: center;
  gap: 1rem;
  padding: 1rem 1.25rem;
  margin-bottom: 0.75rem;
  font-size: 1.2rem;
  font-weight: 600;
  text-align: left;
}

.icon {
  font-size: 2rem;
}
</style>
