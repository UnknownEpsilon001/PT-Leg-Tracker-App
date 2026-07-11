# Smart OA Knee App Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the offline-first Thai Android app for knee-OA patients: knowledge content, device guide, guided therapy sessions with pain tracking, pre/post quizzes, data export, and (stubbed) server sync.

**Architecture:** Vue 3 SPA (Pinia stores + Vue Router) persisted to Capacitor Preferences as JSON, wrapped by Capacitor 8 for Android. Logic (storage, scoring, merge, export) lives in plain TS modules unit-tested with Vitest; screens are thin Vue components verified by `npm run build` and manual dev-server checks.

**Tech Stack:** Vue 3.5, TypeScript ~6.0, Vite 8, Pinia 3, Vue Router 5, Capacitor 8 (`@capacitor/preferences`, `@capacitor/filesystem`, `@capacitor/share`), Vitest.

## Global Constraints

- All commands run from `PT-Leg-Tracker/` (NOT repo root).
- Spec: `docs/superpowers/specs/2026-07-11-smart-oa-knee-app-design.md`.
- UI language: Thai only. Elderly UI: body font ≥ 20px, touch targets ≥ 48dp (use ≥ 3rem), max 4 primary menu items per screen, every button = icon + Thai label.
- Offline-first: every feature except YouTube playback and server sync must work with no network.
- App must never crash on malformed/missing stored data — fall back to empty defaults.
- One patient per phone; no login.
- Commit after every task. Never add Co-Authored-By lines.
- Sample content (articles, quiz questions, guide photos, YouTube IDs) is REPLACEABLE DATA the researcher/PT will swap later — structure is what matters. Placeholder guide images are deliberate; real photos arrive later.

---

## File Structure

```
PT-Leg-Tracker/
  src/
    types.ts                    # shared data types (Profile, Session, QuizResult…)
    lib/
      storage.ts                # JSON load/save over Capacitor Preferences
      quiz.ts                   # scoring logic (pure)
      sessions.ts               # merge/dedupe logic (pure)
      exporter.ts               # export serialization (pure) + share glue
      api.ts                    # server fetch (sync)
    stores/
      profile.ts                # patient profile store
      sessions.ts               # session log store
      quiz.ts                   # quiz results store
      settings.ts               # font size, server URL, last sync
    content/
      knowledge.json            # OA articles + self-care manual sections
      deviceGuide.json          # step cards + video IDs
      quizzes.json              # pre / post / satisfaction question sets
    views/
      SetupView.vue             # first-launch profile
      HomeView.vue              # 4 big buttons
      KnowledgeView.vue         # article lists (knowledge + self-care)
      ArticleView.vue           # single article
      DeviceGuideView.vue       # step cards + YouTube
      SessionView.vue           # guided session (timer → pain → save)
      RecordsView.vue           # history + chart
      QuizView.vue              # quiz engine UI
      SettingsView.vue          # export, profile edit, server URL, font size
    components/
      BigButton.vue             # reusable large menu button
      PainScale.vue             # 0–10 face scale picker
      TrendChart.vue            # pure-SVG line chart
    tests/ (colocated as *.test.ts next to lib modules)
```

---

### Task 1: Scaffold removal, theme, app shell

**Files:**
- Delete: `src/components/HelloWorld.vue`, `src/components/TheWelcome.vue`, `src/components/WelcomeItem.vue`, `src/components/icons/` (whole dir), `src/views/AboutView.vue`, `src/stores/counter.ts`, `src/assets/base.css`, `src/assets/logo.svg`
- Modify: `src/App.vue`, `src/assets/main.css`, `src/router/index.ts`, `src/views/HomeView.vue`
- Test: none (verified by build)

**Interfaces:**
- Consumes: existing Vite/Capacitor scaffold.
- Produces: CSS custom properties `--c-bg`, `--c-text`, `--c-primary`, `--c-primary-dark`, `--c-card`, `--c-border`; class `font-large` on `#app` scales fonts; empty-but-building app shell with router.

- [ ] **Step 1: Delete scaffold files**

```bash
cd PT-Leg-Tracker
git rm src/components/HelloWorld.vue src/components/TheWelcome.vue src/components/WelcomeItem.vue src/views/AboutView.vue src/stores/counter.ts src/assets/base.css src/assets/logo.svg
git rm -r src/components/icons
```

- [ ] **Step 2: Replace `src/assets/main.css` with the theme**

```css
:root {
  --c-bg: #f7f5f0;
  --c-text: #1f2937;
  --c-primary: #0e7490;
  --c-primary-dark: #155e75;
  --c-card: #ffffff;
  --c-border: #d6d3cd;
}

* {
  box-sizing: border-box;
  margin: 0;
}

html {
  font-size: 20px; /* elderly baseline; everything below uses rem */
}

#app.font-large {
  font-size: 1.2em;
}

body {
  background: var(--c-bg);
  color: var(--c-text);
  font-family: 'Sarabun', 'Noto Sans Thai', system-ui, sans-serif;
  line-height: 1.6;
  -webkit-tap-highlight-color: transparent;
}

h1 {
  font-size: 1.5rem;
}

h2 {
  font-size: 1.25rem;
}

button {
  font-family: inherit;
  font-size: 1rem;
  min-height: 3rem; /* ≥48dp touch target */
  border-radius: 0.75rem;
  border: 1px solid var(--c-border);
  background: var(--c-card);
  color: var(--c-text);
  padding: 0.5rem 1rem;
  cursor: pointer;
}

button.primary {
  background: var(--c-primary);
  border-color: var(--c-primary-dark);
  color: #fff;
}

input,
select {
  font-family: inherit;
  font-size: 1rem;
  min-height: 3rem;
  border-radius: 0.75rem;
  border: 1px solid var(--c-border);
  padding: 0.5rem 0.75rem;
  width: 100%;
  background: var(--c-card);
  color: var(--c-text);
}

.page {
  max-width: 40rem;
  margin: 0 auto;
  padding: 1rem;
  padding-bottom: 3rem;
}

.card {
  background: var(--c-card);
  border: 1px solid var(--c-border);
  border-radius: 1rem;
  padding: 1rem;
  margin-bottom: 0.75rem;
}
```

- [ ] **Step 3: Replace `src/App.vue`**

```vue
<script setup lang="ts">
import { RouterView, useRoute, useRouter } from 'vue-router'

const route = useRoute()
const router = useRouter()
</script>

<template>
  <header class="topbar" v-if="route.name !== 'home' && route.name !== 'setup'">
    <button class="back" @click="router.back()" aria-label="ย้อนกลับ">← กลับ</button>
    <h1>{{ route.meta.title ?? '' }}</h1>
  </header>
  <RouterView />
</template>

<style scoped>
.topbar {
  display: flex;
  align-items: center;
  gap: 0.75rem;
  padding: 0.75rem 1rem;
  background: var(--c-primary);
  color: #fff;
}

.topbar h1 {
  font-size: 1.2rem;
  color: #fff;
}

.back {
  background: transparent;
  border: none;
  color: #fff;
  font-size: 1rem;
}
</style>
```

- [ ] **Step 4: Reset `src/router/index.ts` to home-only (routes grow per task)**

```ts
import { createRouter, createWebHistory } from 'vue-router'
import HomeView from '../views/HomeView.vue'

const router = createRouter({
  history: createWebHistory(import.meta.env.BASE_URL),
  routes: [
    {
      path: '/',
      name: 'home',
      component: HomeView,
    },
  ],
})

export default router
```

- [ ] **Step 5: Replace `src/views/HomeView.vue` with a minimal stub (real home in Task 4)**

```vue
<template>
  <main class="page">
    <h1>Smart OA Knee</h1>
  </main>
</template>
```

- [ ] **Step 6: Verify build passes**

