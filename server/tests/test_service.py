# [TH] เทสตรรกะหลักของ SessionService: วงจรเซสชันสองต้นทาง (แอป/ปุ่มอุปกรณ์),
# กติกาการ queue คำสั่ง, และการเยียวยาตัวเองเมื่อ heartbeat ขาดหาย
# Tests for SessionService's core logic: dual-origin (app/device button) session
# lifecycle, command-queueing rules, and self-healing on missed heartbeats
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


def test_started_event_refreshes_liveness_after_long_silence(clock):
    svc = make_service(clock)
    svc.handle_heartbeat("idle", 0, 0)
    clock.advance(AUTO_CLOSE_AFTER_SEC + 1)  # >60s silence, e.g. power-cycle/WiFi drop
    session_id = svc.handle_event("started", 0, 0)
    view = svc.current()
    assert view["state"] == "running"
    assert view["sessionId"] == session_id


def test_button_win_clears_stale_pending_start_and_avoids_phantom_starting(clock):
    svc = make_service(clock)
    svc.queue_start()
    # [TH] ปุ่มกดที่ตัวเครื่องชนะ race ก่อนอุปกรณ์จะไป fetch คำสั่งที่ queue ไว้
    # physical button wins the race before the device fetches the queued command
    session_id = svc.handle_event("started", 0, 0)
    assert db.get_open_session()["origin"] == "device"

    # [TH] อุปกรณ์ poll มาช้าต้องไม่เห็นคำสั่ง "start" ค้างอยู่ -- เจตนาสำเร็จไปแล้ว
    # จึงต้องไม่ถูกรายงานว่ายัง fetch ได้
    # device's late poll must not see a stale "start" -- the intent is already
    # satisfied, so it must not be reported as still fetchable
    assert svc.fetch_command() is None

    clock.advance(5)
    assert svc.handle_event("stopped", 5, 1) == session_id
    assert db.get_open_session() is None

    # [TH] ต้องไม่มีสถานะ "starting" หลอกๆ และไม่มี 409 มั่วจากแฟลก in-flight ที่หลงเหลือ
    # no phantom "starting" state and no bogus 409 from a leftover in-flight flag
    assert svc.current()["state"] == "idle"
    svc.queue_start()  # must not raise


def test_start_in_flight_flag_cleared_on_stop_prevents_origin_leak(clock):
    svc = make_service(clock)
    # device genuinely fetched a start command (in-flight flag set)
    svc.queue_start()
    svc.fetch_command()
    assert svc.device.start_in_flight() is True

    svc.handle_event("started", 0, 0)
    svc.handle_event("stopped", 5, 1)
    assert svc.device.start_in_flight() is False

    # [TH] การ start ด้วยปุ่มล้วนๆ ครั้งถัดไป (ไม่มีแอปเกี่ยวข้อง) ต้องได้ origin เป็น device
    # ไม่ใช่ "app" จากแฟลก in-flight ที่หลุดค้างมา
    # a subsequent pure button start (no app involvement) must get device origin,
    # not "app" from a leaked in-flight flag
    svc.handle_event("started", 0, 0)
    assert db.get_open_session()["origin"] == "device"


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
        "targetReps": 0,
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


from app.db import DEFAULT_DEVICE_ID


def test_two_devices_have_isolated_queues_and_sessions(clock):
    svc = SessionService(now=clock)
    svc.queue_start("KNEE-01")
    # only KNEE-01's queue has the command
    assert svc.fetch_command("KNEE-02") is None
    assert svc.fetch_command("KNEE-01") == "start"
    # KNEE-01 starts; KNEE-02 stays idle
    svc.handle_event("started", 0, 0, "KNEE-01")
    assert svc.current("KNEE-01")["state"] == "running"
    assert svc.current("KNEE-02")["state"] == "idle"
    # stopping KNEE-01 does not touch KNEE-02
    svc.handle_event("stopped", 40, 2, "KNEE-01")
    assert svc.current("KNEE-01")["state"] == "idle"
    assert svc.current("KNEE-02")["sessionId"] is None


def test_queue_start_with_a_target_sets_the_desired_target(clock):
    svc = make_service(clock)
    svc.queue_start(target_reps=5)
    assert svc.fetch_target() == 5
    assert svc.fetch_command() == "start"


def test_set_target_works_while_a_session_runs(clock):
    svc = make_service(clock)
    svc.handle_event("started", 0, 0)
    svc.set_target(12)
    assert svc.fetch_target() == 12


def test_current_reports_the_live_target(clock):
    svc = make_service(clock)
    svc.handle_heartbeat("idle", 0, 0, target=8)
    assert svc.current()["targetReps"] == 8


# A target edit must show up in current() the moment it is made, with no
# intervening heartbeat -- the app's re-read after setTarget() depends on
# this, and a heartbeat is the one thing that is NOT guaranteed to have
# happened yet (it can take up to a few seconds). Covers both branches of
# current(): no open session (idle/starting) and an open session (running).
def test_set_target_is_visible_in_current_with_no_heartbeat(clock):
    svc = make_service(clock)
    svc.set_target(11)
    assert svc.current()["targetReps"] == 11


def test_set_target_is_visible_in_current_while_running_with_no_heartbeat(clock):
    svc = make_service(clock)
    svc.handle_event("started", 0, 0)
    svc.set_target(7)
    assert svc.current()["targetReps"] == 7


def test_current_falls_back_to_live_target_once_the_edit_is_acked(clock):
    svc = make_service(clock)
    svc.set_target(11)
    assert svc.current()["targetReps"] == 11
    svc.handle_heartbeat("idle", 0, 0, target=11)
    # no longer outstanding -- current() now reports it via live_target
    assert svc.current()["targetReps"] == 11
    assert svc.fetch_target() is None


def test_default_device_backcompat(clock):
    svc = SessionService(now=clock)
    svc.queue_start()  # no device_id
    assert svc.fetch_command() == "start"
    assert svc.device is svc._device(DEFAULT_DEVICE_ID)
