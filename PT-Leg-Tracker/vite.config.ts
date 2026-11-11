/// <reference types="vitest/config" />
import { execFileSync } from 'node:child_process'
import { fileURLToPath, URL } from 'node:url'

import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'
import vueDevTools from 'vite-plugin-vue-devtools'

function git(...args: string[]): string {
  try {
    return execFileSync('git', args, { stdio: ['ignore', 'pipe', 'ignore'] }).toString().trim()
  } catch {
    return 'unknown'
  }
}

// https://vite.dev/config/
export default defineConfig({
  define: {
    __BUILD_ID__: JSON.stringify({
      version: process.env.npm_package_version ?? 'unknown',
      commit: git('rev-parse', '--short=7', 'HEAD'),
      branch: git('rev-parse', '--abbrev-ref', 'HEAD'),
      built: new Date().toISOString(),
    }),
  },
  plugins: [
    vue(),
    vueDevTools(),
  ],
  resolve: {
    alias: {
      '@': fileURLToPath(new URL('./src', import.meta.url)),
    },
  },
  test: {
    environment: 'node',
    include: ['src/**/*.test.ts'],
  },
})
