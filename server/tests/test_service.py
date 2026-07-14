import pytest

from app import db
from app.service import AUTO_CLOSE_AFTER_SEC, ConflictError, SessionService


def make_service(clock):
    return SessionService(now=clock)


# --- dual-origin session lifecycle ---

def test_app_started_session_has_app_origin(clock):
    svc = make_service(clock)
    svc.queue_start()
    assert svc.fetch_command() == "start"
    session_id = svc.handle_event("started", 0, 0)
    assert session_id is not None
    assert db.get_open_session()["origin"] == "app"


def test_button_started_session_has_device_origin(clock):
    svc = make_service(clock)
    session_id = svc.handle_event("started", 0, 0)
    assert session_id is not None
    assert db.get_open_session()["origin"] == "device"


def test_stopped_event_closes_session_with_final_numbers(clock):
    svc = make_service(clock)
    session_id = svc.handle_event("started", 0, 0)
    clock.advance(300)
    assert svc.handle_event("stopped", 300, 25) == session_id
    assert db.get_open_session() is None
    svc.claim(session_id, "PT001")
    row = svc.list_for_patient("PT001")[0]
    assert row["durationSec"] == 300
    assert row["reps"] == 25


def test_stopped_with_no_open_session_is_noop(clock):
    svc = make_service(clock)
    assert svc.handle_event("stopped", 10, 1) is None


def test_started_while_open_closes_stale_and_opens_new(clock):
    svc = make_service(clock)
    stale_id = svc.handle_event("started", 0, 0)
    svc.handle_heartbeat("running", 120, 9)
    new_id = svc.handle_event("started", 0, 0)
    assert new_id != stale_id
    open_row = db.get_open_session()
    assert open_row["id"] == new_id
    svc.claim(stale_id, "PT001")
    stale_row = svc.list_for_patient("PT001")[0]
    assert stale_row["durationSec"] == 120
    assert stale_row["reps"] == 9


# --- queueing rules ---

def test_queue_start_conflicts_while_running(clock):
    svc = make_service(clock)
    svc.handle_event("started", 0, 0)
    with pytest.raises(ConflictError):
        svc.queue_start()


def test_queue_start_conflicts_while_start_pending(clock):
    svc = make_service(clock)
    svc.queue_start()
    with pytest.raises(ConflictError):
        svc.queue_start()


def test_queue_stop_conflicts_when_idle(clock):
    svc = make_service(clock)
    with pytest.raises(ConflictError):
        svc.queue_stop()


def test_queue_start_allowed_after_command_expiry(clock):
    svc = make_service(clock)
    svc.queue_start()
    clock.advance(31)
    svc.queue_start()  # must not raise


# --- current() view ---

def test_current_idle(clock):
    svc = make_service(clock)
    view = svc.current()
    assert view == {
        "sessionId": None,
        "state": "idle",
        "elapsedSec": 0,
        "reps": 0,
        "deviceOnline": False,
    }


def test_current_starting_then_running(clock):
    svc = make_service(clock)
    svc.handle_heartbeat("idle", 0, 0)
    svc.queue_start()
    assert svc.current()["state"] == "starting"
    svc.fetch_command()
    session_id = svc.handle_event("started", 0, 0)
    svc.handle_heartbeat("running", 5, 1)
    view = svc.current()
    assert view["state"] == "running"
    assert view["sessionId"] == session_id
    assert view["elapsedSec"] == 5
    assert view["reps"] == 1
    assert view["deviceOnline"] is True


def test_current_starting_expires_back_to_idle(clock):
    svc = make_service(clock)
    svc.queue_start()
    clock.advance(31)
    assert svc.current()["state"] == "idle"


def test_fetched_start_still_reports_starting_until_confirmed(clock):
    svc = make_service(clock)
    svc.queue_start()
    svc.fetch_command()
    assert svc.current()["state"] == "starting"
    with pytest.raises(ConflictError):
        svc.queue_start()


def test_fetched_start_in_flight_expires(clock):
    svc = make_service(clock)
    svc.queue_start()
    svc.fetch_command()
    clock.advance(31)
    assert svc.current()["state"] == "idle"
    svc.queue_start()  # must not raise


# --- healing rules ---

def test_auto_close_after_heartbeat_silence(clock):
    svc = make_service(clock)
    session_id = svc.handle_event("started", 0, 0)
    svc.handle_heartbeat("running", 90, 7)
    clock.advance(AUTO_CLOSE_AFTER_SEC + 1)
    assert svc.current()["state"] == "idle"
    svc.claim(session_id, "PT001")
    row = svc.list_for_patient("PT001")[0]
    assert row["durationSec"] == 90
    assert row["reps"] == 7


def test_idle_heartbeat_closes_lost_session(clock):
    svc = make_service(clock)
    session_id = svc.handle_event("started", 0, 0)
    svc.handle_heartbeat("running", 45, 4)
    svc.handle_heartbeat("idle", 0, 0)
    assert db.get_open_session() is None
    svc.claim(session_id, "PT001")
    row = svc.list_for_patient("PT001")[0]
    assert row["durationSec"] == 45
    assert row["reps"] == 4
