<script setup lang="ts">
// ปุ่มปรับจำนวนเป้าหมาย (−/+) ใช้ทั้งก่อนเริ่มและระหว่างเครื่องทำงานในหน้าออกกำลังกาย
// Target stepper (−/+) shared by the pre-start and running phases of SessionView,
// and by its start-confirmation sheet, which shows the count without a prefix and
// names it เซ็ต for screen readers.
// null = target unknown, so both buttons stay disabled; bounds are [0, 99].
withDefaults(defineProps<{ value: number | null; label: string; prefix?: string; noun?: string }>(), {
  prefix: 'เป้าหมาย ',
  noun: 'เป้าหมาย',
})
defineEmits<{ step: [delta: number] }>()
</script>

<template>
  <div class="target-stepper">
    <button
      class="step"
      :aria-label="`ลดจำนวน${noun}`"
      :disabled="value === null || value === 0"
      @click="$emit('step', -1)"
    >
      −
    </button>
    <span class="target-label">{{ prefix }}{{ label }}</span>
    <button
      class="step"
      :aria-label="`เพิ่มจำนวน${noun}`"
      :disabled="value === null || value === 99"
      @click="$emit('step', 1)"
    >
      +
    </button>
  </div>
</template>

<style scoped>
.target-stepper {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 0.75rem;
  margin-bottom: 1rem;
}

.target-label {
  min-width: 8rem;
  text-align: center;
  font-weight: 600;
}

.step {
  min-width: 3rem;
  font-size: 1.3rem;
  padding: 0.5rem;
}
</style>
