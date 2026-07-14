"""Session state machine: the server-side source of truth (spec: Approach A)."""

import time
from datetime import datetime, timezone

from . import db
from .state import DeviceState

AUTO_CLOSE_AFTER_SEC = 60


class ConflictError(Exception):
    """Request conflicts with current session state (HTTP 409)."""


class SessionService:
    def __init__(self, now=time.time):
        self._now = now
        self.device = DeviceState(now=now)

    def _iso_now(self) -> str:
        return datetime.fromtimestamp(self._now(), tz=timezone.utc).isoformat()

    # --- ESP32-facing ---

    def fetch_command(self) -> str | None:
        return self.device.fetch_command()

    def handle_event(self, event_type: str, elapsed_sec: int, reps: int) -> str | None:
        if event_type == "started":
            stale = db.get_open_session()
            if stale is not None:
                # power-glitch recovery: close stale session with last-known numbers
                db.close_session(
                    stale["id"],
                    self._iso_now(),
                    self.device.live_elapsed_sec,
                    self.device.live_reps,
                )
            origin = "app" if self.device.consume_start_origin() else "device"
            session_id = db.create_session(self._iso_now(), origin)
            self.device.live_state = "running"
            self.device.live_elapsed_sec = elapsed_sec
            self.device.live_reps = reps
            return session_id

        open_session = db.get_open_session()
        self.device.live_state = "idle"
        if open_session is None:
            return None
        db.close_session(open_session["id"], self._iso_now(), elapsed_sec, reps)
        return open_session["id"]

    def handle_heartbeat(self, state: str, elapsed_sec: int, reps: int) -> None:
        prev_elapsed = self.device.live_elapsed_sec
        prev_reps = self.device.live_reps
        self.device.heartbeat(state, elapsed_sec, reps)
        if state == "idle":
            # heal: device idle but a session is still open (lost 'stopped' event)
            open_session = db.get_open_session()
            if open_session is not None:
                db.close_session(
                    open_session["id"], self._iso_now(), prev_elapsed, prev_reps
                )

    # --- app-facing ---

    def queue_start(self) -> None:
        self._auto_close_if_silent()
        if db.get_open_session() is not None:
            raise ConflictError("session already running")
        if self.device.pending_command() == "start":
            raise ConflictError("start already queued")
        if self.device.start_in_flight():
            raise ConflictError("start in progress")
        self.device.queue_command("start")

    def queue_stop(self) -> None:
        self._auto_close_if_silent()
        if db.get_open_session() is None:
            raise ConflictError("no session running")
        self.device.queue_command("stop")

    def current(self) -> dict:
        self._auto_close_if_silent()
        online = self.device.device_online()
        open_session = db.get_open_session()
        if open_session is not None:
            return {
                "sessionId": open_session["id"],
                "state": "running",
                "elapsedSec": self.device.live_elapsed_sec,
                "reps": self.device.live_reps,
                "deviceOnline": online,
            }
        state = (
            "starting"
            if self.device.pending_command() == "start" or self.device.start_in_flight()
            else "idle"
        )
        return {
            "sessionId": None,
            "state": state,
            "elapsedSec": 0,
            "reps": 0,
            "deviceOnline": online,
        }

    def claim(self, session_id: str, patient_code: str) -> bool:
        return db.claim_session(session_id, patient_code)

    def list_for_patient(self, patient_code: str) -> list[dict]:
        self._auto_close_if_silent()
        return [
            {
                "id": row["id"],
                "startedAt": row["started_at"],
                "endedAt": row["ended_at"],
                "durationSec": row["duration_sec"],
                "reps": row["reps"],
                "patientCode": row["patient_code"],
            }
            for row in db.list_sessions(patient_code)
        ]

    def _auto_close_if_silent(self) -> None:
        silent_for = self.device.seconds_since_heartbeat()
        if silent_for is None or silent_for <= AUTO_CLOSE_AFTER_SEC:
            return
        open_session = db.get_open_session()
        if open_session is not None:
            db.close_session(
                open_session["id"],
                self._iso_now(),
                self.device.live_elapsed_sec,
                self.device.live_reps,
            )
            self.device.live_state = "idle"
