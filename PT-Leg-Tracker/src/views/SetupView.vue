<script setup lang="ts">
import { ref } from 'vue'
import { useRouter } from 'vue-router'
import { useProfileStore } from '@/stores/profile'
import AppIcon from '@/components/AppIcon.vue'

const router = useRouter()
const store = useProfileStore()

const name = ref(store.profile?.name ?? '')
const age = ref<number | null>(store.profile?.age ?? null)
const patientCode = ref(store.profile?.patientCode ?? '')
const error = ref('')

async function submit() {
  if (!name.value.trim() || !age.value || !patientCode.value.trim()) {
    error.value = 'กรุณากรอกข้อมูลให้ครบทุกช่อง'
    return
  }
  try {
    await store.save({
      name: name.value.trim(),
      age: age.value,
      patientCode: patientCode.value.trim(),
      createdAt: store.profile?.createdAt ?? new Date().toISOString(),
    })
    router.replace({ name: 'home' })
  } catch {
    error.value = 'บันทึกไม่สำเร็จ กรุณาลองใหม่'
  }
}
</script>

<template>
  <main class="page">
    <h1>ลงทะเบียนผู้ใช้งาน</h1>
    <p>กรอกข้อมูลครั้งแรกเพียงครั้งเดียว</p>
    <div class="card">
      <label>ชื่อ-นามสกุล<input v-model="name" type="text" /></label>
      <label>อายุ (ปี)<input v-model.number="age" type="number" min="1" max="120" /></label>
      <label>รหัสผู้ป่วย (จากเจ้าหน้าที่)<input v-model="patientCode" type="text" /></label>
      <p v-if="error" class="error">{{ error }}</p>
      <button class="primary wide" @click="submit"><AppIcon name="check" /> บันทึก</button>
    </div>
  </main>
</template>

<style scoped>
label {
  display: block;
  margin-bottom: 1rem;
  font-weight: 600;
}

.error {
  color: #b91c1c;
  margin-bottom: 0.75rem;
}

.wide {
  width: 100%;
  font-size: 1.1rem;
}
</style>
