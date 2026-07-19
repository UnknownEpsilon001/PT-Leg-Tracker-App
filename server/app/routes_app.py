"""Android-app-facing endpoints."""

from fastapi import APIRouter, HTTPException, Request
from pydantic import BaseModel

from .service import ConflictError

router = APIRouter(prefix="/api/sessions")


class ClaimBody(BaseModel):
    patientCode: str


@router.post("/start", status_code=202)
def start(request: Request, deviceId: str = "default"):
    try:
        request.app.state.service.queue_start(deviceId)
    except ConflictError as exc:
        raise HTTPException(status_code=409, detail=str(exc))
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
