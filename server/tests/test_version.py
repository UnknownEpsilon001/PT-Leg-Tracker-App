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


def test_favicon_is_empty_not_404():
    assert TestClient(create_app()).get("/favicon.ico").status_code == 204


def test_double_slash_from_old_firmware_still_routes():
    client = TestClient(create_app())
    assert client.get("http://testserver//api/device/command?deviceId=default").status_code == 200
    assert client.get("/api//sessions/current").status_code == 200
