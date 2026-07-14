# PT Leg Tracker Server

FastAPI + SQLite server for the Smart OA Knee project. Owns therapy-session
state and relays control between the Android app and the ESP32 device.
Contract spec: `../docs/superpowers/specs/2026-07-13-server-mediated-device-control-design.md`.

## Setup

```powershell
python -m venv .venv
.venv\Scripts\python -m pip install -r requirements.txt
```

## Run

```powershell
.venv\Scripts\python -m uvicorn app.main:app --host 0.0.0.0 --port 8000
```

`--host 0.0.0.0` exposes the server on the LAN so phone + device can reach it.
DB file: `ptlt.sqlite3` in the working directory (override with env var `PTLT_DB`).

## Test

```powershell
.venv\Scripts\python -m pytest
```

## Mock device (no hardware)

In a second terminal (Node >= 18):

```powershell
node scripts/mock-esp32.mjs http://localhost:8000
```

Press `b` to simulate the physical start/stop button, `q` to quit.

## API

- ESP32: `GET /api/device/command`, `POST /api/device/event`, `POST /api/device/heartbeat`
- App: `POST /api/sessions/start|stop`, `GET /api/sessions/current`,
  `POST /api/sessions/{id}/claim`, `GET /api/sessions?patientCode=X`