Run: `npm run build`
Expected: vue-tsc + vite build succeed, no errors.

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "feat: replace scaffold with elderly-friendly app shell and theme"
```

---

### Task 2: Vitest setup, shared types, storage module

**Files:**
- Modify: `PT-Leg-Tracker/package.json` (test script), `PT-Leg-Tracker/vite.config.ts` (vitest config)
- Create: `src/types.ts`, `src/lib/storage.ts`, `src/lib/storage.test.ts`

**Interfaces:**
- Consumes: `@capacitor/preferences` (new dependency).
- Produces:
  - `types.ts`: `Profile { name: string; age: number; patientCode: string; createdAt: string }`, `Session { id: string; date: string; durationSec: number; painBefore?: number; painAfter: number; source: 'manual' | 'device' }`, `QuizResult { quizId: 'pre' | 'post' | 'satisfaction'; answers: number[]; score: number; timestamp: string }`, `Settings { fontLarge: boolean; serverUrl: string; lastSync: string | null }`
  - `storage.ts`: `loadJson<T>(key: string, fallback: T): Promise<T>`, `saveJson(key: string, value: unknown): Promise<void>`

- [ ] **Step 1: Install dependencies**

```bash
cd PT-Leg-Tracker
npm install @capacitor/preferences @capacitor/filesystem @capacitor/share
npm install -D vitest
```

- [ ] **Step 2: Add test script to `package.json` scripts block**

```json
"test": "vitest run",
"test:watch": "vitest"
```

- [ ] **Step 3: Add vitest config to `vite.config.ts`** — add to the exported config object (add the triple-slash reference at the top of the file):

```ts
/// <reference types="vitest/config" />
// ...existing imports and config...
export default defineConfig({
  // ...existing plugins etc...
  test: {
    environment: 'node',
    include: ['src/**/*.test.ts'],
  },
})
```

- [ ] **Step 4: Create `src/types.ts`**

```ts
export interface Profile {
  name: string
  age: number
  patientCode: string
  createdAt: string
}

export interface Session {
  id: string
  date: string // ISO date-time
  durationSec: number
  painBefore?: number
  painAfter: number
  source: 'manual' | 'device'
}

export interface QuizResult {
  quizId: 'pre' | 'post' | 'satisfaction'
  answers: number[]
  score: number
  timestamp: string
}

export interface Settings {
  fontLarge: boolean
  serverUrl: string
  lastSync: string | null
}
```

- [ ] **Step 5: Write failing test `src/lib/storage.test.ts`**

```ts
import { beforeEach, describe, expect, it, vi } from 'vitest'

const store = new Map<string, string>()

vi.mock('@capacitor/preferences', () => ({
  Preferences: {
    get: vi.fn(async ({ key }: { key: string }) => ({ value: store.get(key) ?? null })),
    set: vi.fn(async ({ key, value }: { key: string; value: string }) => {
      store.set(key, value)
    }),
  },
}))

import { loadJson, saveJson } from './storage'

describe('storage', () => {
  beforeEach(() => store.clear())

  it('round-trips a value', async () => {
    await saveJson('k', { a: 1 })
    expect(await loadJson('k', { a: 0 })).toEqual({ a: 1 })
  })

  it('returns fallback when key missing', async () => {
    expect(await loadJson('missing', [])).toEqual([])
  })

  it('returns fallback on corrupt JSON instead of throwing', async () => {
    store.set('bad', '{not json')
    expect(await loadJson('bad', { ok: true })).toEqual({ ok: true })
  })
})
```

- [ ] **Step 6: Run test to verify it fails**

Run: `npm test`
Expected: FAIL — `Cannot find module './storage'` (or equivalent).

- [ ] **Step 7: Create `src/lib/storage.ts`**

```ts
import { Preferences } from '@capacitor/preferences'

export async function loadJson<T>(key: string, fallback: T): Promise<T> {
  try {
    const { value } = await Preferences.get({ key })
    if (value === null) return fallback
    return JSON.parse(value) as T
  } catch {
    return fallback
  }
}

export async function saveJson(key: string, value: unknown): Promise<void> {
  await Preferences.set({ key, value: JSON.stringify(value) })
}
```

- [ ] **Step 8: Run tests to verify they pass**

Run: `npm test`
Expected: 3 passed.

- [ ] **Step 9: Sync Capacitor plugins into Android project**

```bash
npm run build
npx cap sync android
```

Expected: sync reports `@capacitor/preferences`, `@capacitor/filesystem`, `@capacitor/share` found.

- [ ] **Step 10: Commit**

```bash
git add -A
git commit -m "feat: add vitest, shared types, and Preferences-backed storage module"
```

---

### Task 3: Profile store, first-launch setup screen, route guard

**Files:**
- Create: `src/stores/profile.ts`, `src/views/SetupView.vue`
- Modify: `src/router/index.ts` (setup route + guard), `src/main.ts` (hydrate before mount)

**Interfaces:**
- Consumes: `loadJson`/`saveJson` from `src/lib/storage.ts`, `Profile` from `src/types.ts`.
- Produces: `useProfileStore()` with `profile: Profile | null`, `hydrate(): Promise<void>`, `save(p: Profile): Promise<void>`, `hasProfile: boolean` (computed). Router guard: no profile → redirect `{ name: 'setup' }`.

- [ ] **Step 1: Create `src/stores/profile.ts`**

```ts
import { computed, ref } from 'vue'
import { defineStore } from 'pinia'
import type { Profile } from '@/types'
import { loadJson, saveJson } from '@/lib/storage'

const KEY = 'profile'

export const useProfileStore = defineStore('profile', () => {
  const profile = ref<Profile | null>(null)
  const hasProfile = computed(() => profile.value !== null)

  async function hydrate() {
    profile.value = await loadJson<Profile | null>(KEY, null)
  }

  async function save(p: Profile) {
    profile.value = p
    await saveJson(KEY, p)
  }

  return { profile, hasProfile, hydrate, save }
})
```

- [ ] **Step 2: Create `src/views/SetupView.vue`**

```vue
<script setup lang="ts">
import { ref } from 'vue'
import { useRouter } from 'vue-router'
import { useProfileStore } from '@/stores/profile'

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
  await store.save({
    name: name.value.trim(),
    age: age.value,
    patientCode: patientCode.value.trim(),
    createdAt: store.profile?.createdAt ?? new Date().toISOString(),
  })
  router.replace({ name: 'home' })
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
      <button class="primary wide" @click="submit">✔ บันทึก</button>
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
```

- [ ] **Step 3: Add route + guard in `src/router/index.ts`**

```ts
import { createRouter, createWebHistory } from 'vue-router'
import HomeView from '../views/HomeView.vue'
import { useProfileStore } from '@/stores/profile'

const router = createRouter({
  history: createWebHistory(import.meta.env.BASE_URL),
  routes: [
    { path: '/', name: 'home', component: HomeView },
    {
      path: '/setup',
      name: 'setup',
      component: () => import('../views/SetupView.vue'),
      meta: { title: 'ลงทะเบียน' },
    },
  ],
})

router.beforeEach((to) => {
  const store = useProfileStore()
  if (!store.hasProfile && to.name !== 'setup') return { name: 'setup' }
})

export default router
```

- [ ] **Step 4: Hydrate stores before mount in `src/main.ts`**

```ts
import './assets/main.css'

import { createApp } from 'vue'
import { createPinia } from 'pinia'

import App from './App.vue'
import router from './router'
import { useProfileStore } from './stores/profile'

const app = createApp(App)
app.use(createPinia())

const profileStore = useProfileStore()
await profileStore.hydrate()

app.use(router)
app.mount('#app')
```

(Top-level await is fine: Vite targets modern ES modules.)

- [ ] **Step 5: Verify in dev server**

Run: `npm run dev`, open browser.
Expected: redirected to `/setup`; submitting valid data lands on home stub; reload skips setup (profile persisted to localStorage-backed Preferences on web).

- [ ] **Step 6: Verify build passes**

Run: `npm run build`
Expected: success.

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "feat: add profile store, first-launch setup screen, and route guard"
```

---

### Task 4: Home screen + BigButton component

