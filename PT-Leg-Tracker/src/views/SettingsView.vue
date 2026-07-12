<script setup lang="ts">
import { ref } from 'vue'
import { useRouter } from 'vue-router'
import { useSettingsStore } from '@/stores/settings'
import { useProfileStore } from '@/stores/profile'
import { useSessionsStore } from '@/stores/sessions'
import { useQuizStore } from '@/stores/quiz'
import { buildExport, shareExport } from '@/lib/exporter'
import { syncNow } from '@/lib/api'

const router = useRouter()
const settings = useSettingsStore()
const profile = useProfileStore()
const sessions = useSessionsStore()
const quiz = useQuizStore()

const serverUrl = ref(settings.settings.serverUrl)
const syncing = ref(false)
const syncMessage = ref('')
const exportMessage = ref('')

async function toggleFont() {
  await settings.update({ fontLarge: !settings.settings.fontLarge })
}

async function saveServer() {
  await settings.update({ serverUrl: serverUrl.value.trim() })
  syncMessage.value = 'บันทึกแล้ว'
}

async function doSync() {
  syncing.value = true
  const ok = await syncNow()
  syncMessage.value = ok ? 'ดึงข้อมูลจากเครื่องสำเร็จ' : 'เชื่อมต่อไม่สำเร็จ ลองใหม่ภายหลัง'
  syncing.value = false
}

async function doExport() {
  try {
    const json = buildExport(profile.profile, sessions.sessions, quiz.results)
    await shareExport(json, profile.profile?.patientCode ?? 'unknown')
  } catch {
    exportMessage.value = 'ส่งออกไม่สำเร็จ'
  }
}

function fmtSync(iso: string | null) {
  return iso ? new Date(iso).toLocaleString('th-TH') : 'ยังไม่เคย'
}
</script>

<template>
  <main class="page">
    <div class="card">
      <h2>การแสดงผล</h2>
      <button class="wide" @click="toggleFont">
        🔠 {{ settings.settings.fontLarge ? 'ตัวหนังสือปกติ' : 'ตัวหนังสือใหญ่พิเศษ' }}
      </button>
    </div>

    <div class="card">
      <h2>แบบทดสอบ (สำหรับเจ้าหน้าที่)</h2>
      <button class="wide" @click="router.push({ name: 'quiz', params: { quizId: 'post' } })">
        📝 แบบทดสอบหลังใช้งาน
      </button>
      <button
        class="wide"
        @click="router.push({ name: 'quiz', params: { quizId: 'satisfaction' } })"
      >
        ⭐ แบบประเมินความพึงพอใจ
      </button>
    </div>

    <div class="card">
      <h2>ส่งออกข้อมูล</h2>
      <button class="primary wide" @click="doExport">📤 ส่งออกข้อมูล (JSON)</button>
      <p v-if="exportMessage">{{ exportMessage }}</p>
    </div>

    <div class="card">
      <h2>เซิร์ฟเวอร์ (สำหรับเจ้าหน้าที่)</h2>
      <label>ที่อยู่เซิร์ฟเวอร์<input v-model="serverUrl" type="url" placeholder="http://..." /></label>
      <button class="wide" @click="saveServer">💾 บันทึก</button>
      <button class="wide" :disabled="syncing" @click="doSync">
        {{ syncing ? 'กำลังดึงข้อมูล...' : '🔄 ดึงข้อมูลจากเครื่องบริหาร' }}
      </button>
      <p>ดึงข้อมูลล่าสุด: {{ fmtSync(settings.settings.lastSync) }}</p>
      <p v-if="syncMessage">{{ syncMessage }}</p>
    </div>

    <div class="card">
      <h2>ข้อมูลผู้ใช้</h2>
      <p>{{ profile.profile?.name }} (รหัส {{ profile.profile?.patientCode }})</p>
      <button class="wide" @click="router.push({ name: 'setup' })">✏️ แก้ไขข้อมูล</button>
    </div>
  </main>
</template>

<style scoped>
.wide {
  width: 100%;
  margin-bottom: 0.5rem;
}

label {
  display: block;
  margin-bottom: 0.5rem;
  font-weight: 600;
}
</style>
