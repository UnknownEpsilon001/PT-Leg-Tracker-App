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
    color: colors[Math.floor(Math.random() * colors.length)]!,
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