**Files:**
- Create: `src/components/BigButton.vue`
- Modify: `src/views/HomeView.vue`

**Interfaces:**
- Consumes: `useProfileStore` (greeting).
- Produces: `BigButton` props `{ icon: string; label: string; to: string }` (route name). Home links to route names `knowledge`, `guide`, `session`, `records`, and a small link to `settings` — these routes are added in Tasks 5–8 and 11 (links 404 until then; acceptable mid-plan).

- [ ] **Step 1: Create `src/components/BigButton.vue`**

```vue
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
```

- [ ] **Step 2: Replace `src/views/HomeView.vue`**

```vue
<script setup lang="ts">
import BigButton from '@/components/BigButton.vue'
import { useProfileStore } from '@/stores/profile'

const store = useProfileStore()
</script>

<template>
  <main class="page">
    <h1>สวัสดี คุณ{{ store.profile?.name }}</h1>
    <p class="sub">แอปดูแลข้อเข่าเสื่อม Smart OA Knee</p>
    <BigButton icon="📖" label="ความรู้เรื่องข้อเข่าเสื่อม" to="knowledge" />
    <BigButton icon="🦵" label="วิธีใช้เครื่องบริหารเข่า" to="guide" />
    <BigButton icon="⏱️" label="เริ่มออกกำลังกาย" to="session" />
    <BigButton icon="📊" label="บันทึกของฉัน" to="records" />
    <button class="settings-link" @click="$router.push({ name: 'settings' })">⚙ ตั้งค่า</button>
  </main>
</template>

<style scoped>
.sub {
  margin-bottom: 1.5rem;
  color: #57534e;
}

.settings-link {
  width: 100%;
  margin-top: 1rem;
  border: none;
  background: transparent;
  color: var(--c-primary-dark);
  text-decoration: underline;
}
</style>
```

- [ ] **Step 3: Verify build**

Run: `npm run build`
Expected: success. Dev-server check: home shows greeting + 4 big buttons + settings link.

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "feat: add home screen with four-button elderly menu"
```

---

### Task 5: Knowledge content + Knowledge/Article screens

**Files:**
- Create: `src/content/knowledge.json`, `src/views/KnowledgeView.vue`, `src/views/ArticleView.vue`
- Modify: `src/router/index.ts` (2 routes)

**Interfaces:**
- Consumes: nothing new.
- Produces: `knowledge.json` shape `{ sections: [{ id: string; title: string; articles: [{ id: string; title: string; body: string }] }] }` — two sections: `knowledge` (ความรู้) and `selfcare` (คู่มือการปฏิบัติตัว). Routes `knowledge` and `article` (`/knowledge/:id`).

- [ ] **Step 1: Create `src/content/knowledge.json`** (sample content — researcher/PT replaces text later, structure stays)

```json
{
  "sections": [
    {
      "id": "knowledge",
      "title": "ความรู้เรื่องข้อเข่าเสื่อม",
      "articles": [
        {
          "id": "what-is-oa",
          "title": "ข้อเข่าเสื่อมคืออะไร",
          "body": "ข้อเข่าเสื่อม (Knee Osteoarthritis) คือภาวะที่กระดูกอ่อนผิวข้อเข่าสึกกร่อนลงตามอายุและการใช้งาน ทำให้มีอาการปวดเข่า ข้อฝืด และเคลื่อนไหวลำบาก พบบ่อยในผู้ที่มีอายุ 40 ปีขึ้นไป"
        },
        {
          "id": "causes",
          "title": "สาเหตุและปัจจัยเสี่ยง",
          "body": "ปัจจัยเสี่ยงสำคัญ ได้แก่ อายุที่มากขึ้น น้ำหนักตัวเกิน การใช้งานข้อเข่าหนักซ้ำ ๆ เช่น การนั่งยอง นั่งพับเพียบเป็นเวลานาน และการบาดเจ็บที่ข้อเข่า"
        },
        {
          "id": "symptoms",
          "title": "อาการและระยะของโรค",
          "body": "อาการเริ่มจากปวดเข่าเวลาเดินหรือขึ้นลงบันได ข้อฝืดตอนเช้า มีเสียงในข้อ หากปล่อยไว้ข้อจะผิดรูปและเดินลำบากมากขึ้น การออกกำลังกล้ามเนื้อรอบเข่าช่วยชะลอการเสื่อมได้"
        }
      ]
    },
    {
      "id": "selfcare",
      "title": "คู่มือการปฏิบัติตัว",
      "articles": [
        {
          "id": "daily-care",
          "title": "การปฏิบัติตัวในชีวิตประจำวัน",
          "body": "หลีกเลี่ยงการนั่งยอง นั่งพับเพียบ และการขึ้นลงบันไดบ่อย ๆ ควบคุมน้ำหนักตัว ใช้ส้วมแบบนั่งราบ และสวมรองเท้าพื้นนุ่มที่กระชับ"
        },
        {
          "id": "pain-relief",
          "title": "การบรรเทาอาการปวดเบื้องต้น",
          "body": "เมื่อปวดเข่าให้พักการใช้งาน ประคบอุ่นบริเวณที่ปวดครั้งละ 15-20 นาที และบริหารกล้ามเนื้อต้นขาอย่างสม่ำเสมอตามโปรแกรมในแอปนี้"
        }
      ]
    }
  ]
}
```

- [ ] **Step 2: Create `src/views/KnowledgeView.vue`**

```vue
<script setup lang="ts">
import { useRouter } from 'vue-router'
import content from '@/content/knowledge.json'

const router = useRouter()
const sections = content.sections
</script>

<template>
  <main class="page">
    <section v-for="section in sections" :key="section.id">
      <h2>{{ section.title }}</h2>
      <button
        v-for="a in section.articles"
        :key="a.id"
        class="card item"
        @click="router.push({ name: 'article', params: { id: a.id } })"
      >
        {{ a.title }} ›
      </button>
    </section>
  </main>
</template>

<style scoped>
h2 {
  margin: 1rem 0 0.5rem;
}

.item {
  width: 100%;
  text-align: left;
  font-size: 1.1rem;
  display: block;
}
</style>
```

- [ ] **Step 3: Create `src/views/ArticleView.vue`**

```vue
<script setup lang="ts">
import { computed } from 'vue'
import { useRoute } from 'vue-router'
import content from '@/content/knowledge.json'

const route = useRoute()
const article = computed(() =>
  content.sections.flatMap((s) => s.articles).find((a) => a.id === route.params.id),
)
</script>

<template>
  <main class="page">
    <template v-if="article">
      <h1>{{ article.title }}</h1>
      <p class="body">{{ article.body }}</p>
    </template>
    <p v-else>ไม่พบเนื้อหา</p>
  </main>
</template>

<style scoped>
h1 {
  margin-bottom: 1rem;
}

.body {
  font-size: 1.1rem;
  white-space: pre-line;
}
</style>
```

- [ ] **Step 4: Record read flags (`contentProgress`)** — in `src/views/ArticleView.vue`, mark the article read when opened. Add below the `article` computed:

```ts
import { watchEffect } from 'vue'
import { loadJson, saveJson } from '@/lib/storage'

watchEffect(async () => {
  const id = article.value?.id
  if (!id) return
  const progress = await loadJson<Record<string, boolean>>('contentProgress', {})
  if (!progress[id]) {
    progress[id] = true
    await saveJson('contentProgress', progress)
  }
})
```

(Merge the imports into the existing `vue` import line.)

- [ ] **Step 5: Add routes to `src/router/index.ts`** (inside `routes` array)

```ts
{
  path: '/knowledge',
  name: 'knowledge',
  component: () => import('../views/KnowledgeView.vue'),
  meta: { title: 'ความรู้' },
},
{
  path: '/knowledge/:id',
  name: 'article',
  component: () => import('../views/ArticleView.vue'),
  meta: { title: 'ความรู้' },
},
```

- [ ] **Step 6: Verify build + dev check**

Run: `npm run build`
Expected: success. Dev: home → ความรู้ → article opens, back button works.

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "feat: add knowledge and self-care content screens"
```

