# [TH] fixture ร่วมของชุดทดสอบ: แยก DB ทดสอบออกจาก DB จริงด้วย tmp_path + monkeypatch
# Shared test fixtures: isolate the test DB from the real one via tmp_path + monkeypatch
import pytest


@pytest.fixture(autouse=True)
def isolated_db(tmp_path, monkeypatch):
    # [TH] บังคับทุกเทสให้ใช้ไฟล์ sqlite ชั่วคราวแยกต่างหาก ป้องกันชนกับ DB จริง
    monkeypatch.setenv("PTLT_DB", str(tmp_path / "test.sqlite3"))


class FakeClock:
    def __init__(self, start=1000.0):
        self.t = start

    def __call__(self):
        return self.t

    def advance(self, seconds):
        self.t += seconds


@pytest.fixture
def clock():
    return FakeClock()
