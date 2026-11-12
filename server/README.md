# Med-Flex Auto Smart — Server

FastAPI + SQLite server for the Med-Flex Auto Smart project. Owns therapy-session
state and relays control between the Android app and the ESP32 device.
Contract spec: `../docs/superpowers/specs/2026-07-13-server-mediated-device-control-design.md`.

## Setup

```powershell
python -m venv .venv
.venv\Scripts\python -m pip install -r requirements.txt
```

## Run on the server PC

Double-click `start-server.cmd`. The first run sets up Python and the packages;
later runs only start the server. It prints two kinds of address:

- **LAN address** (`192.168.x.x` or `10.x.x.x`): put this in the device's server
  setting. The device must be on the same Wi-Fi as the server PC, because the
  ESP32 cannot join Tailscale.
- **Tailscale address** (`100.x.x.x`): put this in the phone app. It works from
  any network while the phone's Tailscale app is connected.

Do not port-forward 8000 on the router. The server has no login, so anyone who
reaches it can start the motor.

## Update the server PC

Close the running server, then double-click `update-server.cmd`. It:

1. Refuses to overwrite tracked local changes.
2. Fast-forwards `main` from GitHub.
3. Backs up `ptlt.sqlite3` under `server/backups/`.
4. Creates the Python environment when missing and installs the locked requirements.
5. Runs the complete server test suite.
6. Starts the server after every check passes.

Command-line options:

```powershell
.\update-server.cmd -Port 9000
.\update-server.cmd -SkipPull -SkipStart  # setup/test only
```

## Run

One-shot dev launcher (server in a new window + interactive mock ESP32 in this
one; quitting the mock or Ctrl+C stops both). Prints the LAN URL to put in the
phone's server setting:

```powershell
.\scripts\dev.ps1           # server + mock
.\scripts\dev.ps1 -NoMock   # server only (real device instead)
.\scripts\dev.ps1 -Port 9000
```

Manual equivalent:

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

## Interactive control test (real device)

Start the server, then double-click `device-control.cmd`. The menu can queue
start/stop commands, inspect live state, change the target/timings, and watch the
device-reported time and repetitions.

Defaults: server `http://localhost:8000`, device code `default`. To use another
server or device from PowerShell:

```powershell
.\device-control.cmd -ServerUrl http://192.168.1.20:8000 -DeviceId KNEE-01
```

## API

- ESP32: `GET /api/device/command`, `POST /api/device/event`, `POST /api/device/heartbeat`, `POST /api/device/session` (a finished session from the device's SD card, sent after the fact; merged by `uid`, safe to repeat)
- App: `POST /api/sessions/start|stop`, `GET /api/sessions/current`,
  `POST /api/sessions/{id}/claim`, `GET /api/sessions?patientCode=X`