---

### Task 6: Device guide (photo step cards + YouTube)

**Files:**
- Create: `src/content/deviceGuide.json`, `src/views/DeviceGuideView.vue`, `public/guide/step-1.svg`, `public/guide/step-2.svg`, `public/guide/step-3.svg` (placeholder images — real photos swap in later, same filenames or update JSON)
- Modify: `src/router/index.ts` (1 route)

**Interfaces:**
- Consumes: nothing new.
- Produces: `deviceGuide.json` shape `{ videoIds: string[]; steps: [{ id: string; title: string; caption: string; image: string }] }`. Route `guide`.

- [ ] **Step 1: Create placeholder images** — three identical simple SVGs (differing number), e.g. `public/guide/step-1.svg`:

```svg
<svg xmlns="http://www.w3.org/2000/svg" width="400" height="300" viewBox="0 0 400 300">
  <rect width="400" height="300" fill="#e7e5e4"/>
  <text x="200" y="150" font-size="48" text-anchor="middle" fill="#78716c">ภาพขั้นตอนที่ 1</text>
</svg>
```

(Repeat for step-2, step-3 with the number changed.)

- [ ] **Step 2: Create `src/content/deviceGuide.json`** (sample captions + empty videoIds; researcher adds real YouTube IDs)

```json
{
  "videoIds": [],
  "steps": [
    {
      "id": "step-1",
      "title": "ขั้นตอนที่ 1 นั่งบนเก้าอี้ให้มั่นคง",
      "caption": "นั่งหลังตรงบนเก้าอี้ที่มั่นคง เท้าทั้งสองข้างวางราบกับพื้น",
      "image": "/guide/step-1.svg"
    },
    {
      "id": "step-2",
      "title": "ขั้นตอนที่ 2 สวมอุปกรณ์ที่ขา",
      "caption": "สวมอุปกรณ์บริหารเข่าที่ขาข้างที่ต้องการ รัดสายให้กระชับแต่ไม่แน่นเกินไป",
      "image": "/guide/step-2.svg"
    },
    {
      "id": "step-3",
      "title": "ขั้นตอนที่ 3 กดปุ่มเริ่มและบริหารตามจังหวะ",
      "caption": "กดปุ่มเริ่มที่เครื่อง แล้วเหยียด-งอเข่าตามจังหวะของอุปกรณ์จนครบเวลา",
      "image": "/guide/step-3.svg"
    }
  ]
}
```

- [ ] **Step 3: Create `src/views/DeviceGuideView.vue`**

```vue
<script setup lang="ts">
import { ref } from 'vue'
import guide from '@/content/deviceGuide.json'

const stepIndex = ref(0)
const online = ref(navigator.onLine)
window.addEventListener('online', () => (online.value = true))
window.addEventListener('offline', () => (online.value = false))
</script>

<template>
  <main class="page">
    <div class="card step">
      <img :src="guide.steps[stepIndex].image" :alt="guide.steps[stepIndex].title" />
      <h2>{{ guide.steps[stepIndex].title }}</h2>
      <p>{{ guide.steps[stepIndex].caption }}</p>
      <div class="nav">
        <button :disabled="stepIndex === 0" @click="stepIndex--">← ก่อนหน้า</button>
        <span>{{ stepIndex + 1 }} / {{ guide.steps.length }}</span>
        <button :disabled="stepIndex === guide.steps.length - 1" @click="stepIndex++">
          ถัดไป →
        </button>
      </div>
    </div>

    <h2>วิดีโอสาธิต</h2>
    <p v-if="guide.videoIds.length === 0">ยังไม่มีวิดีโอ</p>
    <template v-else-if="online">
      <div v-for="id in guide.videoIds" :key="id" class="video">
        <iframe
          :src="`https://www.youtube.com/embed/${id}`"
          title="วิดีโอสาธิต"
          allowfullscreen
        ></iframe>
      </div>
    </template>
    <p v-else class="card">📶 วิดีโอต้องใช้อินเทอร์เน็ต กรุณาเชื่อมต่อแล้วลองใหม่</p>
  </main>
</template>

<style scoped>
.step img {
  width: 100%;
  border-radius: 0.75rem;
}

.nav {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-top: 0.75rem;
}

.video {
  position: relative;
  padding-top: 56.25%;
  margin-bottom: 0.75rem;
}

.video iframe {
  position: absolute;
  inset: 0;
  width: 100%;
  height: 100%;
  border: 0;
  border-radius: 0.75rem;
}
</style>
```

- [ ] **Step 4: Add route**

```ts
{
  path: '/guide',
  name: 'guide',
  component: () => import('../views/DeviceGuideView.vue'),
  meta: { title: 'วิธีใช้เครื่อง' },
},
```

- [ ] **Step 5: Verify build + dev check**

Run: `npm run build`
Expected: success. Dev: step cards page, prev/next works, "ยังไม่มีวิดีโอ" shown.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "feat: add device guide with photo step cards and YouTube embeds"
```

---

### Task 7: Sessions store + guided session screen

**Files:**
- Create: `src/stores/sessions.ts`, `src/components/PainScale.vue`, `src/views/SessionView.vue`
- Test: store logic is a thin wrapper over storage (already tested); merge logic tested in Task 11. No new unit test here.
- Modify: `src/router/index.ts` (1 route), `src/main.ts` (hydrate sessions store)

**Interfaces:**
- Consumes: `Session` type, `loadJson`/`saveJson`.
- Produces: `useSessionsStore()` with `sessions: Session[]` (sorted newest first on read), `hydrate(): Promise<void>`, `add(s: Session): Promise<void>`, `replaceAll(list: Session[]): Promise<void>` (used by sync in Task 11). `PainScale` props `{ modelValue: number | null }`, emits `update:modelValue`. Route `session`.

- [ ] **Step 1: Create `src/stores/sessions.ts`**

```ts
import { ref } from 'vue'
import { defineStore } from 'pinia'
import type { Session } from '@/types'
import { loadJson, saveJson } from '@/lib/storage'

const KEY = 'sessions'

export const useSessionsStore = defineStore('sessions', () => {
  const sessions = ref<Session[]>([])

  async function hydrate() {
    sessions.value = await loadJson<Session[]>(KEY, [])
  }

  async function add(s: Session) {
    sessions.value = [...sessions.value, s]
    await saveJson(KEY, sessions.value)
  }

  async function replaceAll(list: Session[]) {
    sessions.value = list
    await saveJson(KEY, list)
  }

  return { sessions, hydrate, add, replaceAll }
})
```

- [ ] **Step 2: Hydrate in `src/main.ts`** (next to profile hydrate)

```ts
import { useSessionsStore } from './stores/sessions'
// after profileStore.hydrate():
await useSessionsStore().hydrate()
```

- [ ] **Step 3: Create `src/components/PainScale.vue`**

```vue
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
```

- [ ] **Step 4: Create `src/views/SessionView.vue`** — three phases: `ready` → `running` (timer) → `rate` (pain) → save & go to records

