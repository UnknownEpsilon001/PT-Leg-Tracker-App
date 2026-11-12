# จุดประกอบแอป FastAPI: เปิด CORS, ผูก service, และรวม router ทั้งฝั่งอุปกรณ์และฝั่งแอป
# FastAPI app factory: enables CORS, wires the service, mounts device + app routers.
import logging
import os
import re
import subprocess
from datetime import datetime, timezone
from pathlib import Path

from fastapi import FastAPI, Request, Response
from fastapi.middleware.cors import CORSMiddleware

from .routes_app import router as app_router
from .routes_device import router as device_router
from .service import SessionService

VERSION = "0.1.0"
log = logging.getLogger("uvicorn.error")


def _git(*args: str) -> str:
    try:
        return subprocess.run(
            ["git", *args],
            cwd=Path(__file__).parent,
            capture_output=True,
            text=True,
            timeout=5,
            check=True,
        ).stdout.strip()
    except (OSError, subprocess.SubprocessError):
        return "unknown"


def build_info() -> dict[str, str]:
    # BUILD_COMMIT / BUILD_BRANCH override git, for a deployment with no .git folder.
    # [TH] ใช้ค่าจากตัวแปรสภาพแวดล้อมแทน git ได้ สำหรับเครื่องที่ไม่มีโฟลเดอร์ .git
    return {
        "name": "server",
        "version": VERSION,
        "commit": os.environ.get("BUILD_COMMIT") or _git("rev-parse", "--short=7", "HEAD"),
        "branch": os.environ.get("BUILD_BRANCH") or _git("rev-parse", "--abbrev-ref", "HEAD"),
        "started": datetime.now(timezone.utc).isoformat(timespec="seconds"),
    }


def create_app(service: SessionService | None = None) -> FastAPI:
    app = FastAPI(title="Med-Flex Auto Smart Server")
    app.add_middleware(
        CORSMiddleware,
        allow_origins=["*"],
        allow_methods=["*"],
        allow_headers=["*"],
    )
    app.state.service = service or SessionService()
    app.include_router(device_router)
    app.include_router(app_router)

    build = build_info()
    log.info(
        "[BUILD] server v%s commit=%s branch=%s started=%s",
        build["version"], build["commit"], build["branch"], build["started"],
    )

    @app.get("/api/version")
    def version() -> dict[str, str]:
        return build

    # Browsers ask for this on every visit; answer empty instead of a logged 404.
    # [TH] เบราว์เซอร์ขอไฟล์นี้ทุกครั้งที่เปิด ตอบว่างไว้แทน 404 ที่ไปรกบันทึก
    @app.get("/favicon.ico", include_in_schema=False)
    def favicon() -> Response:
        return Response(status_code=204)

    # Writes and failures only: polls and heartbeats would bury them. The query
    # string stays out of the log because it can carry a patient code.
    # [TH] บันทึกเฉพาะคำสั่งเขียนและรายการที่ล้มเหลว เพราะการโพลและ heartbeat จะกลบข้อมูลหมด
    #      ไม่บันทึก query string เพราะอาจมีรหัสผู้ป่วย
    @app.middleware("http")
    async def log_events(request: Request, call_next):
        response = await call_next(request)
        path = request.url.path
        if response.status_code >= 400 or (
            request.method != "GET" and not path.endswith("/heartbeat")
        ):
            log.info("[API] %s %s -> %d", request.method, path, response.status_code)
        return response

    # Devices flashed before the firmware fix send "//api/..." when their server URL
    # ends in "/". Fold repeated slashes so they are not answered 404. Registered
    # last, so it runs before routing and before the log line above.
    # [TH] เครื่องที่ยังไม่ได้อัปเดตเฟิร์มแวร์จะส่ง "//api/..." ถ้า URL ลงท้ายด้วย / จึงรวม / ซ้ำให้เหลืออันเดียว
    @app.middleware("http")
    async def fold_slashes(request: Request, call_next):
        path = request.scope["path"]
        if "//" in path:
            request.scope["path"] = re.sub(r"/{2,}", "/", path)
        return await call_next(request)

    return app


app = create_app()
