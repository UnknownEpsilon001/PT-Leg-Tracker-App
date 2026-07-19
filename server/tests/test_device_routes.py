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


def test_command_endpoint_scoped_by_device_id(clock):
    client = make_client(clock)
    client.app.state.service.queue_start("KNEE-01")
    assert client.get("/api/device/command", params={"deviceId": "KNEE-02"}).json() == {
        "command": None
    }
    assert client.get("/api/device/command", params={"deviceId": "KNEE-01"}).json() == {
        "command": "start"
    }


def test_heartbeat_tofu_creates_device(clock):
    client = make_client(clock)
    client.post(
        "/api/device/heartbeat",
        json={"state": "running", "elapsedSec": 5, "reps": 1, "deviceId": "KNEE-07"},
    )
    assert client.app.state.service._device("KNEE-07").device_online() is True
