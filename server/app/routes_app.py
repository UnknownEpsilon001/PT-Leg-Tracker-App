"""Android-app-facing endpoints.

[TH] เอนด์พอยต์ฝั่งแอปแอนดรอยด์: สั่งเริ่ม/หยุด (/start,/stop), อ่านสถานะปัจจุบัน (/current),
     ดึงประวัติตามรหัสผู้ป่วย และผูกเซสชันกับผู้ป่วย (/claim) ; ชนสถานะจะได้ HTTP 409
"""

from fastapi import APIRouter, HTTPException, Request
from pydantic import BaseModel, Field

from .service import ConflictError

router = APIRouter(prefix="/api/sessions")


class ClaimBody(BaseModel):
    patientCode: str


class StartBody(BaseModel):
    targetReps: int | None = Field(default=None, ge=0, le=99)


class TargetBody(BaseModel):
    targetReps: int = Field(ge=0, le=99)


@router.post("/start", status_code=202)
def start(request: Request, deviceId: str = "default", body: StartBody | None = None):
    try:
        request.app.state.service.queue_start(
            deviceId, target_reps=None if body is None else body.targetReps
        )
    except ConflictError as exc:
        raise HTTPException(status_code=409, detail=str(exc))
    return {"queued": True}


@router.post("/target", status_code=202)
def set_target(body: TargetBody, request: Request, deviceId: str = "default"):
    request.app.state.service.set_target(body.targetReps, deviceId)
    return {"queued": True}


class TimingsBody(BaseModel):
    liftSec: int | None = Field(default=None, ge=1, le=20)
    lowerSec: int | None = Field(default=None, ge=1, le=20)
    holdSec: int | None = Field(default=None, ge=1, le=60)
    restSec: int | None = Field(default=None, ge=1, le=60)
    maxTravelSec: int | None = Field(default=None, ge=1, le=20)


@router.post("/timings", status_code=202)
def set_timings(body: TimingsBody, request: Request, deviceId: str = "default"):
    # Only the fields that were sent. lift/lower are bounded to 20 here because
    # that is maxTravelSec's own ceiling; the controller clamps them again to the
    # maxTravelSec it actually holds, which is the only place that knows it.
    # [TH] ส่งเฉพาะฟิลด์ที่ถูกระบุมา ค่ายก/ลงจำกัดที่ 20 ตรงนี้เพราะเป็นเพดานของ maxTravelSec เอง
    #      คอนโทรลเลอร์จะจำกัดซ้ำด้วยค่า maxTravelSec ที่ถืออยู่จริง ซึ่งเป็นที่เดียวที่รู้ค่านั้น
    request.app.state.service.set_timings(
        body.model_dump(exclude_none=True), deviceId
    )
    return {"queued": True}


@router.post("/stop", status_code=202)
def stop(request: Request, deviceId: str = "default"):
    try:
        request.app.state.service.queue_stop(deviceId)
    except ConflictError as exc:
        raise HTTPException(status_code=409, detail=str(exc))
    return {"queued": True}


@router.get("/current")
def current(request: Request, deviceId: str = "default"):
    return request.app.state.service.current(deviceId)


@router.get("")
def list_sessions(patientCode: str, request: Request):
    return request.app.state.service.list_for_patient(patientCode)


@router.post("/{session_id}/claim")
def claim(session_id: str, body: ClaimBody, request: Request):
    if not request.app.state.service.claim(session_id, body.patientCode):
        raise HTTPException(status_code=404, detail="unknown session")
    return {"ok": True}
