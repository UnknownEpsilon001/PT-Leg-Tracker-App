# Unified Editable Content Folder Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move all staff-editable educational, branding, quiz, image, local-video, and YouTube content into `PT-Leg-Tracker/public/editable-content/` and load it safely at runtime.

**Architecture:** Vite and Capacitor package `public/editable-content/` unchanged with the app. A focused `src/lib/content.ts` module fetches, validates, types, and caches each JSON file; views consume that API and render explicit loading/error states. Functional UI copy, server settings, user records, and therapy control remain compiled source code.

**Tech Stack:** Vue 3 Composition API, TypeScript, Vite public assets, Vitest, Capacitor 8, Android/Gradle.

## Global Constraints

- All staff-editable content lives under `PT-Leg-Tracker/public/editable-content/`.
- Supported images: `.png`, `.jpg`, `.jpeg`, `.webp`, `.svg`; supported local videos: `.mp4`, `.webm`.
- Local content and media must work without internet after being packaged; YouTube playback requires internet.
- YouTube configuration stores only video IDs, not full watch URLs.
- Article IDs, guide-step IDs, and quiz IDs remain stable; quiz IDs are exactly `pre`, `post`, and `satisfaction`.
- Existing profile, quiz-result, session, pain-log, settings, and read-progress storage must not be reset or migrated.
- Invalid content shows a concise Thai error pointing staff to `public/editable-content/README.md`; it must not crash unrelated screens or silently load a hidden stale copy.
- Server and firmware files are out of scope.
- Run all npm/npx commands from `PT-Leg-Tracker/`; run Gradle from `PT-Leg-Tracker/android/`.
- Never add `Co-Authored-By` or other AI attribution to commits.

---

## File Map

**Create**

- `PT-Leg-Tracker/public/editable-content/README.md` — non-developer editing/build guide.
- `PT-Leg-Tracker/public/editable-content/app.json` — home branding configuration.
- `PT-Leg-Tracker/public/editable-content/knowledge.json` — migrated article content.
- `PT-Leg-Tracker/public/editable-content/device-guide.json` — migrated steps and mixed video entries.
- `PT-Leg-Tracker/public/editable-content/quizzes.json` — migrated quiz content.
- `PT-Leg-Tracker/public/editable-content/images/branding/logo.png` — migrated logo.
- `PT-Leg-Tracker/public/editable-content/images/knowledge/*` — migrated article images.
- `PT-Leg-Tracker/public/editable-content/images/guide/*` — migrated guide images.
- `PT-Leg-Tracker/public/editable-content/videos/README.md` — local-video format/size guidance.
- `PT-Leg-Tracker/src/lib/content.ts` — public JSON fetch, runtime validation, cache, and types.
- `PT-Leg-Tracker/src/lib/content.test.ts` — loader/validator contract tests.

**Modify**

- `PT-Leg-Tracker/src/views/HomeView.vue` — async branding data.
- `PT-Leg-Tracker/src/views/KnowledgeView.vue` — async knowledge data.
- `PT-Leg-Tracker/src/views/ArticleView.vue` — async article lookup while preserving read progress.
- `PT-Leg-Tracker/src/views/DeviceGuideView.vue` — async guide data and YouTube/local-video rendering.
- `PT-Leg-Tracker/src/views/QuizView.vue` — async quiz data.

**Delete after consumers migrate**

- `PT-Leg-Tracker/src/assets/logo.png`
- `PT-Leg-Tracker/src/content/knowledge.json`
- `PT-Leg-Tracker/src/content/deviceGuide.json`
- `PT-Leg-Tracker/src/content/quizzes.json`
- `PT-Leg-Tracker/public/knowledge/`
- `PT-Leg-Tracker/public/guide/`

---

### Task 1: Create the Unified Editable Content Package

**Files:**
- Create: `PT-Leg-Tracker/public/editable-content/**`
- Source data: `PT-Leg-Tracker/src/content/*.json`
- Source media: `PT-Leg-Tracker/src/assets/logo.png`, `PT-Leg-Tracker/public/knowledge/*`, `PT-Leg-Tracker/public/guide/*`

**Interfaces:**
- Produces: packaged files later loaded from `/editable-content/app.json`, `/editable-content/knowledge.json`, `/editable-content/device-guide.json`, and `/editable-content/quizzes.json`.
- Does not delete old files yet; current imports must continue to build until Tasks 3–5 migrate consumers.

- [ ] **Step 1: Create the folder tree and copy existing media**

Run from the repository root:

