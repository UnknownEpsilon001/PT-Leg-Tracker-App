from app import db


def test_create_and_get_open_session():
    session_id = db.create_session("2026-07-13T10:00:00+00:00", "app")
    row = db.get_open_session()
    assert row is not None
    assert row["id"] == session_id
    assert row["origin"] == "app"
    assert row["ended_at"] is None
    assert row["patient_code"] is None


def test_close_session_clears_open():
    session_id = db.create_session("2026-07-13T10:00:00+00:00", "device")
    db.close_session(session_id, "2026-07-13T10:05:00+00:00", 300, 42)
    assert db.get_open_session() is None


def test_claim_session():
    session_id = db.create_session("2026-07-13T10:00:00+00:00", "app")
    assert db.claim_session(session_id, "PT001") is True
    assert db.claim_session("no-such-id", "PT001") is False


def test_claim_is_idempotent_and_overwrites():
    session_id = db.create_session("2026-07-13T10:00:00+00:00", "app")
    db.claim_session(session_id, "PT001")
    assert db.claim_session(session_id, "PT002") is True
    # Verify the overwrite actually happened
    db.close_session(session_id, "2026-07-13T10:05:00+00:00", 300, 42)
    assert [r["id"] for r in db.list_sessions("PT002")] == [session_id]
    assert db.list_sessions("PT001") == []


def test_list_sessions_finished_only_for_patient():
    # Create two finished sessions for PT001 out of chronological order
    second_finished = db.create_session("2026-07-13T11:00:00+00:00", "app")
    db.close_session(second_finished, "2026-07-13T11:05:00+00:00", 250, 15)
    db.claim_session(second_finished, "PT001")

    first_finished = db.create_session("2026-07-13T10:00:00+00:00", "app")
    db.close_session(first_finished, "2026-07-13T10:05:00+00:00", 300, 10)
    db.claim_session(first_finished, "PT001")

    still_open = db.create_session("2026-07-13T12:00:00+00:00", "device")
    db.claim_session(still_open, "PT001")

    other = db.create_session("2026-07-13T09:00:00+00:00", "app")
    db.close_session(other, "2026-07-13T09:05:00+00:00", 300, 5)
    db.claim_session(other, "PT002")

    rows = db.list_sessions("PT001")
    # Verify only finished sessions returned and ordered by started_at (oldest first)
    assert [r["id"] for r in rows] == [first_finished, second_finished]
    assert rows[0]["duration_sec"] == 300
    assert rows[0]["reps"] == 10
    assert rows[1]["duration_sec"] == 250
    assert rows[1]["reps"] == 15
