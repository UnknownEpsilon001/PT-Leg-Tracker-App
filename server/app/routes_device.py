"""ESP32-facing endpoints. The device initiates every request.

[TH] เอนด์พอยต์ฝั่งอุปกรณ์ ESP32: อุปกรณ์เป็นฝ่ายเรียกเข้ามาเองทุกครั้ง
     ดึงคำสั่ง (/command), ส่งเหตุการณ์ started/stopped (/event), ส่งสัญญาณชีพ (/heartbeat)
     และส่งแถวรอบการทำงานจากการ์ด SD ย้อนหลัง (/session)
"""

from typing import Literal

from fastapi import APIRouter, Request
from pydantic import BaseModel, Field

router = APIRouter(prefix="/api/device")


class EventBody(BaseModel):
    type: Literal["started", "stopped"]
    elapsedSec: int = 0
    reps: int = 0
    deviceId: str = "default"
    # The controller's number for this session; absent on older images.
    # [TH] หมายเลขของรอบนี้จากคอนโทรลเลอร์ อุปกรณ์รุ่นเก่าจะไม่มีฟิลด์นี้
    sessionUid: str | None = Field(default=None, max_length=64)


class SessionUploadBody(BaseModel):
    """One row of the device's sessions.csv, sent once the server can be reached.

    [TH] หนึ่งแถวจาก sessions.csv ของอุปกรณ์ ส่งมาเมื่อติดต่อเซิร์ฟเวอร์ได้
    """

    uid: str = Field(min_length=1, max_length=64)
    endedAt: str | None = None  # card local time "YYYY-MM-DD HH:MM:SS", None = unknown
    durationSec: int = Field(default=0, ge=0)
    reps: int = Field(default=0, ge=0)
    fault: bool = False
    patientCode: str | None = Field(default=None, max_length=64)
    deviceId: str = "default"


class HeartbeatBody(BaseModel):
    state: Literal["idle", "running"]
    elapsedSec: int = 0
    reps: int = 0
    # None, not 0: a heartbeat that omits this field (an ESP32 still running a
    # pre-target-echo image) must not be read as acking a desired target of
    # exactly 0 — see DeviceState.heartbeat.
    # [TH] เป็น None ไม่ใช่ 0 สัญญาณชีพที่ไม่มีฟิลด์นี้ (ESP32 รุ่นเก่าก่อนมีการยืนยันเป้าหมายกลับ)
    #      ต้องไม่ถูกตีความว่ายืนยันเป้าหมาย 0 พอดี ดู DeviceState.heartbeat
    target: int | None = None
    deviceId: str = "default"


@router.get("/command")
def get_command(request: Request, deviceId: str = "default"):
    return request.app.state.service.poll_command(deviceId)


@router.post("/event")
def post_event(body: EventBody, request: Request):
    session_id = request.app.state.service.handle_event(
        body.type, body.elapsedSec, body.reps, body.deviceId, body.sessionUid
    )
    return {"sessionId": session_id}


@router.post("/session")
def post_session(body: SessionUploadBody, request: Request):
    session_id = request.app.state.service.handle_upload(
        body.uid,
        body.endedAt,
        body.durationSec,
        body.reps,
        body.fault,
        body.patientCode,
        body.deviceId,
    )
    return {"sessionId": session_id}


@router.post("/heartbeat")
def post_heartbeat(body: HeartbeatBody, request: Request):
    request.app.state.service.handle_heartbeat(
        body.state, body.elapsedSec, body.reps, body.target, body.deviceId
    )
    return {"ok": True}
