# App v4 UI Rebrand Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Brand the app with the college logo, restyle to the logo's navy/maroon/silver palette with the Kanit font, and add CSS-only animation polish.

**Architecture:** Pure presentation work in the Vue app (`PT-Leg-Tracker/`). CSS custom-property retheme in `main.css`, logo assets produced by a one-off sharp script, Android icon/splash via `@capacitor/assets`, animations as CSS transitions/keyframes plus one small canvas confetti component. No store, router-config, server, or logic changes.

**Tech Stack:** Vue 3 + Vite, `@fontsource/kanit`, `sharp` (devDep, asset prep), `@capacitor/assets` (icon/splash generation).

## Global Constraints

- Spec: `docs/superpowers/specs/2026-07-19-app-v4-ui-rebrand-design.md`.
- App commands run from `PT-Leg-Tracker/`, NOT repo root.
- Font sizes, touch-target sizes (`min-height: 3rem` buttons, `html { font-size: 20px }`), and all Thai copy unchanged.
- Every animation wrapped in `@media (prefers-reduced-motion: no-preference)`.
- No animation libraries. Confetti is hand-rolled canvas.
- After each task: `npm run build` green. `npm test` (44 vitest) must stay green throughout — these tasks touch no tested logic; a red test means you broke something out of scope.
- Palette roles fixed (navy primary `#24395e`, maroon accent `#8c2f39`, silver muted `#c6ccd6`); exact hex may be eye-tuned against the logo.
- Commit after every task. No AI attribution lines in commits.

---

### Task 1: Palette tokens + Kanit font

**Files:**
- Modify: `PT-Leg-Tracker/src/assets/main.css:1-12` (tokens), `:27-33` (body font)
- Modify: `PT-Leg-Tracker/src/main.ts` (font imports)
- Modify: `PT-Leg-Tracker/package.json` (dependency)

**Interfaces:**
- Produces: CSS vars `--c-primary #24395e`, `--c-primary-dark #1a2b47`, `--c-accent #8c2f39`, `--c-accent-dark #6e232c`, `--c-border #c6ccd6`; `button.accent` class. Later tasks use these names.

- [ ] **Step 1: Install font**

```bash
cd PT-Leg-Tracker
npm install @fontsource/kanit
```

- [ ] **Step 2: Import weights in `src/main.ts`** — add at the top, before `./assets/main.css` import:

```ts
import '@fontsource/kanit/400.css'
import '@fontsource/kanit/600.css'
import '@fontsource/kanit/700.css'
```

- [ ] **Step 3: Retheme tokens in `main.css`** — replace the `:root` block:

```css
:root {
  --c-bg: #f7f5f0;
  --c-text: #1c2534;
  --c-primary: #24395e;
  --c-primary-dark: #1a2b47;
  --c-accent: #8c2f39;
  --c-accent-dark: #6e232c;
  --c-card: #ffffff;
  --c-border: #c6ccd6;
  --c-muted: #56607a;
  --radius-card: 1rem;
  --radius-btn: 0.75rem;
  --shadow-card: 0 1px 3px rgb(28 37 52 / 0.08), 0 1px 2px rgb(28 37 52 / 0.05);
}
```

- [ ] **Step 4: Font stack in `main.css` body rule** — change `font-family` line to:

```css
  font-family: 'Kanit', 'Sarabun', 'Noto Sans Thai', system-ui, sans-serif;
```

- [ ] **Step 5: Add accent button class in `main.css`** — after the `button.secondary` block:

```css
button.accent {
  background: var(--c-accent);
  border-color: var(--c-accent-dark);
  color: #fff;
}
```

- [ ] **Step 6: Use accent for the session stop button** — in `PT-Leg-Tracker/src/views/SessionView.vue`, running-phase template, change:

```html
<button class="primary wide" @click="stop"><AppIcon name="stop" /> สั่งเครื่องหยุด</button>
```

to:

```html
<button class="accent wide" @click="stop"><AppIcon name="stop" /> สั่งเครื่องหยุด</button>
```

- [ ] **Step 7: Verify**

