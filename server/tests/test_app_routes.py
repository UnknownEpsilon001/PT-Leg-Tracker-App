# [TH] เทสระดับ HTTP: จำลองฝั่งแอปเรียก API เซสชัน (start/stop/claim/list) ผ่าน TestClient
# HTTP-level tests: simulate the app side calling the session API (start/stop/claim/list) via TestClient
from fastapi.testclient import TestClient

from app.main import create_app
from app.service import SessionService


def make_client(clock):
    return TestClient(create_app(service=SessionService(now=clock)))


def device_starts(client, elapsed=0, reps=0):
    return client.post(
        "/api/device/event", json={"type": "started", "elapsedSec": elapsed, "reps": reps}
    ).json()["sessionId"]


def test_start_queues_command(clock):
    client = make_client(clock)
    res = client.post("/api/sessions/start")
    assert res.status_code == 202
    assert res.json() == {"queued": True}
    assert client.get("/api/device/command").json()["command"] == "start"


def test_start_conflict_while_running(clock):
    client = make_client(clock)
    device_starts(client)
    assert client.post("/api/sessions/start").status_code == 409


def test_stop_conflict_when_idle(clock):
    client = make_client(clock)
    assert client.post("/api/sessions/stop").status_code == 409


def test_full_app_controlled_flow(clock):
    client = make_client(clock)
    client.post("/api/sessions/start")
    assert client.get("/api/sessions/current").json()["state"] == "starting"

    # mock firmware side: fetch command, report started, heartbeat
    assert client.get("/api/device/command").json()["command"] == "start"
    client.post("/api/device/event", json={"type": "started"})
    client.post(
        "/api/device/heartbeat", json={"state": "running", "elapsedSec": 10, "reps": 2}
    )

    view = client.get("/api/sessions/current").json()
    assert view["state"] == "running"
    assert view["elapsedSec"] == 10
    assert view["reps"] == 2
    assert view["deviceOnline"] is True
    session_id = view["sessionId"]

    assert client.post("/api/sessions/stop").status_code == 202
    assert client.get("/api/device/command").json()["command"] == "stop"
    client.post(
        "/api/device/event", json={"type": "stopped", "elapsedSec": 120, "reps": 11}
    )
    assert client.get("/api/sessions/current").json()["state"] == "idle"

    assert (
        client.post(
            f"/api/sessions/{session_id}/claim", json={"patientCode": "PT001"}
        ).status_code
        == 200
    )
    rows = client.get("/api/sessions", params={"patientCode": "PT001"}).json()
    assert len(rows) == 1
    assert rows[0]["id"] == session_id
    assert rows[0]["durationSec"] == 120
    assert rows[0]["reps"] == 11
    assert rows[0]["patientCode"] == "PT001"


def test_claim_unknown_session_404(clock):
    client = make_client(clock)
    res = client.post("/api/sessions/no-such-id/claim", json={"patientCode": "PT001"})
    assert res.status_code == 404


def test_list_requires_patient_code(clock):
    client = make_client(clock)
    assert client.get("/api/sessions").status_code == 422


def test_post_target_returns_202_and_queues_it(clock):
    client = make_client(clock)
    assert client.post("/api/sessions/target", json={"targetReps": 6}).status_code == 202
    assert client.get("/api/device/command").json()["targetReps"] == 6


def test_post_target_rejects_out_of_range(clock):
    client = make_client(clock)
    assert client.post("/api/sessions/target", json={"targetReps": 100}).status_code == 422
    assert client.post("/api/sessions/target", json={"targetReps": -1}).status_code == 422


def test_start_accepts_a_target_in_the_body(clock):
    client = make_client(clock)
    assert client.post("/api/sessions/start", json={"targetReps": 4}).status_code == 202
    assert client.get("/api/device/command").json()["targetReps"] == 4


def test_start_without_a_body_still_works(clock):
    client = make_client(clock)
    assert client.post("/api/sessions/start").status_code == 202


def test_post_timings_returns_202_and_reaches_the_device(clock):
    client = make_client(clock)
    body = {"liftSec": 3, "lowerSec": 3, "holdSec": 8, "restSec": 8, "maxTravelSec": 9}
    assert client.post("/api/sessions/timings", json=body).status_code == 202
    assert client.get("/api/device/command").json()["timings"] == body


def test_post_timings_rejects_out_of_range(clock):
    client = make_client(clock)
    assert client.post(
        "/api/sessions/timings", json={"liftSec": 0}
    ).status_code == 422
    assert client.post(
        "/api/sessions/timings", json={"holdSec": 61}
    ).status_code == 422


def test_command_reports_no_timings_when_none_are_wanted(clock):
    client = make_client(clock)
    assert client.get("/api/device/command").json()["timings"] is None


def test_sessions_isolated_by_device_id(clock):
    client = make_client(clock)
    client.post("/api/sessions/start", params={"deviceId": "KNEE-01"})
    # KNEE-02 sees no running/queued state
    assert client.get(
        "/api/sessions/current", params={"deviceId": "KNEE-02"}
    ).json()["state"] == "idle"
    assert client.get(
        "/api/sessions/current", params={"deviceId": "KNEE-01"}
    ).json()["state"] == "starting"
