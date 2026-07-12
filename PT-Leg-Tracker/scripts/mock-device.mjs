// Mock ESP32 for on-device testing: node scripts/mock-device.mjs [port]
// Implements the v2 device contract: POST /start, GET /status, POST /stop.
import { createServer } from 'node:http'

const port = Number(process.argv[2]) || 8080
let session = null // { id, startedAt }

const server = createServer((req, res) => {
  res.setHeader('Access-Control-Allow-Origin', '*')
  res.setHeader('Access-Control-Allow-Methods', 'GET, POST, OPTIONS')
  res.setHeader('Content-Type', 'application/json')
  if (req.method === 'OPTIONS') return res.writeHead(204).end()

  if (req.method === 'POST' && req.url === '/start') {
    if (session) return res.writeHead(409).end('{}')
    session = { id: `mock-${Date.now()}`, startedAt: Date.now() }
    return res.end(JSON.stringify({ sessionId: session.id }))
  }
  if (req.method === 'GET' && req.url === '/status') {
    if (!session) return res.end(JSON.stringify({ state: 'idle', elapsedSec: 0, sessionId: null }))
    return res.end(
      JSON.stringify({
        state: 'running',
        elapsedSec: Math.floor((Date.now() - session.startedAt) / 1000),
        sessionId: session.id,
      }),
    )
  }
  if (req.method === 'POST' && req.url === '/stop') {
    if (!session) return res.writeHead(409).end('{}')
    session = null
    return res.end(JSON.stringify({ ok: true }))
  }
  res.writeHead(404).end('{}')
})

server.listen(port, '0.0.0.0', () => console.log(`mock device on http://0.0.0.0:${port}`))
