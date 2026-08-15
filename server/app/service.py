"""Session state machine: the server-side source of truth (spec: Approach A).

[TH] เครื่องสถานะของเซสชัน ฝั่งเซิร์ฟเวอร์ถือเป็นความจริงหลัก (ต้นทาง)
     รับเหตุการณ์/สัญญาณชีพจาก ESP32 และคำสั่ง start/stop จากแอป แล้วบันทึกลงฐานข้อมูล
     พร้อมกลไกเยียวยา เช่น ปิดเซสชันค้างเมื่ออุปกรณ์เงียบหรือพลาดเหตุการณ์ 'stopped'
"""

import time
from datetime import datetime, timezone

from . import db
from .db import DEFAULT_DEVICE_ID
from .state import DeviceState

AUTO_CLOSE_AFTER_SEC = 60


class ConflictError(Exception):
    """Request conflicts with current session state (HTTP 409)."""


class SessionService:
    def __init__(self, now=time.time):
        self._now = now
        self._devices: dict[str, DeviceState] = {}

    def _device(self, device_id: str) -> DeviceState:
        dev = self._devices.get(device_id)
        if dev is None:
            dev = DeviceState(now=self._now)
            self._devices[device_id] = dev
        return dev

    @property
    def device(self) -> DeviceState:
        """Back-compat accessor for the default device."""
        return self._device(DEFAULT_DEVICE_ID)

    def _iso_now(self) -> str:
        return datetime.fromtimestamp(self._now(), tz=timezone.utc).isoformat()

    # --- ESP32-facing ---

    def fetch_command(self, device_id: str = DEFAULT_DEVICE_ID) -> str | None:
        return self._device(device_id).fetch_command()

    def fetch_target(self, device_id: str = DEFAULT_DEVICE_ID) -> int | None:
        return self._device(device_id).fetch_desired_target()

    def set_target(self, reps: int, device_id: str = DEFAULT_DEVICE_ID) -> None:
        # Never a ConflictError: the target is editable while a session runs, and
        # the controller applies a lowered one at the end of the current rest.
        # [TH] ไม่เคยชนสถานะ เพราะแก้เป้าหมายได้ระหว่างที่รอบกำลังเดิน
        #      และคอนโทรลเลอร์จะใช้ค่าที่ปรับลดลงเมื่อสิ้นสุดการพักครั้งปัจจุบัน
        self._device(device_id).set_desired_target(reps)

    def set_timings(self, timings: dict, device_id: str = DEFAULT_DEVICE_ID) -> None:
        self._device(device_id).set_desired_timings(timings)

    def fetch_timings(self, device_id: str = DEFAULT_DEVICE_ID) -> dict | None:
        return self._device(device_id).fetch_desired_timings()

    def handle_event(
        self,
        event_type: str,
        elapsed_sec: int,
        reps: int,
        device_id: str = DEFAULT_DEVICE_ID,
    ) -> str | None:
        # [TH] จัดการเหตุการณ์จากอุปกรณ์: "started" เปิดเซสชันใหม่ อื่นๆ ปิดเซสชันที่เปิดอยู่
        dev = self._device(device_id)
        if event_type == "started":
            stale = db.get_open_session(device_id)
            if stale is not None:
                # power-glitch recovery: close stale session with last-known numbers
                # [TH] กู้จากไฟกระตุก: ปิดเซสชันค้างด้วยตัวเลขล่าสุดที่รู้
                db.close_session(
                    stale["id"], self._iso_now(), dev.live_elapsed_sec, dev.live_reps
                )
            origin = "app" if dev.consume_start_origin() else "device"
            # the start intent is satisfied; a still-queued "start" would otherwise
            # be fetched stale by the device's next poll
            dev.clear_pending_start()
            session_id = db.create_session(self._iso_now(), origin, device_id)
            # an event is proof of life: refresh liveness so a fresh session isn't
            # immediately auto-closed by silence that preceded this event
            # target is carried through, not defaulted -- this is a lifecycle
            # event, not a target report, so it must not blank live_target
            # [TH] ส่งต่อค่าเป้าหมายเดิม ไม่ใช้ค่าเริ่มต้น เพราะนี่คือเหตุการณ์วงจรชีวิต
            #      ไม่ใช่การรายงานเป้าหมาย จึงต้องไม่ทำให้ live_target ว่างไป
            dev.heartbeat("running", elapsed_sec, reps, target=dev.live_target)
            return session_id

        open_session = db.get_open_session(device_id)
        dev.heartbeat("idle", elapsed_sec, reps, target=dev.live_target)
        # a start cannot be in flight across a completed session
        dev.clear_start_in_flight()
        if open_session is None:
            return None
        db.close_session(open_session["id"], self._iso_now(), elapsed_sec, reps)
        return open_session["id"]

    def handle_heartbeat(
        self,
        state: str,
        elapsed_sec: int,
        reps: int,
        target: int = 0,
        device_id: str = DEFAULT_DEVICE_ID,
    ) -> None:
        dev = self._device(device_id)
        prev_elapsed = dev.live_elapsed_sec
        prev_reps = dev.live_reps
        dev.heartbeat(state, elapsed_sec, reps, target=target)
        if state == "idle":
            # heal: device idle but a session is still open (lost 'stopped' event)
            # [TH] เยียวยา: อุปกรณ์ว่างแต่ยังมีเซสชันเปิดค้าง (เหตุการณ์ 'stopped' หาย) จึงปิดให้
            open_session = db.get_open_session(device_id)
            if open_session is not None:
                db.close_session(
                    open_session["id"], self._iso_now(), prev_elapsed, prev_reps
                )

    # --- app-facing ---

    def queue_start(
        self, device_id: str = DEFAULT_DEVICE_ID, target_reps: int | None = None
    ) -> None:
        # [TH] แอปสั่งเริ่ม: กันซ้ำหลายทาง (มีเซสชันเปิดอยู่ / คำสั่งค้าง / start กำลังไปถึงอุปกรณ์)
        self._auto_close_if_silent(device_id)
        dev = self._device(device_id)
        if db.get_open_session(device_id) is not None:
            raise ConflictError("session already running")
        if dev.pending_command() == "start":
            raise ConflictError("start already queued")
        if dev.start_in_flight():
            raise ConflictError("start in progress")
        # set before queuing the command so a device that fetches on its very
        # next poll already sees the target in the same response
        # [TH] ตั้งค่าก่อนคิวคำสั่ง เพื่อให้อุปกรณ์ที่ถามทันทีในรอบถัดไปเห็นเป้าหมายในคำตอบเดียวกัน
        if target_reps is not None:
            dev.set_desired_target(target_reps)
        dev.queue_command("start")

    def queue_stop(self, device_id: str = DEFAULT_DEVICE_ID) -> None:
        self._auto_close_if_silent(device_id)
        if db.get_open_session(device_id) is None:
            raise ConflictError("no session running")
        self._device(device_id).queue_command("stop")

    def current(self, device_id: str = DEFAULT_DEVICE_ID) -> dict:
        self._auto_close_if_silent(device_id)
        dev = self._device(device_id)
        online = dev.device_online()
        open_session = db.get_open_session(device_id)
        if open_session is not None:
            return {
                "sessionId": open_session["id"],
                "state": "running",
                "elapsedSec": dev.live_elapsed_sec,
                "reps": dev.live_reps,
                "deviceOnline": online,
                "targetReps": dev.live_target,
            }
        state = (
            "starting"
            if dev.pending_command() == "start" or dev.start_in_flight()
            else "idle"
        )
        return {
            "sessionId": None,
            "state": state,
            "elapsedSec": 0,
            "reps": 0,
            "deviceOnline": online,
            "targetReps": dev.live_target,
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

    def _auto_close_if_silent(self, device_id: str = DEFAULT_DEVICE_ID) -> None:
        # [TH] ปิดเซสชันอัตโนมัติถ้าอุปกรณ์เงียบนานเกินกำหนด (ป้องกันเซสชันค้างค้าง)
        dev = self._device(device_id)
        silent_for = dev.seconds_since_heartbeat()
        if silent_for is None or silent_for <= AUTO_CLOSE_AFTER_SEC:
            return
        open_session = db.get_open_session(device_id)
        if open_session is not None:
            db.close_session(
                open_session["id"], self._iso_now(), dev.live_elapsed_sec, dev.live_reps
            )
            dev.live_state = "idle"
