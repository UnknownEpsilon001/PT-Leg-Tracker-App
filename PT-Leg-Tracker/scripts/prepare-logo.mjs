// Derives every app logo asset from the master logo: the in-app logo, the Android
// launcher icons and splash screens, and the capacitor-assets sources.
// Usage: node scripts/prepare-logo.mjs
import sharp from 'sharp'
import { mkdirSync, readdirSync, existsSync } from 'node:fs'
import { join } from 'node:path'

const SRC = '../assets/logo-med-flex.png'
const RES = 'android/app/src/main/res'
const WHITE = { r: 255, g: 255, b: 255, alpha: 1 }
const NAVY = { r: 26, g: 43, b: 71, alpha: 1 } // the app's dark background
const CLEAR = { r: 0, g: 0, b: 0, alpha: 0 }

// `share` is the logo's size as a fraction of the canvas's shorter side.
async function logoOn(width, height, background, share) {
  const box = Math.round(Math.min(width, height) * share)
  const logo = await sharp(SRC).resize(box, box, { fit: 'inside', kernel: 'lanczos3' }).toBuffer()
  return sharp({ create: { width, height, channels: 4, background } })
    .composite([{ input: logo, gravity: 'center' }])
    .png({ compressionLevel: 9 })
}

mkdirSync('assets', { recursive: true })
await sharp(SRC)
  .resize(800, 800, { fit: 'inside', kernel: 'lanczos3' })
  .png({ compressionLevel: 9 })
  .toFile('public/editable-content/images/branding/logo.png')
await (await logoOn(1024, 1024, WHITE, 0.86)).toFile('assets/icon-only.png')
await (await logoOn(2732, 2732, WHITE, 0.4)).toFile('assets/splash.png')
await (await logoOn(2732, 2732, NAVY, 0.4)).toFile('assets/splash-dark.png')

// Launcher icons. The adaptive foreground is drawn on a 108 dp canvas of which
// only the middle 66 dp is sure to survive the launcher's mask.
const DENSITIES = { ldpi: 0.75, mdpi: 1, hdpi: 1.5, xhdpi: 2, xxhdpi: 3, xxxhdpi: 4 }
for (const [name, scale] of Object.entries(DENSITIES)) {
  const dir = join(RES, `mipmap-${name}`)
  const legacy = Math.round(48 * scale)
  await (await logoOn(legacy, legacy, WHITE, 0.92)).toFile(join(dir, 'ic_launcher.png'))
  await (await logoOn(legacy, legacy, WHITE, 0.78)).toFile(join(dir, 'ic_launcher_round.png'))
  if (name === 'ldpi') continue
  const side = Math.round(108 * scale)
  await (await logoOn(side, side, CLEAR, 66 / 108)).toFile(join(dir, 'ic_launcher_foreground.png'))
}

// Splash screens: each existing file is redrawn at its own size.
for (const dir of readdirSync(RES).filter((d) => d.startsWith('drawable'))) {
  const file = join(RES, dir, 'splash.png')
  if (!existsSync(file)) continue
  const { width, height } = await sharp(file).metadata()
  const png = await (await logoOn(width, height, dir.includes('night') ? NAVY : WHITE, 0.55)).toBuffer()
  await sharp(png).toFile(file)
}
console.log('done: branding logo, launcher icons, splash screens')