```powershell
New-Item -ItemType Directory -Force `
  PT-Leg-Tracker/public/editable-content/images/branding, `
  PT-Leg-Tracker/public/editable-content/images/knowledge, `
  PT-Leg-Tracker/public/editable-content/images/guide, `
  PT-Leg-Tracker/public/editable-content/videos | Out-Null
Copy-Item PT-Leg-Tracker/src/assets/logo.png `
  PT-Leg-Tracker/public/editable-content/images/branding/logo.png
Copy-Item PT-Leg-Tracker/public/knowledge/* `
  PT-Leg-Tracker/public/editable-content/images/knowledge/
Copy-Item PT-Leg-Tracker/public/guide/* `
  PT-Leg-Tracker/public/editable-content/images/guide/
```

- [ ] **Step 2: Create `app.json`**

```json
{
  "homeSubtitle": "เครื่องช่วยบริหารกล้ามเนื้อต้นขา Smart OA Knee",
  "logo": "/editable-content/images/branding/logo.png",
  "logoAlt": "ตราวิทยาลัยการอาชีพฝาง"
}
```

- [ ] **Step 3: Copy and rewrite `knowledge.json` media paths**

Copy the current JSON verbatim, preserving every section/article ID, title,
body, and imageAlt. Change only these values:

```text
/knowledge/what-is-oa.svg  -> /editable-content/images/knowledge/what-is-oa.svg
/knowledge/causes.svg      -> /editable-content/images/knowledge/causes.svg
/knowledge/symptoms.svg    -> /editable-content/images/knowledge/symptoms.svg
/knowledge/daily-care.svg  -> /editable-content/images/knowledge/daily-care.svg
/knowledge/pain-relief.svg -> /editable-content/images/knowledge/pain-relief.svg
```

- [ ] **Step 4: Copy and rewrite `device-guide.json`**

Preserve all three current steps and change their image paths to:

```text
/editable-content/images/guide/step-1.svg
/editable-content/images/guide/step-2.svg
/editable-content/images/guide/step-3.svg
```

Replace the top-level `"videoIds": []` property with:

```json
"videos": []
```

- [ ] **Step 5: Copy `quizzes.json` without changing data**

Preserve the three IDs and every existing title, question, choice, and
correctIndex exactly.

- [ ] **Step 6: Write the root editor README**

Include these exact sections and examples:

````markdown
# Editable App Content

Everything in this folder is copied into the Android app. Edit JSON as UTF-8.
Keep JSON commas and double quotes valid. Do not rename the fixed quiz IDs
`pre`, `post`, or `satisfaction`, and do not rename existing article IDs after
patients have started using the app.

## Replace a picture

1. Copy a PNG, JPG, JPEG, WebP, or SVG into the matching `images/` folder.
2. Use a lowercase filename without spaces, for example `wear-device.jpg`.
3. Update the JSON path, for example:
   `"image": "/editable-content/images/guide/wear-device.jpg"`.
4. Update `imageAlt` with a short Thai description when the field exists.

Recommended: approximately 1200 × 900 pixels and below 1 MB per image.

## Add a YouTube video

For `https://www.youtube.com/watch?v=dQw4w9WgXcQ`, store only the ID:

```json
{ "type": "youtube", "id": "dQw4w9WgXcQ", "title": "วิดีโอสาธิต" }
```

YouTube requires internet.

## Add an offline video

Copy an MP4 or WebM into `videos/`, then add:

```json
{
  "type": "file",
  "src": "/editable-content/videos/device-demo.mp4",
  "title": "วิดีโอสาธิตแบบออฟไลน์"
}
```

Keep local videos short and compressed because they increase APK size.

## Build and install

From `PT-Leg-Tracker/`:

```powershell
npm test
npm run build
npx cap sync android
cd android
.\gradlew assembleDebug
adb install -r app\build\outputs\apk\debug\app-debug.apk
```

Before distribution, open the home page, every knowledge article, every guide
step and video, and all three quizzes.
````

- [ ] **Step 7: Write `videos/README.md`**

```markdown
# Local Videos

Place `.mp4` or `.webm` files here and reference them from
`../device-guide.json` using `/editable-content/videos/<filename>`.
Prefer short, compressed MP4 (H.264 video with AAC audio) for broad Android
compatibility. Every local video increases the APK size.
```

- [ ] **Step 8: Validate JSON and ensure copied media exists**

Run from the repository root:

```powershell
$root = 'PT-Leg-Tracker/public/editable-content'
$jsonFiles = 'app.json','knowledge.json','device-guide.json','quizzes.json'
foreach ($name in $jsonFiles) {
  Get-Content -Raw (Join-Path $root $name) | ConvertFrom-Json | Out-Null
}
Get-ChildItem "$root/images/branding/logo.png", "$root/images/knowledge/*", "$root/images/guide/*"
```

Expected: all four JSON files parse and all copied files are listed.

- [ ] **Step 9: Verify the unchanged app still builds**

Run from `PT-Leg-Tracker/`:

```powershell
npm run build
```

Expected: exit 0; old imports still work during migration.

- [ ] **Step 10: Commit**

```powershell
git add PT-Leg-Tracker/public/editable-content
git commit -m "feat(app): add unified editable content package"
```

---

### Task 2: Add the Typed, Validated, Cached Content Loader

**Files:**
- Create: `PT-Leg-Tracker/src/lib/content.ts`
- Create: `PT-Leg-Tracker/src/lib/content.test.ts`

**Interfaces:**
- Produces: `loadAppContent(): Promise<AppContent>`
- Produces: `loadKnowledgeContent(): Promise<KnowledgeContent>`
- Produces: `loadDeviceGuide(): Promise<DeviceGuideContent>`
- Produces: `loadQuizContent(): Promise<QuizContent>`
- Produces: `clearContentCache(): void` for tests and deliberate reloads.
- Produces: exported interfaces `AppContent`, `KnowledgeContent`, `KnowledgeArticle`, `DeviceGuideContent`, `GuideVideo`, `QuizContent`, `QuizDefinition`, and `ContentError`.

- [ ] **Step 1: Write failing fetch/cache tests**

Create `src/lib/content.test.ts` with a fetch stub that returns per-URL JSON:

```ts
import { afterEach, describe, expect, it, vi } from 'vitest'
import {
  ContentError,
  clearContentCache,
  loadAppContent,
  loadDeviceGuide,
  loadKnowledgeContent,
  loadQuizContent,
} from './content'

function response(body: unknown, ok = true): Response {
  return { ok, json: async () => body } as Response
}

afterEach(() => {
  clearContentCache()
  vi.unstubAllGlobals()
})

describe('editable content loader', () => {
  it('loads and caches app content', async () => {
    const fetchMock = vi.fn().mockResolvedValue(
      response({ homeSubtitle: 'Smart OA Knee', logo: '/editable-content/images/branding/logo.png', logoAlt: 'logo' }),
    )
    vi.stubGlobal('fetch', fetchMock)

    const first = await loadAppContent()
    const second = await loadAppContent()

    expect(first).toEqual({ homeSubtitle: 'Smart OA Knee', logo: '/editable-content/images/branding/logo.png', logoAlt: 'logo' })
    expect(second).toBe(first)
    expect(fetchMock).toHaveBeenCalledTimes(1)
    expect(fetchMock).toHaveBeenCalledWith('/editable-content/app.json')
  })

  it('throws ContentError when a file is missing', async () => {
    vi.stubGlobal('fetch', vi.fn().mockResolvedValue(response({}, false)))
    await expect(loadAppContent()).rejects.toBeInstanceOf(ContentError)
  })

  it('throws ContentError when json parsing fails', async () => {
    vi.stubGlobal('fetch', vi.fn().mockResolvedValue({ ok: true, json: async () => { throw new SyntaxError('bad json') } }))
    await expect(loadAppContent()).rejects.toMatchObject({ file: 'app.json' })
  })
})
```

- [ ] **Step 2: Write failing validation tests**

Add tests using complete minimal objects:

```ts
it('accepts knowledge with unique article ids', async () => {
  vi.stubGlobal('fetch', vi.fn().mockResolvedValue(response({
    sections: [{
      id: 'knowledge',
      title: 'ความรู้',
      articles: [{ id: 'a1', title: 'หัวข้อ', body: 'เนื้อหา', image: '/editable-content/images/knowledge/a.jpg', imageAlt: 'ภาพ' }],
    }],
  })))
  await expect(loadKnowledgeContent()).resolves.toMatchObject({ sections: [{ id: 'knowledge' }] })
})

it('rejects duplicate article ids', async () => {
  const article = { id: 'same', title: 'หัวข้อ', body: 'เนื้อหา', image: '/editable-content/images/knowledge/a.jpg', imageAlt: 'ภาพ' }
  vi.stubGlobal('fetch', vi.fn().mockResolvedValue(response({
    sections: [
      { id: 'one', title: 'หนึ่ง', articles: [article] },
      { id: 'two', title: 'สอง', articles: [article] },
    ],
  })))
  await expect(loadKnowledgeContent()).rejects.toMatchObject({ file: 'knowledge.json' })
})

it('accepts youtube and local guide videos', async () => {
  vi.stubGlobal('fetch', vi.fn().mockResolvedValue(response({
    steps: [{ id: 'step-1', title: 'ขั้นตอน', caption: 'คำอธิบาย', image: '/editable-content/images/guide/step.jpg' }],
    videos: [
      { type: 'youtube', id: 'abc123', title: 'ออนไลน์' },
      { type: 'file', src: '/editable-content/videos/demo.mp4', title: 'ออฟไลน์' },
    ],
  })))
  await expect(loadDeviceGuide()).resolves.toMatchObject({ videos: [{ type: 'youtube' }, { type: 'file' }] })
})

it('rejects an unknown guide video type', async () => {
  vi.stubGlobal('fetch', vi.fn().mockResolvedValue(response({
    steps: [{ id: 'step-1', title: 'ขั้นตอน', caption: 'คำอธิบาย', image: '/editable-content/images/guide/step.jpg' }],
    videos: [{ type: 'vimeo', id: 'x', title: 'x' }],
  })))
  await expect(loadDeviceGuide()).rejects.toMatchObject({ file: 'device-guide.json' })
})

it('accepts the three fixed quiz ids', async () => {
  const q = (id: string, correctIndex?: number) => ({
    id,
    title: id,
    questions: [{ text: 'คำถาม', choices: ['ก', 'ข'], ...(correctIndex === undefined ? {} : { correctIndex }) }],
  })
  vi.stubGlobal('fetch', vi.fn().mockResolvedValue(response({ quizzes: [q('pre', 0), q('post', 1), q('satisfaction')] })))
  await expect(loadQuizContent()).resolves.toMatchObject({ quizzes: [{ id: 'pre' }, { id: 'post' }, { id: 'satisfaction' }] })
})

it('rejects an out-of-range correctIndex', async () => {
  const quiz = { id: 'pre', title: 'pre', questions: [{ text: 'คำถาม', choices: ['ก'], correctIndex: 1 }] }
  vi.stubGlobal('fetch', vi.fn().mockResolvedValue(response({ quizzes: [quiz, { ...quiz, id: 'post' }, { ...quiz, id: 'satisfaction', questions: [{ text: 'คำถาม', choices: ['ก'] }] }] })))
  await expect(loadQuizContent()).rejects.toMatchObject({ file: 'quizzes.json' })
})
```

- [ ] **Step 3: Run tests to verify RED**

Run from `PT-Leg-Tracker/`:

```powershell
npm test -- src/lib/content.test.ts
```

Expected: FAIL because `./content` does not exist.

- [ ] **Step 4: Implement types, cache, fetch, and `ContentError`**

Create `src/lib/content.ts` with these exact public types and file mapping:

```ts
export interface AppContent {
  homeSubtitle: string
  logo: string
  logoAlt: string
}

export interface KnowledgeArticle {
  id: string
  title: string
  body: string
  image: string
  imageAlt: string
}

export interface KnowledgeSection {
  id: string
  title: string
  articles: KnowledgeArticle[]
}

export interface KnowledgeContent { sections: KnowledgeSection[] }

export interface GuideStep {
  id: string
  title: string
  caption: string
  image: string
}

export type GuideVideo =
  | { type: 'youtube'; id: string; title: string }
  | { type: 'file'; src: string; title: string }

export interface DeviceGuideContent {
  steps: GuideStep[]
  videos: GuideVideo[]
}

export type QuizId = 'pre' | 'post' | 'satisfaction'
export interface QuizQuestion { text: string; choices: string[]; correctIndex?: number }
export interface QuizDefinition { id: QuizId; title: string; questions: QuizQuestion[] }
export interface QuizContent { quizzes: QuizDefinition[] }

export class ContentError extends Error {
  constructor(public file: string) {
    super(`invalid editable content: ${file}`)
  }
}

const cache = new Map<string, unknown>()

export function clearContentCache(): void {
  cache.clear()
}

async function loadFile<T>(file: string, validate: (value: unknown) => T): Promise<T> {
  if (cache.has(file)) return cache.get(file) as T
  try {
    const res = await fetch(`/editable-content/${file}`)
    if (!res.ok) throw new ContentError(file)
    const value = validate(await res.json())
    cache.set(file, value)
    return value
  } catch (error) {
    if (error instanceof ContentError) throw error
    throw new ContentError(file)
  }
}
```

- [ ] **Step 5: Implement focused validators**

Add these private helpers and validators after `loadFile`:

```ts
function record(value: unknown): Record<string, unknown> {
  if (typeof value !== 'object' || value === null || Array.isArray(value)) throw new Error()
  return value as Record<string, unknown>
}

function text(value: unknown): string {
  if (typeof value !== 'string' || value.trim() === '') throw new Error()
  return value
}

function list(value: unknown): unknown[] {
  if (!Array.isArray(value)) throw new Error()
  return value
}

function localPath(value: unknown): string {
  const path = text(value)
  if (!path.startsWith('/editable-content/')) throw new Error()
  return path
}

function validateApp(value: unknown): AppContent {
  const item = record(value)
  return {
    homeSubtitle: text(item.homeSubtitle),
    logo: localPath(item.logo),
    logoAlt: text(item.logoAlt),
  }
}

function validateKnowledge(value: unknown): KnowledgeContent {
  const articleIds = new Set<string>()
  const sections = list(record(value).sections).map((sectionValue): KnowledgeSection => {
    const section = record(sectionValue)
    const articles = list(section.articles).map((articleValue): KnowledgeArticle => {
      const article = record(articleValue)
      const id = text(article.id)
      if (articleIds.has(id)) throw new Error()
      articleIds.add(id)
      return {
        id,
        title: text(article.title),
        body: text(article.body),
        image: localPath(article.image),
        imageAlt: text(article.imageAlt),
      }
    })
    return { id: text(section.id), title: text(section.title), articles }
  })
  return { sections }
}

function validateGuide(value: unknown): DeviceGuideContent {
  const item = record(value)
  const stepIds = new Set<string>()
  const steps = list(item.steps).map((stepValue): GuideStep => {
    const step = record(stepValue)
    const id = text(step.id)
    if (stepIds.has(id)) throw new Error()
    stepIds.add(id)
    return {
      id,
      title: text(step.title),
      caption: text(step.caption),
      image: localPath(step.image),
    }
  })
  if (steps.length === 0) throw new Error()

  const videos = list(item.videos).map((videoValue): GuideVideo => {
    const video = record(videoValue)
    const title = text(video.title)
    if (video.type === 'youtube') {
      return { type: 'youtube', id: text(video.id), title }
    }
    if (video.type === 'file') {
      const src = localPath(video.src)
      if (!/\.(mp4|webm)$/i.test(src)) throw new Error()
      return { type: 'file', src, title }
    }
    throw new Error()
  })
  return { steps, videos }
}

function validateQuizzes(value: unknown): QuizContent {
  const fixedIds: QuizId[] = ['pre', 'post', 'satisfaction']
  const seen = new Set<QuizId>()
  const quizzes = list(record(value).quizzes).map((quizValue): QuizDefinition => {
    const quiz = record(quizValue)
    const rawId = text(quiz.id)
    if (!fixedIds.includes(rawId as QuizId) || seen.has(rawId as QuizId)) throw new Error()
    const id = rawId as QuizId
    seen.add(id)
    const questions = list(quiz.questions).map((questionValue): QuizQuestion => {
      const question = record(questionValue)
      const choices = list(question.choices).map(text)
      if (choices.length < 2) throw new Error()
      const result: QuizQuestion = { text: text(question.text), choices }
      if (id === 'satisfaction') {
        if (question.correctIndex !== undefined) throw new Error()
      } else {
        if (
          !Number.isInteger(question.correctIndex) ||
          (question.correctIndex as number) < 0 ||
          (question.correctIndex as number) >= choices.length
        ) throw new Error()
        result.correctIndex = question.correctIndex as number
      }
      return result
    })
    if (questions.length === 0) throw new Error()
    return { id, title: text(quiz.title), questions }
  })
  if (quizzes.length !== fixedIds.length || fixedIds.some((id) => !seen.has(id))) throw new Error()
  return { quizzes }
}
```

Export loaders using the validators:

```ts
export const loadAppContent = () => loadFile('app.json', validateApp)
export const loadKnowledgeContent = () => loadFile('knowledge.json', validateKnowledge)
export const loadDeviceGuide = () => loadFile('device-guide.json', validateGuide)
export const loadQuizContent = () => loadFile('quizzes.json', validateQuizzes)
```

- [ ] **Step 6: Run focused and full tests**

```powershell
npm test -- src/lib/content.test.ts
npm test
```

Expected: focused tests pass; full suite remains green.

- [ ] **Step 7: Type-check and commit**

```powershell
npm run build
git add src/lib/content.ts src/lib/content.test.ts
git commit -m "feat(app): load and validate editable content"
```

---

### Task 3: Migrate Branding and Knowledge Views

**Files:**
- Modify: `PT-Leg-Tracker/src/views/HomeView.vue`
- Modify: `PT-Leg-Tracker/src/views/KnowledgeView.vue`
- Modify: `PT-Leg-Tracker/src/views/ArticleView.vue`
- Delete: `PT-Leg-Tracker/src/assets/logo.png`
- Delete: `PT-Leg-Tracker/src/content/knowledge.json`
- Delete: `PT-Leg-Tracker/public/knowledge/*`

**Interfaces:**
- Consumes: `loadAppContent(): Promise<AppContent>` and `loadKnowledgeContent(): Promise<KnowledgeContent>` from Task 2.
- Error copy: `โหลดเนื้อหาไม่สำเร็จ กรุณาตรวจสอบ editable-content/README.md`.

- [ ] **Step 1: Add a reusable async state pattern to `HomeView.vue`**

Replace the compiled logo import with:

```ts
import { onMounted, ref } from 'vue'
import { loadAppContent, type AppContent } from '@/lib/content'

const appContent = ref<AppContent | null>(null)
const loading = ref(true)
const contentError = ref(false)

onMounted(async () => {
  try {
    appContent.value = await loadAppContent()
  } catch {
    contentError.value = true
  } finally {
    loading.value = false
  }
})
```

Render branding only when loaded and keep the rest of the home menu usable.
Replace the existing logo, greeting, and subtitle block with:

```vue
<p v-if="loading">กำลังโหลดเนื้อหา…</p>
<p v-else-if="contentError" class="error">
  โหลดเนื้อหาไม่สำเร็จ กรุณาตรวจสอบ public/editable-content/README.md
</p>
<img v-if="appContent" :src="appContent.logo" :alt="appContent.logoAlt" class="logo" />
<h1>สวัสดี คุณ{{ store.profile?.name }}</h1>
<p v-if="appContent" class="sub">{{ appContent.homeSubtitle }}</p>
```

Keep all buttons unchanged.

- [ ] **Step 2: Load knowledge asynchronously in `KnowledgeView.vue`**

Use:

```ts
import { onMounted, ref } from 'vue'
import { loadKnowledgeContent, type KnowledgeSection } from '@/lib/content'

const sections = ref<KnowledgeSection[]>([])
const loading = ref(true)
const contentError = ref(false)

onMounted(async () => {
  try {
    sections.value = (await loadKnowledgeContent()).sections
  } catch {
    contentError.value = true
  } finally {
    loading.value = false
  }
})
```

Before the existing section loop render:

```vue
<p v-if="loading">กำลังโหลดเนื้อหา…</p>
<p v-else-if="contentError" class="error">
  โหลดเนื้อหาไม่สำเร็จ กรุณาตรวจสอบ public/editable-content/README.md
</p>
<template v-else>
  <!-- existing section loop -->
</template>
```

- [ ] **Step 3: Load and select articles asynchronously in `ArticleView.vue`**

Use refs for `loading`, `contentError`, and `KnowledgeContent | null`. Load once
on mount:

```ts
const content = ref<KnowledgeContent | null>(null)
const loading = ref(true)
const contentError = ref(false)

onMounted(async () => {
  try {
    content.value = await loadKnowledgeContent()
  } catch {
    contentError.value = true
  } finally {
    loading.value = false
  }
})
```

Compute the article from loaded content and `route.params.id`:

```ts
const article = computed(() =>
  content.value?.sections.flatMap((section) => section.articles)
    .find((item) => item.id === route.params.id),
)
```

Keep the current watchEffect that writes `contentProgress` after an article is
found. Template precedence must be loading → content error → article → not found.

- [ ] **Step 4: Add shared view error styling**

In each touched view's scoped style, add:

```css
.error {
  color: #b91c1c;
  font-weight: 600;
}
```

- [ ] **Step 5: Remove migrated legacy files**

```powershell
git rm PT-Leg-Tracker/src/assets/logo.png
git rm PT-Leg-Tracker/src/content/knowledge.json
git rm -r PT-Leg-Tracker/public/knowledge
```

- [ ] **Step 6: Verify tests and build**

Run from `PT-Leg-Tracker/`:

```powershell
npm test
npm run build
```

Expected: all tests and the Vue type-check/build pass; production output contains
`editable-content/app.json`, `editable-content/knowledge.json`, and all migrated
images.

- [ ] **Step 7: Commit**

```powershell
git add src/views/HomeView.vue src/views/KnowledgeView.vue src/views/ArticleView.vue
git add -u src/assets src/content public/knowledge
git commit -m "feat(app): read branding and knowledge from editable content"
```

---

### Task 4: Migrate the Device Guide and Support Local Videos

**Files:**
- Modify: `PT-Leg-Tracker/src/views/DeviceGuideView.vue`
- Delete: `PT-Leg-Tracker/src/content/deviceGuide.json`
- Delete: `PT-Leg-Tracker/public/guide/*`

**Interfaces:**
- Consumes: `loadDeviceGuide(): Promise<DeviceGuideContent>` and discriminated union `GuideVideo` from Task 2.
- YouTube source format: `https://www.youtube.com/embed/${video.id}`.
- Local source format: the validated `video.src` path.

- [ ] **Step 1: Replace compiled guide import with async state**

Use:

```ts
import { computed, onMounted, onUnmounted, ref } from 'vue'
import { loadDeviceGuide, type DeviceGuideContent } from '@/lib/content'

const guide = ref<DeviceGuideContent | null>(null)
const loading = ref(true)
const contentError = ref(false)
const currentStep = computed(() => guide.value?.steps[stepIndex.value] ?? null)

onMounted(async () => {
  window.addEventListener('online', setOnline)
  window.addEventListener('offline', setOffline)
  try {
    guide.value = await loadDeviceGuide()
  } catch {
    contentError.value = true
  } finally {
    loading.value = false
  }
})
```

Keep listener cleanup unchanged.

- [ ] **Step 2: Add loading/error/ready template branches**

At the top of `<main>`:

```vue
<p v-if="loading">กำลังโหลดเนื้อหา…</p>
<p v-else-if="contentError" class="error">
  โหลดเนื้อหาไม่สำเร็จ กรุณาตรวจสอบ public/editable-content/README.md
</p>
<template v-else-if="guide && currentStep">
  <!-- guide card and video section -->
</template>
```

Update step references from `guide.steps` to `guide!.steps` only where Vue's
template type narrowing requires it. Preserve next/previous behavior.

- [ ] **Step 3: Render mixed video entries**

Replace the old `videoIds` block with:

```vue
<h2>วิดีโอสาธิต</h2>
<p v-if="guide.videos.length === 0">ยังไม่มีวิดีโอ</p>
<template v-for="video in guide.videos" :key="video.type === 'youtube' ? `yt-${video.id}` : `file-${video.src}`">
  <div v-if="video.type === 'youtube' && online" class="video">
    <iframe
      :src="`https://www.youtube.com/embed/${video.id}`"
      :title="video.title"
      allowfullscreen
    ></iframe>
  </div>
  <p v-else-if="video.type === 'youtube'" class="card">
    วิดีโอ YouTube ต้องใช้อินเทอร์เน็ต กรุณาเชื่อมต่อแล้วลองใหม่
  </p>
  <div v-else class="local-video">
    <video controls playsinline preload="metadata" :src="video.src"></video>
    <p>{{ video.title }}</p>
  </div>
</template>
```

Add:

```css
.local-video {
  margin-bottom: 0.75rem;
}

.local-video video {
  display: block;
  width: 100%;
  border-radius: 0.75rem;
  background: #000;
}

.error {
  color: #b91c1c;
  font-weight: 600;
}
```

- [ ] **Step 4: Remove migrated legacy guide files**

```powershell
git rm PT-Leg-Tracker/src/content/deviceGuide.json
git rm -r PT-Leg-Tracker/public/guide
```

- [ ] **Step 5: Verify**

```powershell
npm test -- src/lib/content.test.ts
npm run build
```

Expected: loader tests and build pass; no `@/content/deviceGuide.json` imports
remain (`rg "deviceGuide.json|videoIds" src` returns no matches).

- [ ] **Step 6: Commit**

```powershell
git add src/views/DeviceGuideView.vue
git add -u src/content public/guide
git commit -m "feat(app): load editable guide images and videos"
```

---

### Task 5: Migrate Quizzes, Remove Legacy Content, and Run Distribution Gates

**Files:**
- Modify: `PT-Leg-Tracker/src/views/QuizView.vue`
- Delete: `PT-Leg-Tracker/src/content/quizzes.json`
- Verify: all app content/media and Android package.

**Interfaces:**
- Consumes: `loadQuizContent(): Promise<QuizContent>` from Task 2.
- Preserves: saved result `quizId` values and `QuizResult` export format.

- [ ] **Step 1: Replace compiled quiz import with async state**

Use:

```ts
import { computed, onMounted, ref, watch } from 'vue'
import { loadQuizContent, type QuizContent } from '@/lib/content'

const data = ref<QuizContent | null>(null)
const loading = ref(true)
const contentError = ref(false)
const quiz = computed(() => data.value?.quizzes.find((q) => q.id === route.params.quizId))

onMounted(async () => {
  try {
    data.value = await loadQuizContent()
  } catch {
    contentError.value = true
  } finally {
    loading.value = false
  }
})
```

Keep answer reset, scoring, persisted result shape, and navigation unchanged.
The existing cast to `QuizResult['quizId']` may be removed because `QuizId` is
already the same union.

- [ ] **Step 2: Add loading and content-error states**

Template precedence:

```vue
<p v-if="loading">กำลังโหลดเนื้อหา…</p>
<p v-else-if="contentError" class="error">
  โหลดเนื้อหาไม่สำเร็จ กรุณาตรวจสอบ public/editable-content/README.md
</p>
<p v-else-if="!quiz">ไม่พบแบบทดสอบ</p>
```

Keep the existing quiz and completion branches after these conditions. Add the
same `.error` style used in the other views.

- [ ] **Step 3: Remove the final legacy JSON and prove no old imports remain**

```powershell
git rm PT-Leg-Tracker/src/content/quizzes.json
rg "@/content|src/content|/knowledge/|/guide/|assets/logo.png" src public/editable-content
```

Expected: no source imports or old media paths. Matches inside the editor README
are acceptable only when they start with `/editable-content/`.

- [ ] **Step 4: Run all automated gates**

From `PT-Leg-Tracker/`:

```powershell
npm test
npm run build
npx eslint src/lib/content.ts src/lib/content.test.ts src/views/HomeView.vue src/views/KnowledgeView.vue src/views/ArticleView.vue src/views/DeviceGuideView.vue src/views/QuizView.vue
npx prettier --check src/lib/content.ts src/lib/content.test.ts src/views/HomeView.vue src/views/KnowledgeView.vue src/views/ArticleView.vue src/views/DeviceGuideView.vue src/views/QuizView.vue public/editable-content/*.json public/editable-content/*.md
```

Expected: all commands exit 0.

- [ ] **Step 5: Sync and build Android**

```powershell
npx cap sync android
cd android
.\gradlew assembleDebug
```

Expected APK:
`PT-Leg-Tracker/android/app/build/outputs/apk/debug/app-debug.apk`.

- [ ] **Step 6: Install on the connected MuMu instance**

```powershell
adb devices -l
adb -s emulator-5554 install -r app\build\outputs\apk\debug\app-debug.apk
adb -s emulator-5554 shell am force-stop com.unknownaimo.ptlegtracker
adb -s emulator-5554 shell monkey -p com.unknownaimo.ptlegtracker -c android.intent.category.LAUNCHER 1
```

If MuMu reports a different single device serial, substitute that listed serial
for `emulator-5554`.

- [ ] **Step 7: Run the MuMu smoke test**

Verify:

1. Home logo and subtitle load from `app.json`.
2. Knowledge list thumbnails and every article image load.
3. All three guide steps navigate and display images.
4. Empty `videos` shows `ยังไม่มีวิดีโอ`.
5. Temporarily add `{ "type": "youtube", "id": "dQw4w9WgXcQ", "title": "ทดสอบ YouTube" }`, rebuild/sync/install, and verify an iframe appears while online; then restore `videos: []`.
6. For local playback, copy a known playable H.264/AAC MP4 to `public/editable-content/videos/device-demo.mp4`, add the exact file entry from the README, rebuild/sync/install, verify controls and playback, then remove the test file and restore `videos: []`. If no test MP4 is available, record this single media-playback check as deferred; parser, build, and rendering-path tests still must pass.
7. Open pre-test, post-test, and satisfaction routes and answer at least one question in each without saving disposable research data for real patients.
8. Disable MuMu networking and relaunch: branding, articles, guide images, quizzes, and any packaged local video still load; YouTube shows the offline message.
9. `adb logcat` has no `FATAL EXCEPTION`, missing editable-content file, or WebView `ERR_FILE_NOT_FOUND` lines.

- [ ] **Step 8: Commit**

```powershell
git add src/views/QuizView.vue
git add -u src/content
git commit -m "feat(app): load quizzes from editable content"
```

- [ ] **Step 9: Final repository-scope check**

```powershell
git status --short
git log -5 --oneline
```

Expected: no task-created uncommitted files. Preserve pre-existing unrelated
changes to `firmware/README.md` and untracked `AGENTS.md`; do not stage or modify
them.