Run: `npm run build && npm test`
Expected: build green, 44 tests pass.
Run: `npm run dev`, open browser — navy headers/buttons, Kanit renders Thai text (letterforms visibly changed).

- [ ] **Step 8: Commit**

```bash
git add -A
git commit -m "feat(ui): navy/maroon palette tokens + Kanit font"
```

---

### Task 2: Logo asset prep script + home screen logo

**Files:**
- Create: `PT-Leg-Tracker/scripts/prepare-logo.mjs`
- Create (generated): `PT-Leg-Tracker/src/assets/logo.png`, `PT-Leg-Tracker/assets/icon-only.png`, `PT-Leg-Tracker/assets/splash.png`, `PT-Leg-Tracker/assets/splash-dark.png`
- Modify: `PT-Leg-Tracker/src/views/HomeView.vue`
- Modify: `PT-Leg-Tracker/package.json` (sharp devDep)

**Interfaces:**
- Consumes: source logo `../assets/Logo-removebg.png` (repo root).
- Produces: `src/assets/logo.png` (trimmed, transparent) imported by HomeView; `assets/icon-only.png` (1024×1024, cream bg), `assets/splash.png` + `splash-dark.png` (2732×2732, cream bg) consumed by Task 3.

- [ ] **Step 1: Install sharp**

```bash
cd PT-Leg-Tracker
npm install -D sharp
```

- [ ] **Step 2: Write `scripts/prepare-logo.mjs`**

```js
// One-off: derive app logo assets from the college logo source.
// Usage: node scripts/prepare-logo.mjs
import sharp from 'sharp'
import { mkdirSync } from 'node:fs'

const SRC = '../assets/Logo-removebg.png'
const CREAM = { r: 247, g: 245, b: 240, alpha: 1 }
mkdirSync('assets', { recursive: true })

const trimmed = await sharp(SRC).trim().toBuffer()
await sharp(trimmed).resize(800, 800, { fit: 'inside' }).toFile('src/assets/logo.png')

// launcher icon: logo on cream, 12% padding, 1024 square
const icon = await sharp(trimmed)
  .resize(800, 800, { fit: 'inside' })
  .toBuffer()
await sharp({ create: { width: 1024, height: 1024, channels: 4, background: CREAM } })
  .composite([{ input: icon, gravity: 'center' }])
  .png()
  .toFile('assets/icon-only.png')

// splash: logo centered on cream, 2732 square (capacitor-assets max size)
const splashLogo = await sharp(trimmed).resize(1000, 1000, { fit: 'inside' }).toBuffer()
await sharp({ create: { width: 2732, height: 2732, channels: 4, background: CREAM } })
  .composite([{ input: splashLogo, gravity: 'center' }])
  .png()
  .toFile('assets/splash.png')
await sharp('assets/splash.png').toFile('assets/splash-dark.png')
console.log('done: src/assets/logo.png, assets/icon-only.png, assets/splash.png, assets/splash-dark.png')
```

- [ ] **Step 3: Run it**

Run: `node scripts/prepare-logo.mjs`
Expected: `done: ...` line; four PNGs exist. Open `assets/icon-only.png` — logo centered on cream, no clipping.

- [ ] **Step 4: Add logo to HomeView** — in `src/views/HomeView.vue`:

Script block, add import:

```ts
import logoUrl from '@/assets/logo.png'
```

Template, insert as first child of `<main class="page">`, before `<h1>`:

```html
<img :src="logoUrl" alt="ตราวิทยาลัยการอาชีพฝาง" class="logo" />
```

Style block, add:

```css
.logo {
  display: block;
  width: 11rem;
  max-width: 60%;
  margin: 0 auto 0.75rem;
}

@media (prefers-reduced-motion: no-preference) {
  .logo {
    animation: logo-rise 0.6s ease-out both;
  }

  @keyframes logo-rise {
    from {
      opacity: 0;
      transform: translateY(0.75rem);
    }
    to {
      opacity: 1;
      transform: none;
    }
  }
}
```

- [ ] **Step 5: Verify**

