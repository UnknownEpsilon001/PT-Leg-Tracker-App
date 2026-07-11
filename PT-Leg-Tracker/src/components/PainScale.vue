<script setup lang="ts">
defineProps<{ modelValue: number | null }>()
defineEmits<{ 'update:modelValue': [value: number] }>()

const faces = ['😀', '😀', '🙂', '🙂', '😐', '😐', '😟', '😟', '😣', '😣', '😭']
</script>

<template>
  <div class="scale">
    <button
      v-for="n in 11"
      :key="n - 1"
      :class="{ selected: modelValue === n - 1 }"
      @click="$emit('update:modelValue', n - 1)"
    >
      <span class="face">{{ faces[n - 1] }}</span>
      <span>{{ n - 1 }}</span>
    </button>
  </div>
</template>

<style scoped>
.scale {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 0.5rem;
}

.scale button {
  display: flex;
  flex-direction: column;
  align-items: center;
  min-height: 4rem;
}

.scale button.selected {
  background: var(--c-primary);
  color: #fff;
  border-color: var(--c-primary-dark);
}

.face {
  font-size: 1.5rem;
}
</style>
