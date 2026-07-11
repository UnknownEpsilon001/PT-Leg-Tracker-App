# Smart OA Knee — PT Leg Tracker

แอปพลิเคชันส่งเสริมการเรียนรู้ด้านการปฏิบัติตัวและการออกกำลังกาย สำหรับผู้ป่วยภาวะข้อเข่าเสื่อม

Learning and self-care companion app for patients with **knee osteoarthritis (OA Knee)** — knowledge, guided exercise, and progress tracking, paired with a leg physical therapy device.

## โครงการ / About the Project

ผลงานภายใต้ทุนส่งเสริมวิทยาศาสตร์ วิจัยและนวัตกรรม (ววน.) โครงการขับเคลื่อนนวัตกรรมสิ่งประดิษฐ์อาชีวศึกษา (สอศ.) ปีงบประมาณ พ.ศ. 2569 รอบที่ 2

- **สถานศึกษา:** วิทยาลัยการอาชีพฝาง อ.ฝาง จ.เชียงใหม่
- **กลุ่มเป้าหมาย:** ผู้ป่วยข้อเข่าเสื่อม อายุ 40 ปีขึ้นไป ในพื้นที่ อ.ฝาง แม่อาย ไชยปราการ จ.เชียงใหม่ ผ่านโรงพยาบาลส่งเสริมสุขภาพตำบล / โรงพยาบาลชุมชนที่เข้าร่วมโครงการ
- **ระยะเวลา:** พฤษภาคม – พฤศจิกายน 2569
- **ทีมวิจัย:** ครูด้านเทคโนโลยีและนวัตกรรม ร่วมกับนักกายภาพบำบัดปฏิบัติการ และนักศึกษา ปวช. เทคโนโลยีอิเล็กทรอนิกส์

Vocational innovation research project (Thai OVEC / วท.ววน. funding, FY 2026). Developed by teachers and electronics students at Fang Industrial and Community Education College together with a licensed physical therapist.

## ภาพรวมระบบ / How It Works

```
┌──────────────┐        ┌──────────┐        ┌─────────────────┐
│ เครื่องกายภาพ │  WiFi  │  Server  │  HTTP  │  Android App    │
│ (ESP32)      ├───────►│          │◄───────┤  (Vue+Capacitor)│
└──────────────┘ upload └──────────┘  pull  └─────────────────┘
```

1. **ESP32** on the leg therapy device records session data and uploads it to the server
2. **Server** stores session history
3. **App** pulls the data, then displays and analyzes it (session time, pain level, progress)

## ฟีเจอร์ / Features

1. **ความรู้โรคข้อเข่าเสื่อม** — สาเหตุ ปัจจัยเสี่ยง อาการ แนวทางป้องกันและดูแลตนเอง
2. **คู่มือการปฏิบัติตัว** — การใช้ข้อเข่าในชีวิตประจำวัน การควบคุมน้ำหนัก การเลือกกิจกรรม ข้อควรระวัง
3. **โปรแกรมการออกกำลังกาย** — ท่าบริหารเสริมความแข็งแรงกล้ามเนื้อรอบเข่า ท่ายืดเหยียด คำแนะนำความถี่/ระยะเวลา พร้อมวิดีโอสาธิต
4. **ระบบติดตามผลการดูแลตนเอง** — บันทึกการออกกำลังกาย บันทึกระดับความปวด ติดตามความก้าวหน้า (ดึงข้อมูลจากเครื่องกายภาพผ่านเซิร์ฟเวอร์)
5. **ระบบประเมินความรู้** — แบบทดสอบก่อน/หลังใช้งาน แบบประเมินความพึงพอใจ
6. **ออกแบบเพื่อผู้สูงอายุ** — ตัวอักษรใหญ่ เมนูใช้งานง่าย ภาพและสัญลักษณ์เข้าใจง่าย

Knowledge base on knee OA · self-care manual · exercise program with demo videos · self-care tracking (exercise log, pain level, progress) · pre/post knowledge tests and satisfaction survey · elderly-friendly UI (large text, simple menus).

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
docs/             # project documents (funding proposal, etc.)
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
