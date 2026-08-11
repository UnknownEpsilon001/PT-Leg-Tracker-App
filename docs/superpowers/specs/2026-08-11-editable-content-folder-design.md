# Unified Editable Content Folder — Design

**Date:** 2026-08-11

## Goal

Put all non-technical content that staff may replace—educational text, guide
instructions, quiz questions, branding images, article/guide pictures, local
videos, and YouTube references—under one plainly named folder:

`PT-Leg-Tracker/public/editable-content/`

A non-developer should be able to update this material by following one README
without editing Vue or TypeScript files. The content remains bundled with the
Android application and works offline, except for YouTube playback.

Functional controls, validation messages, server settings, and other technical
UI copy remain source code. Moving every interface label would be a separate
localization system and is outside this change.

## Folder Layout

```text
PT-Leg-Tracker/public/editable-content/
├── README.md
├── app.json
├── knowledge.json
├── device-guide.json
├── quizzes.json
├── images/
│   ├── branding/
│   │   └── logo.png
│   ├── knowledge/
│   │   ├── what-is-oa.svg
│   │   ├── causes.svg
│   │   ├── symptoms.svg
│   │   ├── daily-care.svg
│   │   └── pain-relief.svg
│   └── guide/
│       ├── step-1.svg
│       ├── step-2.svg
│       └── step-3.svg
└── videos/
    └── README.md
```

The existing content and images move into this structure. Staff may replace
the example SVG files with `.png`, `.jpg`, `.jpeg`, `.webp`, or `.svg` files and
then update the matching JSON path. Local guide videos use `.mp4` or `.webm`.
Filenames should use lowercase letters, numbers, and hyphens without spaces.

## Editable Files

### `app.json`

Contains the small branding surface currently embedded in the home screen:

```json
{
  "homeSubtitle": "เครื่องช่วยบริหารกล้ามเนื้อต้นขา Smart OA Knee",
  "logo": "/editable-content/images/branding/logo.png",
  "logoAlt": "ตราวิทยาลัยการอาชีพฝาง"
}
```

The greeting continues to include the registered patient's name. Navigation
labels remain functional UI copy in the Vue component.

### `knowledge.json`

Keeps the current section/article model. Each article contains:

- stable `id` used by routing and read-progress storage;
- editable `title` and `body`;
- `image`, an absolute local path beginning with `/editable-content/`;
- `imageAlt`, a useful Thai description for accessibility.

Article IDs must remain unique. Existing IDs are preserved during migration so
saved read progress and links continue to work.

### `device-guide.json`

Keeps the ordered `steps` array with stable `id`, `title`, `caption`, and
`image`. It replaces `videoIds` with a mixed `videos` array:

```json
{
  "videos": [
    { "type": "youtube", "id": "YOUTUBE_VIDEO_ID", "title": "วิดีโอสาธิต" },
    {
      "type": "file",
      "src": "/editable-content/videos/device-demo.mp4",
      "title": "วิดีโอสาธิตแบบออฟไลน์"
    }
  ]
}
```

YouTube entries accept only the video ID, not a full watch URL. YouTube entries
render only while online. File entries use the native HTML video player and
remain available offline because the file is packaged with the app. An empty
array continues to show the existing “ยังไม่มีวิดีโอ” message.

### `quizzes.json`

Keeps the current quiz format. The quiz IDs `pre`, `post`, and `satisfaction`
are fixed because routes and exported research data depend on them. Staff may
change titles, questions, choices, ordering, and `correctIndex` values.
Knowledge-test questions require a valid `correctIndex`; satisfaction questions
omit it.

## Runtime Loading

Vite copies the entire public folder into the web build, and Capacitor copies it
into the Android app during `npx cap sync android`. A focused module at
`src/lib/content.ts` fetches and caches the JSON files from
`/editable-content/`. It owns the TypeScript interfaces and lightweight runtime
validation so views do not each implement parsing logic.

The home, knowledge list, article, device guide, and quiz views load their
content asynchronously through this module. Each view has three explicit
states: loading, ready, and error. Navigation and stored user data do not depend
on the content loader.

## Error Handling

Missing files, malformed JSON, unknown video types, invalid required fields,
duplicate article IDs, and invalid quiz answer indexes are content errors. The
loader rejects them with one internal typed error; affected views show a concise
Thai message telling staff to inspect `public/editable-content/README.md`.

The app must not silently substitute stale hidden copies because that would make
staff believe their edits were applied. One invalid content file affects only
the screen that uses it; registration, records, settings, and therapy-session
control continue to work.

Broken image or local-video paths use the browser's normal failed-media state
and are also covered by the README troubleshooting checklist. YouTube network
failure keeps the existing offline guidance.

## Editor Guide

The root README provides:

- a “replace an image” walkthrough;
- a “paste a YouTube URL and extract its ID” example;
- a local MP4 example;
- JSON comma/quote rules and UTF-8 guidance;
- fixed IDs that must not be renamed;
- recommended image dimensions and file-size guidance;
- the exact build, Capacitor sync, APK build, and install commands;
- a checklist for opening every edited screen before distribution.

`videos/README.md` exists so the otherwise-empty directory is committed and
briefly repeats supported video formats and size considerations.

## Migration and Compatibility

- Move `src/content/knowledge.json`, `src/content/deviceGuide.json`, and
  `src/content/quizzes.json` into the unified public folder.
- Move `src/assets/logo.png`, `public/knowledge/*`, and `public/guide/*` into
  their matching image subfolders.
- Update all JSON media paths and remove the old files after call sites use the
  loader.
- Preserve article, guide-step, and quiz IDs.
- Existing profile, quiz-result, session, pain-log, and settings storage is not
  migrated or reset.
- Server and firmware projects are untouched.

## Verification

Automated checks cover successful loading, caching, malformed JSON, missing
files, duplicate IDs, quiz answer bounds, and mixed YouTube/local-video parsing.
Existing quiz-scoring tests remain unchanged. The full Vitest suite, type check,
Vite production build, Capacitor sync, and Android debug build must pass.

The final MuMu smoke test covers registration/home branding, the knowledge list
and article images, all guide steps, one local video, one YouTube entry while
online, all three quiz routes, and a relaunch to prove bundled offline content
loads consistently.
