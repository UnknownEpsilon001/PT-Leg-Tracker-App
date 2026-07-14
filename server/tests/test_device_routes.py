from fastapi.testclient import TestClient

from app.main import create_app
from app.service import SessionService


def make_client(clock):
    return TestClient(create_app(service=SessionService(now=clock)))


def test_command_endpoint_consumes(clock):
    client = make_client(clock)
    service = client.app.state.service
    service.queue_start()
    assert client.get("/api/device/command").json() == {"command": "start"}
    assert client.get("/api/device/command").json() == {"command": None}


def test_event_started_returns_session_id(clock):
    client = make_client(clock)
    res = client.post(
        "/api/device/event", json={"type": "started", "elapsedSec": 0, "reps": 0}
    )
    assert res.status_code == 200
    assert res.json()["sessionId"]


def test_event_stopped_without_open_session_returns_null(clock):
    client = make_client(clock)
    res = client.post(
        "/api/device/event", json={"type": "stopped", "elapsedSec": 10, "reps": 2}
    )
    assert res.status_code == 200
    assert res.json() == {"sessionId": None}


def test_event_rejects_unknown_type(clock):
    client = make_client(clock)
    res = client.post("/api/device/event", json={"type": "exploded"})
    assert res.status_code == 422


def test_heartbeat_updates_liveness(clock):
    client = make_client(clock)
    res = client.post(
        "/api/device/heartbeat",
        json={"state": "running", "elapsedSec": 30, "reps": 4},
    )
    assert res.status_code == 200
    assert res.json() == {"ok": True}
    assert client.app.state.service.device.device_online() is True


def test_cors_headers_present(clock):
    client = make_client(clock)
    res = client.get("/api/device/command", headers={"Origin": "https://localhost"})
    assert res.headers.get("access-control-allow-origin") == "*"
