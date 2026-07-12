<script setup lang="ts">
import { ref } from 'vue'
import { useRouter } from 'vue-router'
import AppIcon from '@/components/AppIcon.vue'
import { enableDailyAlarm, cancelDailyAlarm } from '@/lib/alarm'
import { buildExport, shareExport } from '@/lib/exporter'
import { syncNow } from '@/lib/api'
import { usePainLogStore } from '@/stores/painlog'
import { useProfileStore } from '@/stores/profile'
import { useQuizStore } from '@/stores/quiz'
import { useSessionsStore } from '@/stores/sessions'
import { useSettingsStore } from '@/stores/settings'

const router = useRouter()
const settings = useSettingsStore()
const profile = useProfileStore()
const sessions = useSessionsStore()
const quiz = useQuizStore()
const painLog = usePainLogStore()

const serverUrl = ref(settings.settings.serverUrl)
const deviceUrl = ref(settings.settings.deviceUrl)
const alarmTime = ref(settings.settings.alarmTime)
const syncing = ref(false)
const syncMessage = ref('')
const exportMessage = ref('')
const alarmMessage = ref('')

async function toggleFont() {
  await settings.update({ fontLarge: !settings.settings.fontLarge })
}

async function saveServer() {
  await settings.update({ serverUrl: serverUrl.value.trim(), deviceUrl: deviceUrl.value.trim() })
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
    const json = buildExport(profile.profile, sessions.sessions, quiz.results, painLog.painLogs)
    await shareExport(json, profile.profile?.patientCode ?? 'unknown')
  } catch {
    exportMessage.value = 'ส่งออกไม่สำเร็จ'
  }
}

async function toggleAlarm() {
  alarmMessage.value = ''
  try {
    if (settings.settings.alarmEnabled) {
      await cancelDailyAlarm()
      await settings.update({ alarmEnabled: false })
    } else {
      const ok = await enableDailyAlarm(alarmTime.value)
      if (!ok) {
        alarmMessage.value = 'ไม่ได้รับอนุญาตให้แจ้งเตือน เปิดได้ในตั้งค่าเครื่อง'
        return
      }
      await settings.update({ alarmEnabled: true, alarmTime: alarmTime.value })
    }
  } catch {
    alarmMessage.value = 'ตั้งการแจ้งเตือนไม่สำเร็จ'
  }
}

async function changeAlarmTime() {
  if (!settings.settings.alarmEnabled) return
  try {
    const ok = await enableDailyAlarm(alarmTime.value)
    if (ok) await settings.update({ alarmTime: alarmTime.value })
  } catch {
    alarmMessage.value = 'ตั้งการแจ้งเตือนไม่สำเร็จ'
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
        <AppIcon name="text-size" />
        {{ settings.settings.fontLarge ? 'ตัวหนังสือปกติ' : 'ตัวหนังสือใหญ่พิเศษ' }}
      </button>
    </div>

    <div class="card">
      <h2>การแจ้งเตือน</h2>
      <label>เวลาแจ้งเตือนออกกำลังกาย<input v-model="alarmTime" type="time" @change="changeAlarmTime" /></label>
      <button class="wide" :class="{ primary: !settings.settings.alarmEnabled }" @click="toggleAlarm">
        <AppIcon name="bell" />
        {{ settings.settings.alarmEnabled ? 'ปิดการแจ้งเตือน' : 'เปิดการแจ้งเตือน' }}
      </button>
      <p v-if="alarmMessage">{{ alarmMessage }}</p>
    </div>

    <div class="card">
      <h2>แบบทดสอบ (สำหรับเจ้าหน้าที่)</h2>
      <button class="wide" @click="router.push({ name: 'quiz', params: { quizId: 'post' } })">
        <AppIcon name="clipboard" /> แบบทดสอบหลังใช้งาน
      </button>
      <button class="wide" @click="router.push({ name: 'quiz', params: { quizId: 'satisfaction' } })">
        <AppIcon name="star" /> แบบประเมินความพึงพอใจ
      </button>
    </div>

    <div class="card">
      <h2>ส่งออกข้อมูล</h2>
      <button class="primary wide" @click="doExport"><AppIcon name="share" /> ส่งออกข้อมูล (JSON)</button>
      <p v-if="exportMessage">{{ exportMessage }}</p>
    </div>

    <div class="card">
      <h2>เซิร์ฟเวอร์และเครื่องบริหาร (สำหรับเจ้าหน้าที่)</h2>
      <label>ที่อยู่เซิร์ฟเวอร์<input v-model="serverUrl" type="url" placeholder="http://..." /></label>
      <label>ที่อยู่เครื่องบริหาร<input v-model="deviceUrl" type="url" placeholder="http://192.168.x.x" /></label>
      <button class="wide" @click="saveServer"><AppIcon name="save" /> บันทึก</button>
      <button class="wide" :disabled="syncing" @click="doSync">
        <AppIcon name="refresh" />
        {{ syncing ? 'กำลังดึงข้อมูล...' : 'ดึงข้อมูลจากเครื่องบริหาร' }}
      </button>
      <p>ดึงข้อมูลล่าสุด: {{ fmtSync(settings.settings.lastSync) }}</p>
      <p v-if="syncMessage">{{ syncMessage }}</p>
    </div>

    <div class="card">
      <h2>ข้อมูลผู้ใช้</h2>
      <p>{{ profile.profile?.name }} (รหัส {{ profile.profile?.patientCode }})</p>
      <button class="wide" @click="router.push({ name: 'setup' })">
        <AppIcon name="pencil" /> แก้ไขข้อมูล
      </button>
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
