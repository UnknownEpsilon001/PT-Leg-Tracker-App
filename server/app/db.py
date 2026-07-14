"""SQLite storage for therapy sessions. One table, stdlib sqlite3, no ORM."""

import os
import sqlite3
import uuid
from contextlib import contextmanager

DB_PATH_ENV = "PTLT_DB"
DEFAULT_DB_PATH = "ptlt.sqlite3"
DEVICE_ID = "device-1"

_SCHEMA = """
CREATE TABLE IF NOT EXISTS sessions (
  id TEXT PRIMARY KEY,
  device_id TEXT NOT NULL,
  started_at TEXT NOT NULL,
  ended_at TEXT,
  duration_sec INTEGER,
  reps INTEGER NOT NULL DEFAULT 0,
  patient_code TEXT,
  origin TEXT NOT NULL CHECK (origin IN ('app', 'device'))
)
"""


@contextmanager
def _db():
    conn = sqlite3.connect(os.environ.get(DB_PATH_ENV, DEFAULT_DB_PATH))
    conn.row_factory = sqlite3.Row
    try:
        with conn:
            conn.execute(_SCHEMA)
            yield conn
    finally:
        conn.close()


def create_session(started_at: str, origin: str) -> str:
    session_id = str(uuid.uuid4())
    with _db() as conn:
        conn.execute(
            "INSERT INTO sessions (id, device_id, started_at, origin) VALUES (?, ?, ?, ?)",
            (session_id, DEVICE_ID, started_at, origin),
        )
    return session_id


def close_session(session_id: str, ended_at: str, duration_sec: int, reps: int) -> None:
    with _db() as conn:
        conn.execute(
            "UPDATE sessions SET ended_at = ?, duration_sec = ?, reps = ? WHERE id = ?",
            (ended_at, duration_sec, reps, session_id),
        )


def get_open_session() -> sqlite3.Row | None:
    with _db() as conn:
        return conn.execute(
            "SELECT * FROM sessions WHERE ended_at IS NULL ORDER BY started_at DESC LIMIT 1"
        ).fetchone()


def claim_session(session_id: str, patient_code: str) -> bool:
    with _db() as conn:
        cur = conn.execute(
            "UPDATE sessions SET patient_code = ? WHERE id = ?",
            (patient_code, session_id),
        )
        return cur.rowcount == 1


def list_sessions(patient_code: str) -> list[sqlite3.Row]:
    with _db() as conn:
        return conn.execute(
            "SELECT * FROM sessions"
            " WHERE patient_code = ? AND ended_at IS NOT NULL"
            " ORDER BY started_at",
            (patient_code,),
        ).fetchall()