```vue
<script setup lang="ts">
import { computed, onUnmounted, ref } from 'vue'
import { useRouter } from 'vue-router'
import PainScale from '@/components/PainScale.vue'
import { useSessionsStore } from '@/stores/sessions'

const router = useRouter()
const store = useSessionsStore()

const phase = ref<'ready' | 'running' | 'rate'>('ready')
const painBefore = ref<number | null>(null)
const painAfter = ref<number | null>(null)
const seconds = ref(0)
let timer: ReturnType<typeof setInterval> | null = null

const clock = computed(() => {
  const m = String(Math.floor(seconds.value / 60)).padStart(2, '0')
  const s = String(seconds.value % 60).padStart(2, '0')
  return `${m}:${s}`
})

function start() {
  phase.value = 'running'
  timer = setInterval(() => seconds.value++, 1000)
}

function stop() {
  if (timer) clearInterval(timer)
  phase.value = 'rate'
}

async function save() {
  if (painAfter.value === null) return
  await store.add({
    id: `manual-${Date.now()}`,
    date: new Date().toISOString(),
    durationSec: seconds.value,
    painBefore: painBefore.value ?? undefined,
    painAfter: painAfter.value,
    source: 'manual',
  })
  router.replace({ name: 'records' })
}

onUnmounted(() => {
  if (timer) clearInterval(timer)
})
</script>

<template>
  <main class="page">
    <template v-if="phase === 'ready'">
      <div class="card">
        <h2>ก่อนเริ่ม ปวดเข่าแค่ไหน?</h2>
        <PainScale v-model="painBefore" />
      </div>
      <p class="card">สวมอุปกรณ์ให้เรียบร้อย (ดูวิธีใช้ได้ที่เมนู "วิธีใช้เครื่อง") แล้วกดเริ่ม</p>
      <button class="primary wide" @click="start">▶ เริ่มออกกำลังกาย</button>
    </template>

    <template v-else-if="phase === 'running'">
      <p class="clock">{{ clock }}</p>
      <p class="hint">บริหารเข่าตามจังหวะของอุปกรณ์</p>
      <button class="primary wide" @click="stop">⏹ เสร็จแล้ว</button>
    </template>

    <template v-else>
      <div class="card">
        <h2>หลังออกกำลังกาย ปวดเข่าแค่ไหน?</h2>
        <PainScale v-model="painAfter" />
      </div>
      <button class="primary wide" :disabled="painAfter === null" @click="save">
        ✔ บันทึกผล
      </button>
    </template>
  </main>
</template>

<style scoped>
.clock {
  font-size: 4rem;
  text-align: center;
  font-variant-numeric: tabular-nums;
  margin: 2rem 0 0.5rem;
}

.hint {
  text-align: center;
  margin-bottom: 2rem;
}

.wide {
  width: 100%;
  font-size: 1.2rem;
  min-height: 4rem;
}
</style>
```

- [ ] **Step 5: Add route**

```ts
{
  path: '/session',
  name: 'session',
  component: () => import('../views/SessionView.vue'),
  meta: { title: 'ออกกำลังกาย' },
},
```

- [ ] **Step 6: Verify build + dev check**

Run: `npm run build`
Expected: success. Dev: full flow ready → timer runs → pain rating → saves (records route 404s until Task 8 — verify the session persisted by reloading and checking Preferences/localStorage instead).

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "feat: add guided session flow with timer and pain scale"
```

---

### Task 8: Records screen + trend chart

**Files:**
- Create: `src/components/TrendChart.vue`, `src/views/RecordsView.vue`
- Modify: `src/router/index.ts` (1 route)

**Interfaces:**
- Consumes: `useSessionsStore().sessions`.
- Produces: `TrendChart` props `{ points: { label: string; value: number }[]; max: number }` — pure-SVG polyline. Route `records`.

- [ ] **Step 1: Create `src/components/TrendChart.vue`**

```vue
<script setup lang="ts">
import { computed } from 'vue'

const props = defineProps<{ points: { label: string; value: number }[]; max: number }>()

const W = 320
const H = 160
const PAD = 24

const coords = computed(() =>
  props.points.map((p, i) => ({
    x:
      props.points.length === 1
        ? W / 2
        : PAD + (i * (W - 2 * PAD)) / (props.points.length - 1),
    y: H - PAD - (p.value / props.max) * (H - 2 * PAD),
    label: p.label,
  })),
)

const polyline = computed(() => coords.value.map((c) => `${c.x},${c.y}`).join(' '))
</script>

<template>
  <svg :viewBox="`0 0 ${W} ${H}`" class="chart" role="img">
    <line :x1="PAD" :y1="H - PAD" :x2="W - PAD" :y2="H - PAD" stroke="var(--c-border)" />
    <polyline :points="polyline" fill="none" stroke="var(--c-primary)" stroke-width="3" />
    <circle v-for="(c, i) in coords" :key="i" :cx="c.x" :cy="c.y" r="5" fill="var(--c-primary)" />
  </svg>
</template>

<style scoped>
.chart {
  width: 100%;
  height: auto;
}
</style>
```

- [ ] **Step 2: Create `src/views/RecordsView.vue`**

```vue
<script setup lang="ts">
import { computed } from 'vue'
import TrendChart from '@/components/TrendChart.vue'
import { useSessionsStore } from '@/stores/sessions'

const store = useSessionsStore()

const sorted = computed(() =>
  [...store.sessions].sort((a, b) => b.date.localeCompare(a.date)),
)

const painPoints = computed(() =>
  [...store.sessions]
    .sort((a, b) => a.date.localeCompare(b.date))
    .slice(-10)
    .map((s) => ({ label: s.date.slice(5, 10), value: s.painAfter })),
)

const thisWeekCount = computed(() => {
  const weekAgo = Date.now() - 7 * 24 * 3600 * 1000
  return store.sessions.filter((s) => new Date(s.date).getTime() >= weekAgo).length
})

function fmt(dateIso: string) {
  return new Date(dateIso).toLocaleDateString('th-TH', {
    day: 'numeric',
    month: 'short',
    year: 'numeric',
  })
}

function mins(sec: number) {
  return Math.round(sec / 60)
}
</script>

<template>
  <main class="page">
    <div class="card">
      <h2>สัปดาห์นี้ออกกำลังกาย {{ thisWeekCount }} ครั้ง</h2>
    </div>

    <div class="card" v-if="painPoints.length >= 2">
      <h2>ระดับความปวดหลังออกกำลังกาย (0-10)</h2>
      <TrendChart :points="painPoints" :max="10" />
    </div>

    <h2>ประวัติการออกกำลังกาย</h2>
    <p v-if="sorted.length === 0" class="card">ยังไม่มีบันทึก เริ่มออกกำลังกายครั้งแรกได้เลย</p>
    <div v-for="s in sorted" :key="s.id" class="card row">
      <span>{{ fmt(s.date) }}</span>
      <span>{{ mins(s.durationSec) }} นาที</span>
      <span>ปวด {{ s.painAfter }}/10</span>
      <span class="src">{{ s.source === 'device' ? '📡' : '✍️' }}</span>
    </div>
  </main>
</template>

<style scoped>
.row {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 0.5rem;
}

.src {
  font-size: 1.2rem;
}
</style>
```

- [ ] **Step 3: Add route**

```ts
{
  path: '/records',
  name: 'records',
  component: () => import('../views/RecordsView.vue'),
  meta: { title: 'บันทึกของฉัน' },
},
```

- [ ] **Step 4: Verify build + dev check**

Run: `npm run build`
Expected: success. Dev: complete a session → lands on records, entry listed; after 2+ sessions chart renders.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "feat: add records screen with history list and pain trend chart"
```

---

### Task 9: Quiz engine (scoring + store + UI + question sets)

**Files:**
- Create: `src/lib/quiz.ts`, `src/lib/quiz.test.ts`, `src/stores/quiz.ts`, `src/content/quizzes.json`, `src/views/QuizView.vue`
- Modify: `src/router/index.ts` (1 route), `src/main.ts` (hydrate), `src/views/HomeView.vue` (pre-test banner)

**Interfaces:**
- Consumes: `QuizResult` type, storage module.
- Produces:
  - `quizzes.json` shape: `{ quizzes: [{ id: 'pre' | 'post' | 'satisfaction'; title: string; questions: [{ text: string; choices: string[]; correctIndex?: number }] }] }` — satisfaction questions omit `correctIndex`.
  - `quiz.ts`: `scoreQuiz(questions: { correctIndex?: number }[], answers: number[]): number` — counts answers matching `correctIndex`; questions without `correctIndex` contribute 0.
  - `useQuizStore()`: `results: QuizResult[]`, `hydrate()`, `addResult(r: QuizResult)`, `hasTaken(quizId: string): boolean`.
  - Route `quiz` (`/quiz/:quizId`).

