# Smart OA Knee — Android app

Vue 3 + Vite + Capacitor shell for the Smart OA Knee app. The folder keeps its `PT-Leg-Tracker/` name; run every npm and npx command from here.

## Recommended IDE Setup

[VS Code](https://code.visualstudio.com/) + [Vue (Official)](https://marketplace.visualstudio.com/items?itemName=Vue.volar) (and disable Vetur).

## Recommended Browser Setup

- Chromium-based browsers (Chrome, Edge, Brave, etc.):
  - [Vue.js devtools](https://chromewebstore.google.com/detail/vuejs-devtools/nhdogjmejiglipccpnnnanhbledajbpd)
  - [Turn on Custom Object Formatter in Chrome DevTools](http://bit.ly/object-formatters)
- Firefox:
  - [Vue.js devtools](https://addons.mozilla.org/en-US/firefox/addon/vue-js-devtools/)
  - [Turn on Custom Object Formatter in Firefox DevTools](https://fxdx.dev/firefox-devtools-custom-object-formatters/)

## Type Support for `.vue` Imports in TS

TypeScript cannot handle type information for `.vue` imports by default, so we replace the `tsc` CLI with `vue-tsc` for type checking. In editors, we need [Volar](https://marketplace.visualstudio.com/items?itemName=Vue.volar) to make the TypeScript language service aware of `.vue` types.

## Customize configuration

See [Vite Configuration Reference](https://vite.dev/config/).

## Project Setup

```sh
npm install
```

## Build and install the Android app

Run the numbered PowerShell menu from `PT-Leg-Tracker/`:

```powershell
.\tools\app-build.ps1
```

The menu can build a debug APK, list ADB devices, install/launch through USB or
Wireless debugging, connect or pair Wireless debugging, or run full auto. The
published APK is written to `output/PT-Leg-Tracker-debug.apk`. Full auto skips
the build when source and the saved APK hash are unchanged. With no connected
device it still produces the APK; with multiple ready devices it asks for a
number instead of guessing.

Build progress is shown in three stages: web build, Capacitor sync, and Gradle
APK build. The published APK and state file are replaced atomically only after a
successful build, so pressing **Ctrl+C** keeps the previous good APK. If a phone
disconnects during install or launch, the menu reports it and returns for a
retry; command-line mode exits non-zero while keeping the APK intact.

Non-interactive examples:

```powershell
.\tools\app-build.ps1 -Action build
.\tools\app-build.ps1 -Action usb
.\tools\app-build.ps1 -Action wireless
.\tools\app-build.ps1 -Action auto -Connection any
.\tools\app-build.ps1 -Action pair -Address 192.168.1.50:37123 -PairingCode 123456
.\tools\app-build.ps1 -Action connect -Address 192.168.1.50:5555
```

Android shows separate pairing and connection ports. Pair once with option 7,
then use option 6 with the current connection address. USB debugging needs no
address: connect the cable, accept the phone's authorization prompt, and choose
option 3.

### Compile and Hot-Reload for Development

```sh
npm run dev
```

### Type-Check, Compile and Minify for Production

```sh
npm run build
```

### Lint with [ESLint](https://eslint.org/)

```sh
npm run lint
```
