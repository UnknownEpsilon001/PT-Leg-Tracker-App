# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

PT Leg Tracker — companion Android app for a leg physical therapy device (สิ่งประดิษฐ์ของคนรุ่นใหม่ สอศ. vocational innovation contest). An ESP32 on the device uploads session data to a server; this app pulls, displays, and analyzes it (session time, progress tracking). The ESP32 firmware and server live in separate repos.

The app itself lives in the `PT-Leg-Tracker/` subdirectory — **run all npm/npx commands from there, not the repo root.**

The FastAPI + SQLite server (session state, app↔ESP32 relay) lives in `server/` — Python subproject with its own venv (`server\.venv`); run pytest/uvicorn from `server/`. Contract spec: `docs/superpowers/specs/2026-07-13-server-mediated-device-control-design.md`.

## Stack

Vue 3 + TypeScript + Vite, Pinia (state), Vue Router, wrapped in Capacitor 8 for Android. No test framework configured yet.

## Commands

All from `PT-Leg-Tracker/`:

```bash
npm run dev          # Vite dev server (web)
npm run build        # type-check (vue-tsc) + vite build → dist/
npm run lint         # oxlint then eslint, both with --fix
npm run format       # prettier on src/
```

Android (requires Android Studio installed):

```bash
npm run build            # always build web assets first
npx cap sync android     # copy dist/ → android/app/src/main/assets/public
npx cap run android      # install + launch on connected device/emulator
npx cap open android     # open in Android Studio instead
```

Native build check without Studio: `cd android && .\gradlew assembleDebug`

## Architecture notes

- **Web → native flow**: Capacitor serves the built `dist/` from the native shell. Any web change needs `npm run build` + `npx cap sync android` before it appears in the Android app. Editing `android/` alone never updates the UI.
- `capacitor.config.ts` — appId `com.unknownaimo.ptlegtracker`, webDir `dist`. appId is locked once published; don't change it.
- `android/` is committed source (Capacitor convention), not build output. `android/app/build/` and `assets/public/` are gitignored.
- `android/gradle.properties` sets `org.gradle.java.home` to Android Studio's bundled JDK because the system Java (25) is too new for Gradle 8.14. If Gradle fails with "Unsupported class file major version", this path is stale.
