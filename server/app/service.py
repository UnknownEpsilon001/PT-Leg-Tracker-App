"""Session state machine: the server-side source of truth (spec: Approach A).

[TH] เครื่องสถานะของเซสชัน ฝั่งเซิร์ฟเวอร์ถือเป็นความจริงหลัก (ต้นทาง)
     รับเหตุการณ์/สัญญาณชีพจาก ESP32 และคำสั่ง start/stop จากแอป แล้วบันทึกลงฐานข้อมูล
     พร้อมกลไกเยียวยา เช่น ปิดเซสชันค้างเมื่ออุปกรณ์เงียบหรือพลาดเหตุการณ์ 'stopped'
"""

import functools
import threading
import time
from datetime import datetime, timedelta, timezone

from . import db
from .db import DEFAULT_DEVICE_ID
from .state import DeviceState

AUTO_CLOSE_AFTER_SEC = 60
# An idle heartbeat closes an open session only once the session is this old: a
# heartbeat built before the start can land just after the started event.
# [TH] สัญญาณชีพ idle จะปิดรอบที่เปิดอยู่ได้เมื่อรอบมีอายุเกินนี้ เพราะสัญญาณชีพที่สร้างก่อนเริ่มอาจมาถึงหลังเหตุการณ์ started
HEAL_GRACE_SEC = 10

# The device writes its card in Thai local time, which has no DST.
# [TH] อุปกรณ์เขียนเวลาลงการ์ดเป็นเวลาไทย ซึ่งไม่มีการปรับเวลาตามฤดูกาล
DEVICE_TZ = timezone(timedelta(hours=7))


def _locked(method):
    """Run the method under the service lock.

    FastAPI runs these sync routes on a thread pool, so two requests could interleave
    inside a check-then-act (two starts both passing the checks, a poll reading a
    command with another command's id). One lock for the whole service is enough at
    a few requests a second.
    [TH] FastAPI รันเส้นทางแบบ sync บนหลายเธรด สองคำขออาจสลับกันกลางการตรวจแล้วทำ จึงล็อกทั้งบริการด้วยล็อกเดียว
    """

    @functools.wraps(method)
    def wrapper(self, *args, **kwargs):
        with self._lock:
            return method(self, *args, **kwargs)

    return wrapper


class ConflictError(Exception):
    """Request conflicts with current session state (HTTP 409)."""


