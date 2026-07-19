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
