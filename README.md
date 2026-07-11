# PT Leg Tracker — เครื่องช่วยกายภาพบำบัดขา

แอปพลิเคชันติดตามผลการกายภาพบำบัดขา ส่วนหนึ่งของโครงการ **สิ่งประดิษฐ์ของคนรุ่นใหม่ (สอศ.)**

Companion app for a leg physical therapy assistance device, developed for the Thai Vocational Innovation Contest (Office of Vocational Education Commission).

## ภาพรวมระบบ / How It Works

```
┌──────────────┐        ┌──────────┐        ┌─────────────────┐
│ เครื่องกายภาพ │  WiFi  │  Server  │  HTTP  │  Android App    │
│ (ESP32)      ├───────►│          │◄───────┤  (Vue+Capacitor)│
└──────────────┘ upload └──────────┘  pull  └─────────────────┘
```

1. **ESP32** on the therapy device records session data and uploads it to the server
2. **Server** stores session history
3. **App** pulls the data, then displays and analyzes it

## ฟีเจอร์ / Features

- ดึงข้อมูลการกายภาพจากเซิร์ฟเวอร์ — pull therapy session data from server
- แสดงผลข้อมูลการใช้งาน — display session history
- วิเคราะห์และติดตามผล เช่น ระยะเวลาการทำกายภาพ — analysis and tracking (session time, progress, etc.)

## Tech Stack

| Layer  | Tech |
|--------|------|
| Device | ESP32 (separate firmware repo) |
| App    | Vue 3, TypeScript, Pinia, Vue Router |
| Build  | Vite |
| Mobile | Capacitor 8 (Android) |

## Project Structure

```
PT-Leg-Tracker/
├── src/          # Vue app source
├── android/      # Capacitor Android native project
├── dist/         # web build output (generated)
└── capacitor.config.ts
```

## Development

Requires Node 22.18+ / 24.12+ and Android Studio (for the Android build).

```bash
cd PT-Leg-Tracker
npm install
npm run dev          # web dev server
```

## Android Build

```bash
npm run build            # build web assets to dist/
npx cap sync android     # copy dist/ into android project
npx cap run android      # install + launch on device/emulator
# or: npx cap open android  → run from Android Studio
```

> Note: `android/gradle.properties` points Gradle at Android Studio's bundled
> JDK (`org.gradle.java.home`). Adjust the path if your Studio is installed
> elsewhere.
