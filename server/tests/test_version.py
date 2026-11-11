# [TH] ตรวจว่า /api/version บอกรุ่นของเซิร์ฟเวอร์ และค่าจากตัวแปรสภาพแวดล้อมใช้แทน git ได้
# /api/version names the running build, and the environment can override git.
from fastapi.testclient import TestClient

from app.main import create_app


def test_version_names_the_build(monkeypatch):
    monkeypatch.setenv("BUILD_COMMIT", "abc1234")
    monkeypatch.setenv("BUILD_BRANCH", "main")
    body = TestClient(create_app()).get("/api/version").json()
    assert body["name"] == "server"
    assert body["commit"] == "abc1234"
    assert body["branch"] == "main"