class SessionService:
    def __init__(self, now=time.time):
        self._now = now
        self._lock = threading.RLock()
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

    @_locked
    def fetch_command(self, device_id: str = DEFAULT_DEVICE_ID) -> str | None:
        return self._device(device_id).fetch_command()

    @_locked
    def poll_command(self, device_id: str = DEFAULT_DEVICE_ID) -> dict:
        """Everything a device poll returns, read in one step so the command and its
        id always belong together.
        [TH] ทุกอย่างที่การโพลของอุปกรณ์ได้รับ อ่านในขั้นเดียว คำสั่งกับรหัสของมันจึงตรงกันเสมอ
        """
        dev = self._device(device_id)
        return {
            "command": dev.fetch_command(),
            "commandId": dev.command_id,
            "targetReps": dev.fetch_desired_target(),
            "timings": dev.fetch_desired_timings(),
        }

    @_locked
    def command_id(self, device_id: str = DEFAULT_DEVICE_ID) -> str:
        return self._device(device_id).command_id

    @_locked
    def fetch_target(self, device_id: str = DEFAULT_DEVICE_ID) -> int | None:
        return self._device(device_id).fetch_desired_target()

    @_locked
    def set_target(self, reps: int, device_id: str = DEFAULT_DEVICE_ID) -> None:
        # Never a ConflictError: the target is editable while a session runs, and
        # the controller applies a lowered one at the end of the current rest.
        # [TH] ไม่เคยชนสถานะ เพราะแก้เป้าหมายได้ระหว่างที่รอบกำลังเดิน
        #      และคอนโทรลเลอร์จะใช้ค่าที่ปรับลดลงเมื่อสิ้นสุดการพักครั้งปัจจุบัน
        self._device(device_id).set_desired_target(reps)

    @_locked
    def set_timings(self, timings: dict, device_id: str = DEFAULT_DEVICE_ID) -> None:
        self._device(device_id).set_desired_timings(timings)

    @_locked
    def fetch_timings(self, device_id: str = DEFAULT_DEVICE_ID) -> dict | None:
        return self._device(device_id).fetch_desired_timings()

    @_locked
    def handle_event(
        self,
        event_type: str,
        elapsed_sec: int,
        reps: int,
        device_id: str = DEFAULT_DEVICE_ID,
        uid: str | None = None,
    ) -> str | None:
        # [TH] จัดการเหตุการณ์จากอุปกรณ์: "started" เปิดเซสชันใหม่ อื่นๆ ปิดเซสชันที่เปิดอยู่
        dev = self._device(device_id)
        if event_type == "started":
            # A started event whose uid the server already holds is a retry of one
            # it acted on (the reply was lost, not the request). Answering with the
            # same row matters more than it looks: falling through would treat that
            # very session as a stale one and close it seconds after it began.
            # [TH] เหตุการณ์ started ที่มี uid ซึ่งเซิร์ฟเวอร์มีอยู่แล้ว คือการส่งซ้ำของเหตุการณ์ที่ทำไปแล้ว
            #      (คำตอบหาย ไม่ใช่คำขอหาย) การตอบด้วยแถวเดิมสำคัญกว่าที่เห็น ถ้าปล่อยผ่านไป
            #      เซสชันนั้นเองจะถูกมองว่าค้างและถูกปิดไม่กี่วินาทีหลังเริ่ม
            if uid:
                again = db.get_session_by_uid(device_id, uid)
                if again is not None:
                    return again["id"]
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
            session_id = db.create_session(self._iso_now(), origin, device_id, uid)
            # an event is proof of life: refresh liveness so a fresh session isn't
            # immediately auto-closed by silence that preceded this event
            # target is carried through, not defaulted -- this is a lifecycle
            # event, not a target report, so it must not blank live_target
            # [TH] ส่งต่อค่าเป้าหมายเดิม ไม่ใช้ค่าเริ่มต้น เพราะนี่คือเหตุการณ์วงจรชีวิต
            #      ไม่ใช่การรายงานเป้าหมาย จึงต้องไม่ทำให้ live_target ว่างไป
            dev.heartbeat("running", elapsed_sec, reps, target=dev.live_target)
            return session_id

        dev.heartbeat("idle", elapsed_sec, reps, target=dev.live_target)
        # A stop stays fetchable until this transition confirms that the device
        # acted. This makes one lost command response harmless.
        # [TH] คำสั่งหยุดจะยังถูกส่งซ้ำจนกว่าเหตุการณ์นี้ยืนยันว่าเครื่องทำตามแล้ว
        dev.clear_pending_command("stop")
        # a start cannot be in flight across a completed session
        dev.clear_start_in_flight()
        # A stopped event that names its session closes that one and nothing else.
        # Closing "the newest open one" let a late stop for an old session end the
        # session that had started after it, with the old one's numbers.
        # [TH] เหตุการณ์ stopped ที่ระบุหมายเลขรอบจะปิดเฉพาะรอบนั้น ไม่ใช่ "รอบล่าสุดที่เปิดอยู่"
        #      ซึ่งเคยทำให้คำสั่งหยุดที่มาช้าของรอบเก่าไปปิดรอบที่เริ่มทีหลังด้วยตัวเลขของรอบเก่า
        if uid:
            named = db.get_session_by_uid(device_id, uid)
            if named is not None:
                if named["ended_at"] is None:
                    db.close_session(named["id"], self._iso_now(), elapsed_sec, reps)
                return named["id"]
        open_session = db.get_open_session(device_id)
        if open_session is None:
            return None
        # A stop that names a session the server never saw is not for the open one,
        # unless that one carries no uid (a pre-uid device, or a healed row).
        # [TH] stop ที่ระบุรอบที่เซิร์ฟเวอร์ไม่รู้จัก ไม่ใช่ของรอบที่เปิดอยู่ ยกเว้นรอบนั้นไม่มี uid
        if uid and open_session["uid"]:
            return None
        db.close_session(open_session["id"], self._iso_now(), elapsed_sec, reps)
        return open_session["id"]

    @_locked
    def handle_upload(
        self,
        uid: str,
        ended_at: str | None,
        duration_sec: int,
        reps: int,
        fault: bool,
        patient_code: str | None,
        device_id: str = DEFAULT_DEVICE_ID,
    ) -> str:
        """A session row from the device's SD card, sent after the fact.

        ended_at is the card's local "YYYY-MM-DD HH:MM:SS", or None when the device
        did not know the time. Unknown or unreadable times fall back to now: the
        row still counts, it is just dated when it arrived.

        [TH] แถวรอบการทำงานจากการ์ด SD ของอุปกรณ์ ที่ส่งมาทีหลัง
             ended_at คือเวลาท้องถิ่นบนการ์ด หรือ None ถ้าอุปกรณ์ไม่ทราบเวลา
             เวลาที่ไม่ทราบหรืออ่านไม่ออกจะใช้เวลาปัจจุบันแทน แถวยังถูกนับ แค่ลงวันที่ตอนที่มาถึง
        """
        now = datetime.fromtimestamp(self._now(), tz=timezone.utc)
        duration = timedelta(seconds=max(duration_sec, 0))
        ended = None
        if ended_at:
            try:
                ended = datetime.strptime(ended_at, "%Y-%m-%d %H:%M:%S").replace(
                    tzinfo=DEVICE_TZ
                )
            except ValueError:
                ended = None
        # A finish time in the future is a device clock set wrong, not a session
        # that has not happened yet; it is dated on arrival like an unknown one.
        # Five minutes of slack covers ordinary drift between the two clocks.
        # [TH] เวลาจบที่อยู่ในอนาคตคือนาฬิกาอุปกรณ์ที่ตั้งผิด ไม่ใช่รอบที่ยังไม่เกิด จึงลงวันที่ตอนที่มาถึง
        #      เหมือนเวลาที่ไม่ทราบ เผื่อไว้ห้านาทีสำหรับความคลาดเคลื่อนปกติระหว่างสองนาฬิกา
        if ended is None or ended > now + timedelta(minutes=5):
            ended = now
        # A session the server already opened live keeps the server's clock at both
        # ends: its start came from that clock, and taking the end from the device's
        # would let any skew between the two make it end before it began.
        # [TH] รอบที่เซิร์ฟเวอร์เปิดไว้แล้วแบบสดใช้นาฬิกาของเซิร์ฟเวอร์ทั้งสองฝั่ง เพราะเวลาเริ่มมาจากนาฬิกานั้น
        #      ถ้าเอาเวลาจบจากนาฬิกาอุปกรณ์ ความคลาดเคลื่อนระหว่างสองนาฬิกาจะทำให้รอบจบก่อนที่จะเริ่มได้
        live = db.get_session_by_uid(device_id, uid)
        if live is not None and live["ended_at"] is None:
            started = datetime.fromisoformat(live["started_at"])
            ended = started + duration
        else:
            started = ended - duration
        # Stored in UTC like every row the server writes itself: the history is
        # sorted as text, and mixed offsets do not sort as times.
        # [TH] เก็บเป็น UTC เหมือนทุกแถวที่เซิร์ฟเวอร์เขียนเอง เพราะประวัติเรียงลำดับแบบข้อความ
        #      และเวลาที่มีเขตเวลาปนกันจะเรียงไม่ตรงตามเวลาจริง
        started = started.astimezone(timezone.utc)
        ended = ended.astimezone(timezone.utc)
        code = patient_code if patient_code and patient_code != "-" else None
        return db.record_device_session(
            device_id,
            uid,
            started.isoformat(),
            ended.isoformat(),
            duration_sec,
            reps,
            fault,
            code,
        )

    @_locked
    def handle_heartbeat(
        self,
        state: str,
        elapsed_sec: int,
        reps: int,
        target: int | None = None,
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
            if open_session is not None and self._age_sec(open_session) >= HEAL_GRACE_SEC:
                db.close_session(
                    open_session["id"], self._iso_now(), prev_elapsed, prev_reps
                )

    def _age_sec(self, session) -> float:
        started = datetime.fromisoformat(session["started_at"])
        return self._now() - started.timestamp()

    # --- app-facing ---

    @_locked
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

    @_locked
    def queue_stop(self, device_id: str = DEFAULT_DEVICE_ID) -> None:
        self._auto_close_if_silent(device_id)
        dev = self._device(device_id)
        if db.get_open_session(device_id) is None:
            # A stop before the start became a session cancels the start. Without
            # this the start kept repeating to the device for up to 30 s after the
            # user pressed stop, and the leg could still lift.
            # [TH] stop ก่อนที่ start จะกลายเป็นรอบ คือการยกเลิก start ไม่อย่างนั้น start ยังถูกส่งซ้ำอีกถึง 30 วินาที
            if dev.pending_command() == "start" or dev.start_in_flight():
                if dev.cancel_start():
                    dev.queue_command("stop")  # the device may already be moving
                return
            raise ConflictError("no session running")
        dev.queue_command("stop")

    @_locked
    def current(self, device_id: str = DEFAULT_DEVICE_ID) -> dict:
        self._auto_close_if_silent(device_id)
        dev = self._device(device_id)
        online = dev.device_online()
        open_session = db.get_open_session(device_id)
        # An edit the device has not yet echoed back over a heartbeat must
        # still show up here immediately: the app's re-read after setTarget()
        # relies on it, and live_target alone is the pre-edit number until the
        # device's next heartbeat. Fall back to live_target only once nothing
        # is outstanding.
        # [TH] การแก้ไขที่อุปกรณ์ยังไม่ได้ส่งสัญญาณชีพยืนยันกลับมาต้องยังปรากฏตรงนี้ทันที
        #      การอ่านซ้ำของแอปหลังเรียก setTarget() พึ่งพาค่านี้อยู่ และ live_target อย่างเดียว
        #      จะเป็นตัวเลขก่อนแก้ไขจนกว่าจะถึงสัญญาณชีพครั้งถัดไปของอุปกรณ์
        #      ใช้ live_target ก็ต่อเมื่อไม่มีค่าค้างอยู่เท่านั้น
        desired_target = dev.fetch_desired_target()
        target_reps = desired_target if desired_target is not None else dev.live_target
        if open_session is not None:
            return {
                "sessionId": open_session["id"],
                "state": "running",
                "elapsedSec": dev.live_elapsed_now(),
                "reps": dev.live_reps,
                "deviceOnline": online,
                "targetReps": target_reps,
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
            "targetReps": target_reps,
        }

    @_locked
    def claim(self, session_id: str, patient_code: str) -> bool:
        return db.claim_session(session_id, patient_code)

    @_locked
    def list_for_patient(self, patient_code: str) -> list[dict]:
        # Every device, not just the default one: a patient's sessions can come from
        # any machine, and a stale one left open on another would stay "running".
        # [TH] ทุกอุปกรณ์ ไม่ใช่แค่อุปกรณ์ตั้งต้น เพราะรอบของคนไข้มาจากเครื่องไหนก็ได้
        #      และรอบที่ค้างเปิดไว้บนเครื่องอื่นจะยังเป็น "กำลังทำงาน" อยู่
        for device_id in list(self._devices):
            self._auto_close_if_silent(device_id)
        return [
            {
                "id": row["id"],
                "startedAt": row["started_at"],
                "endedAt": row["ended_at"],
                "durationSec": row["duration_sec"],
                "reps": row["reps"],
                "patientCode": row["patient_code"],
                "fault": bool(row["fault"]),
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
