# Editable App Content

This folder contains the text, pictures, guide instructions, and quiz content
that can be updated without changing the app code. Everything here is copied
into the Android app, so the normal content works without internet after the
app is installed. Only YouTube videos need internet access.

## Which file changes which screen?

| File                | Screen and editable content                                  |
| ------------------- | ------------------------------------------------------------ |
| `app.json`          | Home screen: subtitle and logo                               |
| `knowledge.json`    | Knowledge list and each knowledge article                    |
| `device-guide.json` | Device guide steps, guide pictures, and demonstration videos |
| `quizzes.json`      | Pre-test, post-test, and satisfaction questionnaire          |

Pictures belong in `images/`; offline videos belong in `videos/`. Do not move
these folders or create paths outside `/editable-content/`.

## Safe editing rules

1. Make a backup copy of the JSON file before editing it.
2. Edit with a text editor that saves as **UTF-8** (for example VS Code or
   Notepad's UTF-8 format). Do not use Word or a spreadsheet application.
3. JSON uses double quotes around text and commas between items. Do not leave
   a comma after the final item in a list or object.
4. Keep file paths lowercase, with letters, numbers, and hyphens only. Do not
   use spaces, `..`, backslashes, `?`, or `#` in a path.

For example, this safely changes a knowledge article title and picture:

```json
{
  "id": "what-is-oa",
  "title": "หัวข้อใหม่",
  "body": "เนื้อหาที่แก้ไขแล้ว",
  "image": "/editable-content/images/knowledge/what-is-oa.svg",
  "imageAlt": "คำอธิบายภาพสั้น ๆ"
}
```

Use only these image paths and extensions for the app logo, knowledge images,
and guide-step images:

```text
/editable-content/images/<folder>/<lowercase-name>.png
/editable-content/images/<folder>/<lowercase-name>.jpg
/editable-content/images/<folder>/<lowercase-name>.jpeg
/editable-content/images/<folder>/<lowercase-name>.webp
/editable-content/images/<folder>/<lowercase-name>.svg
```

## Important IDs

Article IDs and guide-step IDs are stable identifiers used by saved progress
and links. Keep every existing `id` unchanged and unique. You may edit titles,
body text, captions, pictures, and ordering.

The quiz IDs are fixed: `pre`, `post`, and `satisfaction`. Do not rename,
remove, duplicate, or add another quiz ID. These IDs are used by app routes and
exported research data.

## Device-guide steps and videos

Each item in `steps` needs a stable `id`, a `title`, a `caption`, and an image.
For example:

```json
{
  "id": "step-1",
  "title": "ขั้นตอนที่ 1",
  "caption": "คำอธิบายขั้นตอน",
  "image": "/editable-content/images/guide/step-1.svg"
}
```

`videos` can mix YouTube and offline file videos. An empty list is valid and
shows the app's existing “no videos” message:

```json
{
  "videos": [
    { "type": "youtube", "id": "dQw4w9WgXcQ", "title": "วิดีโอออนไลน์" },
    {
      "type": "file",
      "src": "/editable-content/videos/device-demo.mp4",
      "title": "วิดีโอออฟไลน์"
    }
  ]
}
```

For YouTube, copy only the 11-character video ID from a link such as
`https://www.youtube.com/watch?v=dQw4w9WgXcQ`. Do not paste the full watch or
share URL. YouTube entries need internet.

For an offline video, copy an `.mp4` or `.webm` file into `videos/` and use a
path beginning `/editable-content/videos/`. Keep videos short and compressed,
because they increase the Android app size.

## Quiz questions

`correctIndex` is the position of the correct choice, starting at zero. With
the choices below, `0` means “Never”, `1` means “Sometimes”, and `2` means
“Always”:

```json
{
  "text": "ตัวอย่างคำถาม",
  "choices": ["Never", "Sometimes", "Always"],
  "correctIndex": 2
}
```

Every pre-test and post-test question needs a valid `correctIndex`. Satisfaction
questions measure an opinion, so they **must omit** `correctIndex` entirely:

```json
{
  "text": "ท่านพึงพอใจต่อการใช้งานหรือไม่",
  "choices": ["น้อยที่สุด", "น้อย", "ปานกลาง", "มาก", "มากที่สุด"]
}
```

## Validate and troubleshoot

Before building, check that every JSON file opens without an error in your text
editor or a JSON validator. If the app shows “โหลดเนื้อหาไม่สำเร็จ”, restore
the backup and check for a missing quote, a missing comma, a duplicated ID, an
invalid quiz ID, or an invalid image/video path. If a picture or local video is
missing, confirm its filename and extension match the JSON path exactly.

## Build and install checklist

From `PT-Leg-Tracker/`, run these commands after every content change:

```powershell
npm test
npm run build
npx cap sync android
cd android
.\gradlew assembleDebug
adb install -r app\build\outputs\apk\debug\app-debug.apk
```

Then open the home screen, each knowledge article, all guide steps and videos,
and the three quizzes. Confirm that the updated content appears before sharing
the APK.
