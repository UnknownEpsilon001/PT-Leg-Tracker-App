# จุดประกอบแอป FastAPI: เปิด CORS, ผูก service, และรวม router ทั้งฝั่งอุปกรณ์และฝั่งแอป
# FastAPI app factory: enables CORS, wires the service, mounts device + app routers.
import logging
import os
import subprocess
from datetime import datetime, timezone
from pathlib import Path

from fastapi import FastAPI, Request
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

    return app


app = create_app()
