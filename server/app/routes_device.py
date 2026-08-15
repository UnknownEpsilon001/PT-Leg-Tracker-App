"""ESP32-facing endpoints. The device initiates every request.

[TH] เอนด์พอยต์ฝั่งอุปกรณ์ ESP32: อุปกรณ์เป็นฝ่ายเรียกเข้ามาเองทุกครั้ง
     ดึงคำสั่ง (/command), ส่งเหตุการณ์ started/stopped (/event), ส่งสัญญาณชีพ (/heartbeat)
"""

from typing import Literal

from fastapi import APIRouter, Request
from pydantic import BaseModel

router = APIRouter(prefix="/api/device")


class EventBody(BaseModel):
    type: Literal["started", "stopped"]
    elapsedSec: int = 0
    reps: int = 0
    deviceId: str = "default"


class HeartbeatBody(BaseModel):
    state: Literal["idle", "running"]
    elapsedSec: int = 0
    reps: int = 0
    target: int = 0
    deviceId: str = "default"


@router.get("/command")
def get_command(request: Request, deviceId: str = "default"):
    service = request.app.state.service
    return {
        "command": service.fetch_command(deviceId),
        "targetReps": service.fetch_target(deviceId),
        "timings": service.fetch_timings(deviceId),
    }


@router.post("/event")
def post_event(body: EventBody, request: Request):
    session_id = request.app.state.service.handle_event(
        body.type, body.elapsedSec, body.reps, body.deviceId
    )
    return {"sessionId": session_id}


@router.post("/heartbeat")
def post_heartbeat(body: HeartbeatBody, request: Request):
    request.app.state.service.handle_heartbeat(
        body.state, body.elapsedSec, body.reps, body.target, body.deviceId
    )
    return {"ok": True}
