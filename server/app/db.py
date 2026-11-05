"""SQLite storage for therapy sessions. One table, stdlib sqlite3, no ORM.

[TH] ที่เก็บข้อมูลเซสชันกายภาพบำบัดด้วย SQLite ตารางเดียว ใช้ sqlite3 มาตรฐาน ไม่มี ORM
     เซสชันที่ยังไม่จบคือแถวที่ ended_at เป็น NULL
"""

import os
import sqlite3
import uuid
from contextlib import contextmanager

DB_PATH_ENV = "PTLT_DB"
DEFAULT_DB_PATH = "ptlt.sqlite3"
DEFAULT_DEVICE_ID = "default"

_SCHEMA = """
CREATE TABLE IF NOT EXISTS sessions (
  id TEXT PRIMARY KEY,
  device_id TEXT NOT NULL,
  started_at TEXT NOT NULL,
  ended_at TEXT,
  duration_sec INTEGER,
  reps INTEGER NOT NULL DEFAULT 0,
  patient_code TEXT,
  origin TEXT NOT NULL CHECK (origin IN ('app', 'device')),
  uid TEXT,
  fault INTEGER NOT NULL DEFAULT 0
)
"""

# Columns added after the first release, so a database created by an older server
# gains them in place instead of needing to be thrown away.
# [TH] คอลัมน์ที่เพิ่มหลังรุ่นแรก ฐานข้อมูลที่สร้างจากเซิร์ฟเวอร์รุ่นเก่าจะได้คอลัมน์เหล่านี้เพิ่มเข้าไปเอง ไม่ต้องทิ้งไฟล์
_ADDED_COLUMNS = {"uid": "TEXT", "fault": "INTEGER NOT NULL DEFAULT 0"}

# The controller numbers every session it runs (uid), and sends that number with
# the live started event and again with the SD-card row it uploads later. One
# row per (device, uid) is what makes the upload safe to repeat.
# [TH] คอนโทรลเลอร์ให้หมายเลขกับทุกรอบที่ทำงาน (uid) และส่งหมายเลขนี้ทั้งกับเหตุการณ์ started แบบสด
#      และกับแถวจากการ์ด SD ที่อัปโหลดทีหลัง การมีแถวเดียวต่อ (อุปกรณ์, uid) ทำให้ส่งซ้ำได้อย่างปลอดภัย
_UID_INDEX = (
    "CREATE UNIQUE INDEX IF NOT EXISTS sessions_device_uid"
    " ON sessions (device_id, uid) WHERE uid IS NOT NULL"
)


# Database files whose schema this process has already brought up to date. The
# check runs once per file rather than on every query: heartbeats arrive every
# few seconds per device. Keyed by path because the tests point PTLT_DB at a
# fresh file each time.
# [TH] ไฟล์ฐานข้อมูลที่โปรเซสนี้ปรับโครงสร้างให้ล่าสุดแล้ว ตรวจครั้งเดียวต่อไฟล์ ไม่ใช่ทุกคำสั่ง
#      เพราะสัญญาณชีพมาทุกไม่กี่วินาทีต่ออุปกรณ์ ใช้พาธเป็นคีย์เพราะชุดทดสอบชี้ PTLT_DB ไปที่ไฟล์ใหม่ทุกครั้ง
_ready: set[str] = set()


def _ensure_schema(conn: sqlite3.Connection) -> None:
    conn.execute(_SCHEMA)
    have = {r["name"] for r in conn.execute("PRAGMA table_info(sessions)")}
    for name, decl in _ADDED_COLUMNS.items():
        if name not in have:
            conn.execute(f"ALTER TABLE sessions ADD COLUMN {name} {decl}")
    conn.execute(_UID_INDEX)


@contextmanager
def _db():
    path = os.environ.get(DB_PATH_ENV, DEFAULT_DB_PATH)
    conn = sqlite3.connect(path)
    conn.row_factory = sqlite3.Row
    try:
        with conn:
            if path not in _ready:
                _ensure_schema(conn)
                _ready.add(path)
            yield conn
    finally:
        conn.close()


def create_session(
    started_at: str,
    origin: str,
    device_id: str = DEFAULT_DEVICE_ID,
    uid: str | None = None,
) -> str:
    session_id = str(uuid.uuid4())
    with _db() as conn:
        conn.execute(
            "INSERT INTO sessions (id, device_id, started_at, origin, uid)"
            " VALUES (?, ?, ?, ?, ?)",
            (session_id, device_id, started_at, origin, uid),
        )
    return session_id