- [ ] **Step 1: Write failing test `src/lib/quiz.test.ts`**

```ts
import { describe, expect, it } from 'vitest'
import { scoreQuiz } from './quiz'

describe('scoreQuiz', () => {
  const questions = [{ correctIndex: 0 }, { correctIndex: 2 }, { correctIndex: 1 }]

  it('counts correct answers', () => {
    expect(scoreQuiz(questions, [0, 2, 1])).toBe(3)
    expect(scoreQuiz(questions, [0, 0, 0])).toBe(1)
    expect(scoreQuiz(questions, [1, 0, 0])).toBe(0)
  })

  it('ignores questions without correctIndex (satisfaction survey)', () => {
    expect(scoreQuiz([{}, {}, {}], [4, 4, 3])).toBe(0)
  })

  it('handles missing answers safely', () => {
    expect(scoreQuiz(questions, [0])).toBe(1)
  })
})
```

- [ ] **Step 2: Run test to verify it fails**

Run: `npm test`
Expected: FAIL — cannot find `./quiz`.

- [ ] **Step 3: Create `src/lib/quiz.ts`**

```ts
export function scoreQuiz(
  questions: { correctIndex?: number }[],
  answers: number[],
): number {
  return questions.reduce(
    (score, q, i) =>
      q.correctIndex !== undefined && answers[i] === q.correctIndex ? score + 1 : score,
    0,
  )
}
```

- [ ] **Step 4: Run tests to verify pass**

Run: `npm test`
Expected: all pass (storage + quiz).

- [ ] **Step 5: Create `src/content/quizzes.json`** (sample questions — researcher/PT replaces with validated instrument; pre and post use the same items per spec)

```json
{
  "quizzes": [
    {
      "id": "pre",
      "title": "แบบทดสอบก่อนใช้งาน",
      "questions": [
        {
          "text": "ข้อเข่าเสื่อมเกิดจากอะไรเป็นหลัก?",
          "choices": [
            "กระดูกอ่อนผิวข้อสึกกร่อน",
            "กล้ามเนื้ออักเสบ",
            "เส้นเลือดอุดตัน",
            "กระดูกหัก"
          ],
          "correctIndex": 0
        },
        {
          "text": "ท่านั่งใดที่ผู้ป่วยข้อเข่าเสื่อมควรหลีกเลี่ยง?",
          "choices": ["นั่งเก้าอี้", "นั่งยอง ๆ", "นั่งพิงพนัก", "นอนราบ"],
          "correctIndex": 1
        },
        {
          "text": "การออกกำลังกล้ามเนื้อรอบเข่ามีผลอย่างไร?",
          "choices": [
            "ทำให้ข้อเสื่อมเร็วขึ้น",
            "ไม่มีผลใด ๆ",
            "ช่วยชะลอการเสื่อมและลดปวด",
            "ทำให้ข้อติด"
          ],
          "correctIndex": 2
        }
      ]
    },
    {
      "id": "post",
      "title": "แบบทดสอบหลังใช้งาน",
      "questions": [
        {
          "text": "ข้อเข่าเสื่อมเกิดจากอะไรเป็นหลัก?",
          "choices": [
            "กระดูกอ่อนผิวข้อสึกกร่อน",
            "กล้ามเนื้ออักเสบ",
            "เส้นเลือดอุดตัน",
            "กระดูกหัก"
          ],
          "correctIndex": 0
        },
        {
          "text": "ท่านั่งใดที่ผู้ป่วยข้อเข่าเสื่อมควรหลีกเลี่ยง?",
          "choices": ["นั่งเก้าอี้", "นั่งยอง ๆ", "นั่งพิงพนัก", "นอนราบ"],
          "correctIndex": 1
        },
        {
          "text": "การออกกำลังกล้ามเนื้อรอบเข่ามีผลอย่างไร?",
          "choices": [
            "ทำให้ข้อเสื่อมเร็วขึ้น",
            "ไม่มีผลใด ๆ",
            "ช่วยชะลอการเสื่อมและลดปวด",
            "ทำให้ข้อติด"
          ],
          "correctIndex": 2
        }
      ]
    },
    {
      "id": "satisfaction",
      "title": "แบบประเมินความพึงพอใจ",
      "questions": [
        {
          "text": "ความพึงพอใจต่อการใช้งานแอปโดยรวม",
          "choices": ["น้อยที่สุด", "น้อย", "ปานกลาง", "มาก", "มากที่สุด"]
        },
        {
          "text": "ตัวหนังสือและปุ่มมีขนาดเหมาะสม อ่านง่าย",
          "choices": ["น้อยที่สุด", "น้อย", "ปานกลาง", "มาก", "มากที่สุด"]
        },
        {
          "text": "เนื้อหาความรู้เป็นประโยชน์ต่อการดูแลตนเอง",
          "choices": ["น้อยที่สุด", "น้อย", "ปานกลาง", "มาก", "มากที่สุด"]
        }
      ]
    }
  ]
}
```

- [ ] **Step 6: Create `src/stores/quiz.ts`**

```ts
import { ref } from 'vue'
import { defineStore } from 'pinia'
import type { QuizResult } from '@/types'
import { loadJson, saveJson } from '@/lib/storage'

const KEY = 'quizResults'

export const useQuizStore = defineStore('quiz', () => {
  const results = ref<QuizResult[]>([])

  async function hydrate() {
    results.value = await loadJson<QuizResult[]>(KEY, [])
  }

  async function addResult(r: QuizResult) {
    results.value = [...results.value, r]
    await saveJson(KEY, results.value)
  }

  function hasTaken(quizId: string): boolean {
    return results.value.some((r) => r.quizId === quizId)
  }

  return { results, hydrate, addResult, hasTaken }
})
```

- [ ] **Step 7: Hydrate in `src/main.ts`** (next to other hydrates)

```ts
import { useQuizStore } from './stores/quiz'
await useQuizStore().hydrate()
```

- [ ] **Step 8: Create `src/views/QuizView.vue`** — one question per screen, big choice buttons

```vue
<script setup lang="ts">
import { computed, ref } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import data from '@/content/quizzes.json'
import { scoreQuiz } from '@/lib/quiz'
import { useQuizStore } from '@/stores/quiz'
import type { QuizResult } from '@/types'

const route = useRoute()
const router = useRouter()
const store = useQuizStore()

const quiz = computed(() => data.quizzes.find((q) => q.id === route.params.quizId))
const qIndex = ref(0)
const answers = ref<number[]>([])
const done = ref(false)
const finalScore = ref(0)

async function answer(choiceIndex: number) {
  if (!quiz.value) return
  answers.value[qIndex.value] = choiceIndex
  if (qIndex.value < quiz.value.questions.length - 1) {
    qIndex.value++
    return
  }
  finalScore.value = scoreQuiz(quiz.value.questions, answers.value)
  await store.addResult({
    quizId: quiz.value.id as QuizResult['quizId'],
    answers: [...answers.value],
    score: finalScore.value,
    timestamp: new Date().toISOString(),
  })
  done.value = true
}
</script>

<template>
  <main class="page">
    <p v-if="!quiz">ไม่พบแบบทดสอบ</p>

    <template v-else-if="!done">
      <p class="progress">ข้อ {{ qIndex + 1 }} / {{ quiz.questions.length }}</p>
      <h2 class="card">{{ quiz.questions[qIndex].text }}</h2>
      <button
        v-for="(choice, i) in quiz.questions[qIndex].choices"
        :key="i"
        class="choice"
        @click="answer(i)"
      >
        {{ choice }}
      </button>
    </template>

    <template v-else>
      <div class="card center">
        <h2>เสร็จเรียบร้อย ขอบคุณค่ะ 🎉</h2>
        <p v-if="quiz.id !== 'satisfaction'">
          ได้ {{ finalScore }} จาก {{ quiz.questions.length }} คะแนน
        </p>
      </div>
      <button class="primary wide" @click="router.replace({ name: 'home' })">กลับหน้าหลัก</button>
    </template>
  </main>
</template>

<style scoped>
.progress {
  color: #57534e;
  margin-bottom: 0.5rem;
}

.choice {
  display: block;
  width: 100%;
  text-align: left;
  font-size: 1.1rem;
  margin-bottom: 0.75rem;
  min-height: 3.5rem;
}

.center {
  text-align: center;
}

.wide {
  width: 100%;
}
</style>
```

