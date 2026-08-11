# Editable App Content

Everything in this folder is copied into the Android app. Edit JSON as UTF-8.
Keep JSON commas and double quotes valid. Do not rename the fixed quiz IDs
`pre`, `post`, or `satisfaction`, and do not rename existing article IDs after
patients have started using the app.

## Replace a picture

1. Copy a PNG, JPG, JPEG, WebP, or SVG into the matching `images/` folder.
2. Use a lowercase filename without spaces, for example `wear-device.jpg`.
3. Update the JSON path, for example:
   `"image": "/editable-content/images/guide/wear-device.jpg"`.
4. Update `imageAlt` with a short Thai description when the field exists.

Recommended: approximately 1200 × 900 pixels and below 1 MB per image.

## Add a YouTube video

For `https://www.youtube.com/watch?v=dQw4w9WgXcQ`, store only the ID:

```json
{ "type": "youtube", "id": "dQw4w9WgXcQ", "title": "วิดีโอสาธิต" }
```

YouTube requires internet.

## Add an offline video

Copy an MP4 or WebM into `videos/`, then add:

```json
{
  "type": "file",
  "src": "/editable-content/videos/device-demo.mp4",
  "title": "วิดีโอสาธิตแบบออฟไลน์"
}
```

Keep local videos short and compressed because they increase APK size.

## Build and install

From `PT-Leg-Tracker/`:

```powershell
npm test
npm run build
npx cap sync android
cd android
.\gradlew assembleDebug
adb install -r app\build\outputs\apk\debug\app-debug.apk
```

Before distribution, open the home page, every knowledge article, every guide
step and video, and all three quizzes.
