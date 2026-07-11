import './assets/main.css'

import { createApp } from 'vue'
import { createPinia } from 'pinia'

import App from './App.vue'
import router from './router'
import { useProfileStore } from './stores/profile'
import { useSessionsStore } from './stores/sessions'

const app = createApp(App)
app.use(createPinia())

const profileStore = useProfileStore()
await profileStore.hydrate()

await useSessionsStore().hydrate()

app.use(router)
app.mount('#app')