- [ ] **Step 9: Add route**

```ts
{
  path: '/quiz/:quizId',
  name: 'quiz',
  component: () => import('../views/QuizView.vue'),
  meta: { title: 'แบบทดสอบ' },
},
```

- [ ] **Step 10: Add pre-test banner to `src/views/HomeView.vue`** — insert into template above the BigButtons, plus store import in script:

```ts
import { useQuizStore } from '@/stores/quiz'
const quizStore = useQuizStore()
```

```vue
<div v-if="!quizStore.hasTaken('pre')" class="card banner">
  <p>ก่อนเริ่มใช้งาน ทำแบบทดสอบก่อนใช้งานสั้น ๆ ก่อนนะคะ</p>
  <button class="primary" @click="$router.push({ name: 'quiz', params: { quizId: 'pre' } })">
    📝 ทำแบบทดสอบก่อนใช้งาน
  </button>
</div>
```

(Post-test and satisfaction are launched from Settings in Task 11 — staff-triggered per spec.)

- [ ] **Step 11: Verify tests + build + dev check**

Run: `npm test` then `npm run build`
Expected: all tests pass; build succeeds. Dev: banner shows, pre-test completes, banner disappears after.

- [ ] **Step 12: Commit**

```bash
git add -A
git commit -m "feat: add quiz engine with pre/post tests and satisfaction survey"
```

---

### Task 10: Export module

**Files:**
- Create: `src/lib/exporter.ts`, `src/lib/exporter.test.ts`

**Interfaces:**
- Consumes: `Profile`, `Session`, `QuizResult` types; `@capacitor/filesystem`, `@capacitor/share`.
- Produces:
  - `buildExport(profile: Profile | null, sessions: Session[], results: QuizResult[]): string` — pure; returns pretty-printed JSON with `exportedAt` timestamp and `appVersion`.
  - `shareExport(json: string, patientCode: string): Promise<void>` — writes `smart-oa-knee-<patientCode>-<yyyy-mm-dd>.json` to cache dir, opens Android share sheet. (Not unit-tested; thin native glue.)

- [ ] **Step 1: Write failing test `src/lib/exporter.test.ts`**

```ts
import { describe, expect, it, vi } from 'vitest'

vi.mock('@capacitor/filesystem', () => ({ Filesystem: {}, Directory: { Cache: 'CACHE' } }))
vi.mock('@capacitor/share', () => ({ Share: {} }))

import { buildExport } from './exporter'

describe('buildExport', () => {
  it('includes profile, sessions, results, and metadata', () => {
    const json = buildExport(
      { name: 'ทดสอบ', age: 60, patientCode: 'P01', createdAt: '2026-07-01T00:00:00Z' },
      [
        {
          id: 's1',
          date: '2026-07-02T10:00:00Z',
          durationSec: 600,
          painAfter: 3,
          source: 'manual',
        },
      ],
      [{ quizId: 'pre', answers: [0, 1], score: 2, timestamp: '2026-07-01T01:00:00Z' }],
    )
    const parsed = JSON.parse(json)
    expect(parsed.profile.patientCode).toBe('P01')
    expect(parsed.sessions).toHaveLength(1)
    expect(parsed.quizResults[0].score).toBe(2)
    expect(parsed.exportedAt).toBeTruthy()
  })

  it('handles null profile', () => {
    const parsed = JSON.parse(buildExport(null, [], []))
    expect(parsed.profile).toBeNull()
    expect(parsed.sessions).toEqual([])
  })
})
```

- [ ] **Step 2: Run test to verify it fails**

Run: `npm test`
Expected: FAIL — cannot find `./exporter`.

- [ ] **Step 3: Create `src/lib/exporter.ts`**

```ts
import { Directory, Encoding, Filesystem } from '@capacitor/filesystem'
import { Share } from '@capacitor/share'
import type { Profile, QuizResult, Session } from '@/types'

export function buildExport(
  profile: Profile | null,
  sessions: Session[],
  quizResults: QuizResult[],
): string {
  return JSON.stringify(
    {
      app: 'smart-oa-knee',
      appVersion: '0.0.0',
      exportedAt: new Date().toISOString(),
      profile,
      sessions,
      quizResults,
    },
    null,
    2,
  )
}

export async function shareExport(json: string, patientCode: string): Promise<void> {
  const date = new Date().toISOString().slice(0, 10)
  const fileName = `smart-oa-knee-${patientCode}-${date}.json`
  const { uri } = await Filesystem.writeFile({
    path: fileName,
    data: json,
    directory: Directory.Cache,
    encoding: Encoding.UTF8,
  })
  await Share.share({ title: 'ข้อมูล Smart OA Knee', url: uri })
}
```

- [ ] **Step 4: Run tests to verify pass**

Run: `npm test`
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "feat: add data export module with share-sheet delivery"
```

---

### Task 11: Sync service, settings store, settings screen

**Files:**
- Create: `src/lib/sessions.ts`, `src/lib/sessions.test.ts`, `src/lib/api.ts`, `src/stores/settings.ts`, `src/views/SettingsView.vue`
- Modify: `src/router/index.ts` (1 route), `src/main.ts` (hydrate settings, apply font class, fire sync), `src/App.vue` (font class binding)

**Interfaces:**
- Consumes: `useSessionsStore` (`sessions`, `replaceAll`), `useProfileStore` (patientCode), exporter, quiz store (post/satisfaction launch).
- Produces:
  - `lib/sessions.ts`: `mergeSessions(local: Session[], remote: Session[]): Session[]` — union deduped by `id`; local entry wins on conflict; result sorted by date ascending.
  - `lib/api.ts`: `fetchDeviceSessions(serverUrl: string, patientCode: string): Promise<Session[]>` — GET `{serverUrl}/api/patients/{patientCode}/sessions`, expects JSON array `{ id, date, durationSec }`, maps each to `Session` with `source: 'device'` and no pain rating. Device sessions have no pain data, so this task changes `Session.painAfter` to optional (`painAfter?: number`) in `types.ts` and updates the two spots in `RecordsView.vue` that assume it exists (chart filters to rated sessions; history row shows "—" when unrated).
  - `useSettingsStore()`: `settings: Settings`, `hydrate()`, `update(patch: Partial<Settings>)`.
  - `syncNow(): Promise<boolean>` exported from `lib/api.ts` — orchestrates fetch + merge + `replaceAll` + `lastSync` update; returns success flag; never throws.
  - Route `settings`.

- [ ] **Step 1: Write failing test `src/lib/sessions.test.ts`**

```ts
import { describe, expect, it } from 'vitest'
import { mergeSessions } from './sessions'
import type { Session } from '@/types'

const mk = (id: string, date: string, source: Session['source']): Session => ({
  id,
  date,
  durationSec: 60,
  source,
})

