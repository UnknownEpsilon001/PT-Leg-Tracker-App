// Mock ESP32 firmware: node scripts/mock-esp32.mjs [serverUrl]
// Implements the v3 firmware contract: poll /api/device/command every 1s,
// heartbeat every 1s running / 5s idle, report transitions via /api/device/event.
// Press "b" to simulate the physical button, "q" to quit.
const base = process.argv[2] || 'http://localhost:8000'

// Real device motion cycle: lift (up) 10 s, then lower + rest 10 s = 1 rep / 20 s.
// snapshot() floors repCounter, so the count ticks to 1 only after a full cycle.
const REP_SECONDS = 20

let running = false
let startedAt = 0
let repCounter = 0
let lastIdleBeat = 0

const post = (path, body) =>
  fetch(base + path, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  }).then((r) => r.json())

const snapshot = () => ({
  elapsedSec: running ? Math.floor((Date.now() - startedAt) / 1000) : 0,
  reps: Math.floor(repCounter),
})

async function start(origin) {
  running = true
  startedAt = Date.now()
  repCounter = 0
  const res = await post('/api/device/event', { type: 'started', ...snapshot() })
  console.log(`[mock-esp32] started (${origin}) session=${res.sessionId}`)
}

async function stop(origin) {
  const final = snapshot()
  running = false
  await post('/api/device/event', { type: 'stopped', ...final })
  console.log(`[mock-esp32] stopped (${origin}) elapsed=${final.elapsedSec}s reps=${final.reps}`)
}

setInterval(async () => {
  try {
    const res = await fetch(`${base}/api/device/command`)
    const { command } = await res.json()
    if (command === 'start' && !running) await start('app command')
    if (command === 'stop' && running) await stop('app command')
  } catch {
    // server unreachable; keep polling
  }
}, 1000)

setInterval(async () => {
  if (running) repCounter += 1 / REP_SECONDS // 1 rep per full 20 s up+down cycle
  if (!running && Date.now() - lastIdleBeat < 5000) return
  lastIdleBeat = Date.now()
  try {
    await post('/api/device/heartbeat', { state: running ? 'running' : 'idle', ...snapshot() })
  } catch {
    // server unreachable; keep beating
  }
}, 1000)

process.stdin.setRawMode?.(true)
process.stdin.resume()
process.stdin.on('data', async (key) => {
  const k = key.toString()
  if (k === 'b') (running ? stop : start)('button').catch(() => console.log('[mock-esp32] server unreachable, button ignored'))
  if (k === 'q' || k === '\u0003') process.exit(0) // q or Ctrl-C
})
console.log(`[mock-esp32] polling ${base} — "b" = physical button, "q" = quit`)
