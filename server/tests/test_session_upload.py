# [TH] เทสการอัปโหลดแถวรอบการทำงานจากการ์ด SD ย้อนหลัง (/api/device/session)
# Tests for the after-the-fact upload of SD-card session rows (/api/device/session)
from fastapi.testclient import TestClient

from app import db
from app.main import create_app
from app.service import SessionService


def make_client(clock):
    return TestClient(create_app(service=SessionService(now=clock)))


def upload(client, **over):
    body = {
        "uid": "a1b2c3d4",
        "endedAt": "2026-11-05 14:32:10",
        "durationSec": 300,
        "reps": 8,
        "fault": False,
        "patientCode": "P01",
    }
    body.update(over)
    return client.post("/api/device/session", json=body)


def test_offline_row_becomes_a_finished_session_for_its_patient(clock):
    client = make_client(clock)
    res = upload(client)
    assert res.status_code == 200
    rows = db.list_sessions("P01")
    assert len(rows) == 1
    row = rows[0]
    assert row["reps"] == 8 and row["duration_sec"] == 300
    # card time is Thai local; 14:32:10 +07:00 ends the session, 5 minutes after it began
    assert row["ended_at"] == "2026-11-05T14:32:10+07:00"
    assert row["started_at"] == "2026-11-05T14:27:10+07:00"
    assert row["origin"] == "device"


def test_upload_is_safe_to_repeat(clock):
    client = make_client(clock)
    first = upload(client).json()["sessionId"]
    second = upload(client).json()["sessionId"]
    assert first == second
    assert len(db.list_sessions("P01")) == 1


def test_upload_merges_with_the_live_session_of_the_same_uid(clock):
    client = make_client(clock)
    live = client.post(
        "/api/device/event",
        json={"type": "started", "elapsedSec": 0, "reps": 0, "sessionUid": "a1b2c3d4"},
    ).json()["sessionId"]
    # the stopped event never arrived; the card row closes the session instead
    merged = upload(client).json()["sessionId"]
    assert merged == live
    rows = db.list_sessions("P01")
    assert len(rows) == 1 and rows[0]["reps"] == 8


def test_upload_does_not_override_a_claim_made_in_the_app(clock):
    client = make_client(clock)
    sid = upload(client, patientCode="-").json()["sessionId"]
    db.claim_session(sid, "P02")
    upload(client, patientCode="P01")
    assert len(db.list_sessions("P02")) == 1
    assert db.list_sessions("P01") == []


def test_unknown_device_time_is_dated_on_arrival(clock):
    client = make_client(clock)
    upload(client, endedAt=None)
    row = db.list_sessions("P01")[0]
    assert row["ended_at"].startswith("1970-01-01T00:16:40")  # FakeClock starts at 1000 s


def test_unreadable_device_time_is_dated_on_arrival_not_rejected(clock):
    client = make_client(clock)
    assert upload(client, endedAt="?").status_code == 200
    assert len(db.list_sessions("P01")) == 1


def test_retried_started_event_does_not_close_its_own_session(clock):
    client = make_client(clock)
    body = {"type": "started", "elapsedSec": 0, "reps": 0, "sessionUid": "x9"}
    first = client.post("/api/device/event", json=body).json()["sessionId"]
    again = client.post("/api/device/event", json=body).json()["sessionId"]
    assert again == first
    assert db.get_open_session()["id"] == first


def test_fault_flag_is_kept(clock):
    client = make_client(clock)
    upload(client, fault=True)
    assert db.list_sessions("P01")[0]["fault"] == 1


def test_upload_rejects_an_empty_uid(clock):
    client = make_client(clock)
    assert upload(client, uid="").status_code == 422


def test_a_database_from_an_older_server_gains_the_new_columns(tmp_path, monkeypatch):
    import sqlite3

    path = tmp_path / "old.sqlite3"
    conn = sqlite3.connect(path)
    conn.execute(
        "CREATE TABLE sessions (id TEXT PRIMARY KEY, device_id TEXT NOT NULL,"
        " started_at TEXT NOT NULL, ended_at TEXT, duration_sec INTEGER,"
        " reps INTEGER NOT NULL DEFAULT 0, patient_code TEXT,"
        " origin TEXT NOT NULL CHECK (origin IN ('app', 'device')))"
    )
    conn.commit()
    conn.close()
    monkeypatch.setenv("PTLT_DB", str(path))
    db.record_device_session("default", "u1", "a", "b", 1, 1, True, "P09")
    assert db.list_sessions("P09")[0]["fault"] == 1