def get_session_by_uid(device_id: str, uid: str) -> sqlite3.Row | None:
    with _db() as conn:
        return conn.execute(
            "SELECT * FROM sessions WHERE device_id = ? AND uid = ?", (device_id, uid)
        ).fetchone()


def record_device_session(
    device_id: str,
    uid: str,
    started_at: str,
    ended_at: str,
    duration_sec: int,
    reps: int,
    fault: bool,
    patient_code: str | None,
) -> str:
    """A finished session from the device's SD card, inserted or merged by uid.

    If the live events already created the row, the card's numbers close it if it
    is still open, and its patient code fills the row only when the app has not
    claimed it: a claim made in the app is the stronger statement of who it was.

    [TH] รอบที่จบแล้วจากการ์ด SD ของอุปกรณ์ เพิ่มใหม่หรือรวมกับแถวเดิมตาม uid
         ถ้าเหตุการณ์สดสร้างแถวไว้แล้ว ตัวเลขจากการ์ดจะปิดแถวนั้นถ้ายังเปิดอยู่
         และรหัสผู้ป่วยจะใส่ให้เฉพาะเมื่อแอปยังไม่ได้ระบุ เพราะการระบุในแอปเชื่อถือได้มากกว่า
    """
    with _db() as conn:
        # INSERT OR IGNORE, then read back: two requests with the same uid at once
        # (a retry racing the original, or the live started event) both reach this
        # point, and a plain INSERT made the second one an IntegrityError and a 500.
        # The unique index decides which one inserts; the other merges below.
        # [TH] ใช้ INSERT OR IGNORE แล้วอ่านกลับ เพราะสองคำขอที่มี uid เดียวกันพร้อมกัน
        #      (การส่งซ้ำที่แข่งกับครั้งแรก หรือกับเหตุการณ์ started แบบสด) มาถึงจุดนี้ทั้งคู่
        #      INSERT ธรรมดาทำให้คำขอที่สองเกิด IntegrityError และตอบ 500 ดัชนีที่ไม่ซ้ำเป็นตัวตัดสินว่าใครได้เพิ่ม
        #      อีกคำขอหนึ่งจะไปรวมแถวด้านล่าง
        session_id = str(uuid.uuid4())
        cur = conn.execute(
            "INSERT OR IGNORE INTO sessions (id, device_id, started_at, ended_at,"
            " duration_sec, reps, patient_code, origin, uid, fault)"
            " VALUES (?, ?, ?, ?, ?, ?, ?, 'device', ?, ?)",
            (session_id, device_id, started_at, ended_at, duration_sec, reps,
             patient_code, uid, int(fault)),
        )
        if cur.rowcount == 1:
            return session_id
        row = conn.execute(
            "SELECT id, ended_at FROM sessions WHERE device_id = ? AND uid = ?",
            (device_id, uid),
        ).fetchone()
        if row["ended_at"] is None:
            conn.execute(
                "UPDATE sessions SET ended_at = ?, duration_sec = ?, reps = ? WHERE id = ?",
                (ended_at, duration_sec, reps, row["id"]),
            )
        conn.execute(
            "UPDATE sessions SET fault = ?, patient_code = COALESCE(patient_code, ?)"
            " WHERE id = ?",
            (int(fault), patient_code, row["id"]),
        )
        return row["id"]


def close_session(session_id: str, ended_at: str, duration_sec: int, reps: int) -> None:
    with _db() as conn:
        conn.execute(
            "UPDATE sessions SET ended_at = ?, duration_sec = ?, reps = ? WHERE id = ?",
            (ended_at, duration_sec, reps, session_id),
        )


def get_open_session(device_id: str = DEFAULT_DEVICE_ID) -> sqlite3.Row | None:
    # [TH] คืนเซสชันที่ยังเปิดอยู่ล่าสุดของอุปกรณ์นี้ (ended_at ยังเป็น NULL)
    with _db() as conn:
        return conn.execute(
            "SELECT * FROM sessions"
            " WHERE ended_at IS NULL AND device_id = ?"
            " ORDER BY started_at DESC LIMIT 1",
            (device_id,),
        ).fetchone()


def claim_session(session_id: str, patient_code: str) -> bool:
    # [TH] ผูกเซสชันกับรหัสผู้ป่วย (แอปเรียกเพื่อระบุว่าเซสชันนี้เป็นของใคร)
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