Run: `npm run build && npm test`
Expected: green. `npm run dev` — logo fades in above greeting.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "feat(ui): college logo on home + asset prep script"
```

---

### Task 3: Android launcher icon + splash

**Files:**
- Modify: `PT-Leg-Tracker/package.json` (devDep `@capacitor/assets`)
- Generated: `PT-Leg-Tracker/android/app/src/main/res/**` (mipmaps, splash drawables)

**Interfaces:**
- Consumes: `PT-Leg-Tracker/assets/icon-only.png`, `splash.png`, `splash-dark.png` from Task 2.

- [ ] **Step 1: Generate**

```bash
cd PT-Leg-Tracker
npm install -D @capacitor/assets
npx capacitor-assets generate --android
```

Expected: output lists generated icon mipmaps + splash images under `android/app/src/main/res/`.

- [ ] **Step 2: Inspect one generated icon**

Open `android/app/src/main/res/mipmap-xxxhdpi/ic_launcher.png` (or nearest) — logo visible, not squashed. If `@capacitor/assets` produced adaptive-icon foreground that crops the badge too tight, rerun Task 2 script with larger padding (resize 700 instead of 800 in the icon block) and regenerate.

- [ ] **Step 3: Verify build still assembles**

```bash
npm run build && npx cap sync android
cd android && .\gradlew assembleDebug
```

Expected: BUILD SUCCESSFUL.

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "feat(android): college logo launcher icon + splash"
```

---

### Task 4: Global animations (page transitions, press feedback, stagger)

**Files:**
- Modify: `PT-Leg-Tracker/src/App.vue`
- Modify: `PT-Leg-Tracker/src/assets/main.css`
- Modify: `PT-Leg-Tracker/src/views/HomeView.vue`

**Interfaces:**
- Produces: global CSS classes `.stagger` (fade-in-up with `--i`-indexed delay) used by Task 4 (Home) and available to any view; `page` transition name on the router view.

- [ ] **Step 1: Router transition in `App.vue`** — replace `<RouterView />` with:

```html
<RouterView v-slot="{ Component }">
  <Transition name="page" mode="out-in">
    <component :is="Component" />
  </Transition>
</RouterView>
```

- [ ] **Step 2: Global animation CSS** — append to `main.css`:

```css
@media (prefers-reduced-motion: no-preference) {
  .page-enter-active,
  .page-leave-active {
    transition:
      opacity 0.18s ease,
      transform 0.18s ease;
  }

  .page-enter-from {
    opacity: 0;
    transform: translateX(0.75rem);
  }

  .page-leave-to {
    opacity: 0;
    transform: translateX(-0.75rem);
  }

  button,
  .card {
    transition: transform 0.12s ease;
  }

  button:active,
  .card:active {
    transform: scale(0.97);
  }

  .stagger {
    animation: fade-in-up 0.45s ease-out both;
    animation-delay: calc(var(--i, 0) * 70ms);
  }

  @keyframes fade-in-up {
    from {
      opacity: 0;
      transform: translateY(0.6rem);
    }
    to {
      opacity: 1;
      transform: none;
    }
  }
}
```

- [ ] **Step 3: Stagger the home menu** — in `HomeView.vue` template, add class + index to the four BigButtons and settings link:

```html
<BigButton class="stagger" style="--i: 0" icon="book-open" label="ความรู้เรื่องข้อเข่าเสื่อม" to="knowledge" />
<BigButton class="stagger" style="--i: 1" icon="wrench" label="วิธีใช้เครื่องบริหารเข่า" to="guide" />
<BigButton class="stagger" style="--i: 2" icon="activity" label="เริ่มออกกำลังกาย" to="session" />
<BigButton class="stagger" style="--i: 3" icon="bar-chart" label="บันทึกของฉัน" to="records" />
```

(`class`/`style` fall through to BigButton's root `<button>` automatically.)

- [ ] **Step 4: Stagger the records history rows** — in `PT-Leg-Tracker/src/views/RecordsView.vue`, on the history row div, add:

```html
<div
  v-for="(r, i) in sorted"
  :key="r.id"
  class="card row stagger"
  :style="{ '--i': Math.min(i, 8) }"
>
```

(`Math.min` caps the delay so long histories don't wait forever.)

- [ ] **Step 5: Verify**

Run: `npm run build && npm test` → green. `npm run dev` — navigate between screens: fade-slide; home cards cascade in; records rows cascade; press a card: shrinks slightly.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "feat(ui): page transitions, press feedback, staggered cards"
```

---

### Task 5: Session screen animations

**Files:**
- Modify: `PT-Leg-Tracker/src/views/SessionView.vue` (template + style only)

- [ ] **Step 1: Pulsing ring + rep pop in running template** — replace the running-phase template block with:

```html
<template v-else-if="phase === 'running'">
  <div class="clock-ring">
    <p class="clock">{{ clock }}</p>
  </div>
  <p class="reps">
    จำนวนครั้ง:
    <Transition name="pop" mode="out-in">
      <span :key="reps" class="reps-num">{{ reps }}</span>
    </Transition>
  </p>
  <p class="hint">
    {{ connLost ? 'กำลังเชื่อมต่อ…' : 'เครื่องกำลังทำงาน บริหารเข่าตามจังหวะของอุปกรณ์' }}
  </p>
  <button class="accent wide" @click="stop"><AppIcon name="stop" /> สั่งเครื่องหยุด</button>
</template>
```

- [ ] **Step 2: Styles** — append inside the existing `<style scoped>`:

```css
.clock-ring {
  width: 14rem;
  height: 14rem;
  margin: 2rem auto 0.5rem;
  border-radius: 50%;
  border: 4px solid var(--c-primary);
  display: flex;
  align-items: center;
  justify-content: center;
}

.clock-ring .clock {
  margin: 0;
  font-size: 3rem;
}

.reps-num {
  display: inline-block;
  font-weight: 700;
  color: var(--c-primary);
}

@media (prefers-reduced-motion: no-preference) {
  .clock-ring {
    animation: ring-pulse 2s ease-in-out infinite;
  }

  @keyframes ring-pulse {
    0%,
    100% {
      box-shadow: 0 0 0 0 rgb(36 57 94 / 0.35);
    }
    50% {
      box-shadow: 0 0 0 14px rgb(36 57 94 / 0);
    }
  }

  .pop-enter-active {
    animation: rep-pop 0.3s ease-out;
  }

  @keyframes rep-pop {
    0% {
      transform: scale(1.6);
    }
    100% {
      transform: scale(1);
    }
  }

  .breathe {
    animation: breathe 2.6s ease-in-out infinite;
  }

  @keyframes breathe {
    0%,
    100% {
      transform: scale(1);
    }
    50% {
      transform: scale(1.02);
    }
  }
}
```

- [ ] **Step 3: Breathing start button** — in the before/starting template, add `breathe` class to the start button:

```html
<button
  class="primary wide breathe"
  :disabled="painBefore === null || phase === 'starting'"
  @click="start"
>
```

- [ ] **Step 4: Verify**

Run: `npm run build && npm test` → green. Dev server + `server/scripts/dev.ps1`: start session — ring pulses, rep number pops each change, start button breathes while idle.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "feat(session): pulsing timer ring, rep pop, breathing start"
```

---

### Task 6: Confetti on finish + chart draw-in

**Files:**
- Create: `PT-Leg-Tracker/src/components/ConfettiBurst.vue`
- Modify: `PT-Leg-Tracker/src/views/SessionView.vue` (mount confetti in after-phase)
- Modify: `PT-Leg-Tracker/src/components/TrendChart.vue` (draw-in)

**Interfaces:**
- Produces: `<ConfettiBurst />` — props: none; renders a fixed full-screen canvas, plays ~1.8 s particle burst once on mount, then removes itself (v-if via internal flag). Respects reduced motion by rendering nothing.

- [ ] **Step 1: Write `ConfettiBurst.vue`**

```vue
<script setup lang="ts">
import { onMounted, onUnmounted, ref } from 'vue'

const canvas = ref<HTMLCanvasElement | null>(null)
const done = ref(false)
let raf = 0

onMounted(() => {
  if (window.matchMedia('(prefers-reduced-motion: reduce)').matches) {
    done.value = true
    return
  }
  const el = canvas.value!
  el.width = window.innerWidth
  el.height = window.innerHeight
  const ctx = el.getContext('2d')!
  const colors = ['#24395e', '#8c2f39', '#c6ccd6', '#e3b341']
  const parts = Array.from({ length: 120 }, () => ({
    x: el.width / 2,
    y: el.height * 0.35,
    vx: (Math.random() - 0.5) * 14,
    vy: -Math.random() * 12 - 4,
    size: Math.random() * 8 + 4,
    color: colors[Math.floor(Math.random() * colors.length)],
    rot: Math.random() * Math.PI,
    vr: (Math.random() - 0.5) * 0.3,
  }))
  const t0 = performance.now()
  const tick = (t: number) => {
    const dt = (t - t0) / 1000
    ctx.clearRect(0, 0, el.width, el.height)
    for (const p of parts) {
      p.x += p.vx
      p.y += p.vy
      p.vy += 0.35
      p.rot += p.vr
      ctx.save()
      ctx.translate(p.x, p.y)
      ctx.rotate(p.rot)
      ctx.globalAlpha = Math.max(0, 1 - dt / 1.8)
      ctx.fillStyle = p.color
      ctx.fillRect(-p.size / 2, -p.size / 2, p.size, p.size * 0.6)
      ctx.restore()
    }
    if (dt < 1.8) raf = requestAnimationFrame(tick)
    else done.value = true
  }
  raf = requestAnimationFrame(tick)
})

onUnmounted(() => cancelAnimationFrame(raf))
</script>

<template>
  <canvas v-if="!done" ref="canvas" class="confetti" aria-hidden="true"></canvas>
</template>

<style scoped>
.confetti {
  position: fixed;
  inset: 0;
  pointer-events: none;
  z-index: 50;
}
</style>
```

- [ ] **Step 2: Mount in SessionView after-phase** — script: `import ConfettiBurst from '@/components/ConfettiBurst.vue'`; template after-phase block, add as first element:

```html
<template v-else>
  <ConfettiBurst />
  <div class="card">
  ...
```

- [ ] **Step 3: Chart draw-in** — in `TrendChart.vue`, add class to polyline:

```html
<polyline :points="polyline" fill="none" stroke="var(--c-primary)" stroke-width="3" class="line" />
```

Style block, add:

```css
@media (prefers-reduced-motion: no-preference) {
  .line {
    stroke-dasharray: 1000;
    stroke-dashoffset: 1000;
    animation: draw 1.1s ease-out forwards;
  }

  @keyframes draw {
    to {
      stroke-dashoffset: 0;
    }
  }
}
```

(Polyline length is bounded by the 320×160 viewBox, always < 1000.)

- [ ] **Step 4: Verify**

Run: `npm run build && npm test` → green. Dev + mock: finish a session → confetti burst over pain-after screen, gone in ~2 s. Records → chart line draws in.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "feat(ui): confetti on session finish + chart draw-in"
```

---

### Task 7: Device verification

**Files:** none (verification only)

- [ ] **Step 1: Deploy to phone**

```bash
cd PT-Leg-Tracker
npm run build
npx cap sync android
cd android && .\gradlew assembleDebug
adb install -r app\build\outputs\apk\debug\app-debug.apk
```

Expected: Success. (Wireless adb: phone must be paired/connected — see server README + memory notes.)

- [ ] **Step 2: Check launcher icon + splash** — phone app drawer shows logo icon; cold-start shows logo splash.

- [ ] **Step 3: Walk all screens** — home (logo, stagger, Kanit, navy), knowledge, guide, records (chart draw), settings, session full loop with `server/scripts/dev.ps1` (ring pulse, rep pop, maroon stop, confetti).

- [ ] **Step 4: Reduced-motion spot check** — Android Settings → Accessibility → Remove animations ON → app shows static UI, no confetti. Turn back OFF.

- [ ] **Step 5: Final commit if any fixups**

```bash
git add -A
git commit -m "fix(ui): device verification fixups"
```
