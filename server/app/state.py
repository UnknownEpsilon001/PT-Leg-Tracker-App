"""In-memory device-facing state: one-slot command queue + liveness tracking.

[TH] สถานะฝั่งอุปกรณ์ที่เก็บในหน่วยความจำ: คิวคำสั่งช่องเดียว + การติดตามว่าอุปกรณ์ยังมีชีวิต
     ใช้เป็นตัวกลางระหว่างแอป (สั่ง start/stop) กับ ESP32 (มาดึงคำสั่งและส่งสัญญาณชีพ)
"""

import time

COMMAND_TTL_SEC = 30  # คำสั่งที่ค้างเกินนี้ถือว่าหมดอายุ (วินาที)
DEVICE_OFFLINE_AFTER_SEC = 10  # ไม่ได้ยินสัญญาณชีพเกินนี้ถือว่าอุปกรณ์ออฟไลน์ (วินาที)


class DeviceState:
    def __init__(self, now=time.time):
        self._now = now
        self._pending_command: str | None = None
        self._queued_at = 0.0
        self._start_command_fetched = False
        self._start_fetched_at = 0.0
        self._last_heartbeat_at: float | None = None
        self.live_state = "idle"
        self.live_elapsed_sec = 0
        self.live_reps = 0
        self.live_target = 0
        self._desired_target: int | None = None
        self._target_queued_at = 0.0
        self._desired_timings: dict | None = None
        self._timings_queued_at = 0.0

    def queue_command(self, command: str) -> None:
        self._pending_command = command
        self._queued_at = self._now()

    def pending_command(self) -> str | None:
        """Pending command, dropping it first if older than COMMAND_TTL_SEC."""
        if (
            self._pending_command is not None
            and self._now() - self._queued_at > COMMAND_TTL_SEC
        ):
            self._pending_command = None
        return self._pending_command

    def fetch_command(self) -> str | None:
        """Consume-on-read: hand the command to the device and clear the slot.

        [TH] อ่านแล้วล้าง: ส่งคำสั่งให้อุปกรณ์แล้วเคลียร์ช่องทันที กันส่งซ้ำ
        """
        command = self.pending_command()
        self._pending_command = None
        if command == "start":
            self._start_command_fetched = True
            self._start_fetched_at = self._now()
        elif command is not None:
            self._start_command_fetched = False
        return command

    def clear_pending_start(self) -> None:
        """Drop a still-queued 'start' if the start intent is already satisfied
        (e.g. the physical button won the race before the device fetched it)."""
        if self._pending_command == "start":
            self._pending_command = None

    def clear_start_in_flight(self) -> None:
        """A start cannot be in flight across a completed session."""
        self._start_command_fetched = False

    def consume_start_origin(self) -> bool:
        """True exactly once if the upcoming 'started' event follows an app command."""
        fetched = self._start_command_fetched
        self._start_command_fetched = False
        return fetched

    def start_in_flight(self) -> bool:
        """True while a fetched start awaits its 'started' confirmation (bounded by COMMAND_TTL_SEC)."""
        return (
            self._start_command_fetched
            and self._now() - self._start_fetched_at <= COMMAND_TTL_SEC
        )

    def set_desired_target(self, reps: int) -> None:
        self._desired_target = reps
        self._target_queued_at = self._now()

    def fetch_desired_target(self) -> int | None:
        """The target the device should adopt, or None when it already has it.

        Not consume-on-read, unlike commands: a command that is applied twice
        starts two sessions, while a target that is applied twice is idempotent.
        So it is repeated until the device's own heartbeat reports that value back,
        which survives a dropped response.

        The acknowledgement is not the only way out, though: it also expires after
        COMMAND_TTL_SEC, like the timings do. A device whose heartbeat omits the
        field entirely — the pre-target-echo image that `target: int | None` in
        routes_device.py exists to model — can never ack, and without the clock
        this would repeat on every 1 Hz poll for the life of the process while
        current_status() kept reporting a desire the machine may never have taken.
        The device polls once a second, so anything online has fetched it long
        before the window closes.

        [TH] ไม่ใช่อ่านแล้วล้างแบบคำสั่ง เพราะคำสั่งที่ถูกใช้ซ้ำจะเริ่มสองรอบ
             แต่เป้าหมายที่ถูกใช้ซ้ำให้ผลเหมือนเดิม
             จึงส่งซ้ำไปจนกว่าสัญญาณชีพของอุปกรณ์จะรายงานค่านั้นกลับมา ซึ่งทนต่อการที่คำตอบหายได้
             แต่การยืนยันกลับไม่ใช่ทางออกทางเดียว ค่านี้หมดอายุตาม COMMAND_TTL_SEC เหมือนค่าเวลาของรอบด้วย
             อุปกรณ์ที่สัญญาณชีพไม่มีฟิลด์นี้เลย ซึ่งเป็นรุ่นก่อนมีการยืนยันกลับที่ target: int | None
             ใน routes_device.py มีไว้รองรับ จะยืนยันกลับไม่ได้เลย
             ถ้าไม่มีการจับเวลา ค่านี้จะถูกส่งซ้ำทุกครั้งที่โพลวินาทีละครั้งไปตลอดอายุโปรเซส
             และ current_status() ก็จะรายงานค่าที่เครื่องอาจไม่เคยรับไปใช้เลย
             อุปกรณ์โพลวินาทีละครั้ง เครื่องที่ออนไลน์อยู่จึงดึงค่าไปนานแล้วก่อนหน้าต่างเวลาจะปิด
        """
        if (
            self._desired_target is not None
            and self._now() - self._target_queued_at > COMMAND_TTL_SEC
        ):
            self._desired_target = None
        return self._desired_target

    def set_desired_timings(self, timings: dict) -> None:
        self._desired_timings = timings
        self._timings_queued_at = self._now()

    def fetch_desired_timings(self) -> dict | None:
        """Cycle timings the device should adopt, until COMMAND_TTL_SEC passes.

        Unlike the target, nothing echoes these back — the heartbeat has no room
        for four more fields and there is nothing useful to do with a mismatch —
        so they expire on a clock instead of on an acknowledgement. Repeating
        them forever would re-apply a stale edit to a controller that had since
        been reconfigured at its own screen.

        [TH] ต่างจากเป้าหมายจำนวนครั้ง ตรงที่ไม่มีอะไรส่งค่าเหล่านี้กลับมายืนยัน
             สัญญาณชีพไม่มีที่ให้อีกสี่ฟิลด์ และถึงรู้ว่าไม่ตรงกันก็ทำอะไรไม่ได้
             จึงให้หมดอายุตามเวลาแทนการรอการยืนยัน
             ถ้าส่งซ้ำไปเรื่อยๆ ค่าที่แก้ไว้นานแล้วจะไปทับเครื่องที่ถูกตั้งค่าใหม่จากหน้าจอของตัวเองไปแล้ว
        """
        if (
            self._desired_timings is not None
            and self._now() - self._timings_queued_at > COMMAND_TTL_SEC
        ):
            self._desired_timings = None
        return self._desired_timings

    def heartbeat(
        self, state: str, elapsed_sec: int, reps: int, target: int | None = None
    ) -> None:
        # target is None, not 0, for a heartbeat that omits the field entirely
        # (an ESP32 still on a pre-target-echo image) — 0 is itself a valid
        # desired value (unlimited reps), so treating a missing field as 0
        # would ack that desire away and clobber a live_target the device
        # never actually reported. Skip both when the field was not sent.
        # [TH] target เป็น None ไม่ใช่ 0 สำหรับสัญญาณชีพที่ไม่มีฟิลด์นี้เลย
        #      (ESP32 รุ่นเก่าก่อนมีการยืนยันเป้าหมายกลับ) เพราะ 0 เองก็เป็นค่าเป้าหมายที่ถูกต้อง
        #      (ทำไม่จำกัดจำนวนครั้ง) ถ้าถือว่าฟิลด์ที่หายไปคือ 0 จะไปยืนยันความต้องการนั้นทิ้งไป
        #      และทับ live_target ด้วยค่าที่อุปกรณ์ไม่ได้รายงานมาจริง จึงข้ามทั้งสองกรณีถ้าไม่มีฟิลด์นี้ส่งมา
        self._last_heartbeat_at = self._now()
        self.live_state = state
        self.live_elapsed_sec = elapsed_sec
        self.live_reps = reps
        if target is None:
            return
        self.live_target = target
        if self._desired_target is not None and target == self._desired_target:
            self._desired_target = None

    def device_online(self) -> bool:
        # [TH] อุปกรณ์ออนไลน์ = เพิ่งได้ยินสัญญาณชีพภายในเวลาที่กำหนด
        if self._last_heartbeat_at is None:
            return False
        return self._now() - self._last_heartbeat_at <= DEVICE_OFFLINE_AFTER_SEC

    def seconds_since_heartbeat(self) -> float | None:
        if self._last_heartbeat_at is None:
            return None
        return self._now() - self._last_heartbeat_at
