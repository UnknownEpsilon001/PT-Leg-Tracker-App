<script setup lang="ts">
import { painColor } from '@/lib/pain'

defineProps<{ modelValue: number | null }>()
defineEmits<{ 'update:modelValue': [value: number] }>()
</script>

<template>
  <div>
    <div class="scale">
      <button
        v-for="n in 11"
        :key="n - 1"
        :class="{ selected: modelValue === n - 1 }"
        :style="
          modelValue === n - 1
            ? { background: painColor(n - 1), borderColor: painColor(n - 1), color: '#fff' }
            : { borderColor: painColor(n - 1), color: painColor(n - 1) }
        "
        @click="$emit('update:modelValue', n - 1)"
      >
        {{ n - 1 }}
      </button>
    </div>
    <div class="ends">
      <span>ไม่ปวด</span>
      <span>ปวดมากที่สุด</span>
    </div>
  </div>
</template>

<style scoped>
.scale {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 0.5rem;
}

.scale button {
  min-height: 3.25rem;
  font-size: 1.25rem;
  font-weight: 700;
  border-width: 2px;
  background: var(--c-card);
}

.ends {
  display: flex;
  justify-content: space-between;
  margin-top: 0.5rem;
  color: var(--c-muted);
  font-size: 1rem;
}
</style>
