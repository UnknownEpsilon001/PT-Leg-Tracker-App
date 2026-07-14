from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

from .routes_app import router as app_router
from .routes_device import router as device_router
from .service import SessionService


def create_app(service: SessionService | None = None) -> FastAPI:
    app = FastAPI(title="PT Leg Tracker Server")
    app.add_middleware(
        CORSMiddleware,
        allow_origins=["*"],
        allow_methods=["*"],
        allow_headers=["*"],
    )
    app.state.service = service or SessionService()
    app.include_router(device_router)
    app.include_router(app_router)
    return app


app = create_app()