describe('mergeSessions', () => {
  it('unions local and remote', () => {
    const merged = mergeSessions(
      [mk('a', '2026-07-01', 'manual')],
      [mk('b', '2026-07-02', 'device')],
    )
    expect(merged.map((s) => s.id)).toEqual(['a', 'b'])
  })

  it('dedupes by id, local wins', () => {
    const local = { ...mk('a', '2026-07-01', 'manual'), durationSec: 99 }
    const remote = { ...mk('a', '2026-07-01', 'device'), durationSec: 11 }
    const merged = mergeSessions([local], [remote])
    expect(merged).toHaveLength(1)
    expect(merged[0].durationSec).toBe(99)
  })

  it('sorts by date ascending', () => {
    const merged = mergeSessions(
      [mk('b', '2026-07-05', 'manual')],
      [mk('a', '2026-07-01', 'device')],
    )
    expect(merged.map((s) => s.id)).toEqual(['a', 'b'])
  })
})
```

- [ ] **Step 2: Run test to verify it fails**

Run: `npm test`
Expected: FAIL — cannot find `./sessions`.

- [ ] **Step 3: Make `painAfter` optional in `src/types.ts`**

```ts
export interface Session {
  id: string
  date: string // ISO date-time
  durationSec: number
  painBefore?: number
  painAfter?: number
  source: 'manual' | 'device'
}
```

Update `src/views/RecordsView.vue`: chart points filter to sessions with pain — replace the `painPoints` computed with:

```ts
const painPoints = computed(() =>
  [...store.sessions]
    .filter((s) => s.painAfter !== undefined)
    .sort((a, b) => a.date.localeCompare(b.date))
    .slice(-10)
    .map((s) => ({ label: s.date.slice(5, 10), value: s.painAfter as number })),
)
```

and in the history row template show pain conditionally:

```vue
<span v-if="s.painAfter !== undefined">ปวด {{ s.painAfter }}/10</span>
<span v-else>—</span>
```

(`SessionView.vue` still requires a rating before saving; no change there.)

- [ ] **Step 4: Create `src/lib/sessions.ts`**

```ts
import type { Session } from '@/types'

export function mergeSessions(local: Session[], remote: Session[]): Session[] {
  const byId = new Map<string, Session>()
  for (const s of remote) byId.set(s.id, s)
  for (const s of local) byId.set(s.id, s) // local wins
  return [...byId.values()].sort((a, b) => a.date.localeCompare(b.date))
}
```

- [ ] **Step 5: Run tests to verify pass**

Run: `npm test`
Expected: all pass.

- [ ] **Step 6: Create `src/stores/settings.ts`**

```ts
import { ref } from 'vue'
import { defineStore } from 'pinia'
import type { Settings } from '@/types'
import { loadJson, saveJson } from '@/lib/storage'

const KEY = 'settings'
const DEFAULTS: Settings = { fontLarge: false, serverUrl: '', lastSync: null }

export const useSettingsStore = defineStore('settings', () => {
  const settings = ref<Settings>({ ...DEFAULTS })

  async function hydrate() {
    settings.value = { ...DEFAULTS, ...(await loadJson<Partial<Settings>>(KEY, {})) }
  }

  async function update(patch: Partial<Settings>) {
    settings.value = { ...settings.value, ...patch }
    await saveJson(KEY, settings.value)
  }

  return { settings, hydrate, update }
})
```

- [ ] **Step 7: Create `src/lib/api.ts`**

```ts
import type { Session } from '@/types'
import { mergeSessions } from './sessions'
import { useSessionsStore } from '@/stores/sessions'
import { useSettingsStore } from '@/stores/settings'
import { useProfileStore } from '@/stores/profile'

interface RemoteSession {
  id: string
  date: string
  durationSec: number
}

export async function fetchDeviceSessions(
  serverUrl: string,
  patientCode: string,
): Promise<Session[]> {
  const base = serverUrl.replace(/\/+$/, '')
  const res = await fetch(`${base}/api/patients/${encodeURIComponent(patientCode)}/sessions`)
  if (!res.ok) throw new Error(`HTTP ${res.status}`)
  const list = (await res.json()) as RemoteSession[]
  return list.map((r) => ({
    id: r.id,
    date: r.date,
    durationSec: r.durationSec,
    source: 'device' as const,
  }))
}

/** Fetch device sessions and merge into local log. Never throws. */
export async function syncNow(): Promise<boolean> {
  const settings = useSettingsStore()
  const profile = useProfileStore()
  const sessions = useSessionsStore()
  const url = settings.settings.serverUrl
  const code = profile.profile?.patientCode
  if (!url || !code) return false
  try {
    const remote = await fetchDeviceSessions(url, code)
    await sessions.replaceAll(mergeSessions(sessions.sessions, remote))
    await settings.update({ lastSync: new Date().toISOString() })
    return true
  } catch {
    return false
  }
}
```

- [ ] **Step 8: Create `src/views/SettingsView.vue`**

```vue
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
        {{ settings.settings.fontLarge ? 'ตัวหนังสือปกติ' : 'ตัวหนังสือใหญ่พิเศษ' }}
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
      <button class="wide" @click="saveServer">บันทึก</button>
      <button class="wide" :disabled="syncing" @click="doSync">
        {{ syncing ? 'กำลังดึงข้อมูล...' : '🔄 ดึงข้อมูลจากเครื่องบริหาร' }}
      </button>
      <p>ดึงข้อมูลล่าสุด: {{ fmtSync(settings.settings.lastSync) }}</p>
      <p v-if="syncMessage">{{ syncMessage }}</p>
    </div>

    <div class="card">
      <h2>ข้อมูลผู้ใช้</h2>
      <p>{{ profile.profile?.name }} (รหัส {{ profile.profile?.patientCode }})</p>
      <button class="wide" @click="router.push({ name: 'setup' })">แก้ไขข้อมูล</button>
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
```

- [ ] **Step 9: Add route**

```ts
{
  path: '/settings',
  name: 'settings',
  component: () => import('../views/SettingsView.vue'),
  meta: { title: 'ตั้งค่า' },
},
```

- [ ] **Step 10: Wire settings into `src/main.ts`** — hydrate + fire-and-forget sync after mount:

```ts
import { useSettingsStore } from './stores/settings'
import { syncNow } from './lib/api'

// after other hydrates:
const settingsStore = useSettingsStore()
await settingsStore.hydrate()

app.use(router)
app.mount('#app')

void syncNow() // background; silent on failure
```

- [ ] **Step 11: Bind font class in `src/App.vue`** — Vue mounts into `#app`, so toggle the class on the wrapper via a computed on the root template element. Change `App.vue` template root to a div:

```vue
<script setup lang="ts">
import { RouterView, useRoute, useRouter } from 'vue-router'
import { useSettingsStore } from '@/stores/settings'

const route = useRoute()
const router = useRouter()
const settings = useSettingsStore()
</script>

<template>
  <div :class="{ 'font-large': settings.settings.fontLarge }">
    <header class="topbar" v-if="route.name !== 'home' && route.name !== 'setup'">
      <button class="back" @click="router.back()" aria-label="ย้อนกลับ">← กลับ</button>
      <h1>{{ route.meta.title ?? '' }}</h1>
    </header>
    <RouterView />
  </div>
</template>
```

And in `main.css`, change the selector `#app.font-large` to `.font-large`:

```css
.font-large {
  font-size: 1.2em;
}
```

- [ ] **Step 12: Verify tests + build + dev check**

Run: `npm test` then `npm run build`
Expected: all tests pass; build succeeds. Dev: settings screen renders; font toggle visibly scales text; export triggers share (web fallback may error — fine, native-only); sync with empty URL shows failure message gracefully.

- [ ] **Step 13: Commit**

```bash
git add -A
git commit -m "feat: add settings screen with export, sync, and font toggle"
```

---

### Task 12: Android verification

**Files:** none (verification only)

**Interfaces:** consumes everything.

- [ ] **Step 1: Build + sync + native compile check**

```bash
cd PT-Leg-Tracker
npm run build
npx cap sync android
cd android
.\gradlew assembleDebug
```

Expected: BUILD SUCCESSFUL.

- [ ] **Step 2: Run on device/emulator**

```bash
npx cap run android
```

Manual smoke test on device: setup → home → knowledge article → device guide steps → full guided session with pain ratings → records shows entry + chart after 2 sessions → pre-test from banner → post-test + satisfaction from settings → export opens Android share sheet with JSON file.

- [ ] **Step 3: Commit any fixes, then final commit if clean**

```bash
git add -A
git commit -m "chore: android verification fixes" # only if fixes were needed
```
