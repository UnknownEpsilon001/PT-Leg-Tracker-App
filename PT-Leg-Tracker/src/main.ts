// จุดเริ่มต้นของแอป: โหลดฟอนต์/สไตล์, สร้าง Vue + Pinia, โหลดข้อมูลจากที่เก็บ, ต่อ router แล้ว mount
// App entry point: fonts/styles, create Vue + Pinia, hydrate stores, wire router, mount.
import '@fontsource/kanit/400.css'
import '@fontsource/kanit/600.css'
import '@fontsource/kanit/700.css'
import './assets/main.css'

import { createApp } from 'vue'
import { createPinia } from 'pinia'
import { App as CapacitorApp } from '@capacitor/app'

import App from './App.vue'
import router from './router'
import { useProfileStore } from './stores/profile'
import { useSessionsStore } from './stores/sessions'
import { useQuizStore } from './stores/quiz'
import { usePainLogStore } from './stores/painlog'
import { useSettingsStore } from './stores/settings'
import { syncNow } from './lib/api'
import { syncAlarmFromSettings } from './lib/alarm'
import { installTapSound } from './lib/tapSound'

const app = createApp(App)
app.use(createPinia())

const profileStore = useProfileStore()
await profileStore.hydrate()

await useSessionsStore().hydrate()

await useQuizStore().hydrate()

await usePainLogStore().hydrate()

const settingsStore = useSettingsStore()
await settingsStore.hydrate()

app.use(router)
app.mount('#app')
installTapSound()  // เสียงกดปุ่ม

// [TH] ปุ่มย้อนกลับของ Android: ที่หน้าแรก/ลงทะเบียนให้ย่อแอป หน้าอื่นให้ถอยกลับ
void CapacitorApp.addListener('backButton', () => {
  const name = router.currentRoute.value.name
  if (name === 'home' || name === 'setup') void CapacitorApp.minimizeApp()
  else router.back()
})

void syncNow() // background; silent on failure — ซิงก์เบื้องหลัง ล้มเหลวก็เงียบ
void syncAlarmFromSettings(settingsStore.settings)  // ตั้งการเตือนใหม่จากค่าที่บันทึกไว้
