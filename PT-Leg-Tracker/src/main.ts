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

void CapacitorApp.addListener('backButton', () => {
  const name = router.currentRoute.value.name
  if (name === 'home' || name === 'setup') void CapacitorApp.minimizeApp()
  else router.back()
})

void syncNow() // background; silent on failure
void syncAlarmFromSettings(settingsStore.settings)
