# [TH] เทส DeviceState ล้วนๆ: คิวคำสั่งหมดอายุ (TTL), แฟลก in-flight, และตรวจว่าอุปกรณ์ออนไลน์อยู่ไหม
# Pure DeviceState tests: command TTL expiry, the in-flight flag, and device-online tracking
from app.state import COMMAND_TTL_SEC, DEVICE_OFFLINE_AFTER_SEC, DeviceState


def test_fetch_repeats_command_until_acknowledged(clock):
    state = DeviceState(now=clock)
    state.queue_command("start")
    assert state.fetch_command() == "start"
    assert state.fetch_command() == "start"
    state.clear_pending_command("start")
    assert state.fetch_command() is None


def test_command_expires_after_ttl(clock):
    state = DeviceState(now=clock)
    state.queue_command("start")
    clock.advance(COMMAND_TTL_SEC + 1)
    assert state.fetch_command() is None


def test_pending_command_does_not_consume(clock):
    state = DeviceState(now=clock)
    state.queue_command("start")
    assert state.pending_command() == "start"
    assert state.pending_command() == "start"
    assert state.fetch_command() == "start"


def test_fetched_start_sets_origin_once(clock):
    state = DeviceState(now=clock)
    state.queue_command("start")
    state.fetch_command()
    assert state.consume_start_origin() is True
    assert state.consume_start_origin() is False


def test_stop_command_does_not_set_start_origin(clock):
    state = DeviceState(now=clock)
    state.queue_command("stop")
    state.fetch_command()
    assert state.consume_start_origin() is False


def test_later_stop_fetch_clears_start_origin(clock):
    state = DeviceState(now=clock)
    state.queue_command("start")
    state.fetch_command()
    state.queue_command("stop")
    state.fetch_command()
    assert state.consume_start_origin() is False


def test_start_in_flight_tracks_fetch_and_expiry(clock):
    state = DeviceState(now=clock)
    assert state.start_in_flight() is False
    state.queue_command("start")
    state.fetch_command()
    assert state.start_in_flight() is True
    clock.advance(COMMAND_TTL_SEC + 1)
    assert state.start_in_flight() is False


def test_clear_pending_start_drops_only_start(clock):
    state = DeviceState(now=clock)
    state.queue_command("start")
    state.clear_pending_start()
    assert state.pending_command() is None


def test_clear_pending_start_leaves_other_commands(clock):
    state = DeviceState(now=clock)
    state.queue_command("stop")
    state.clear_pending_start()
    assert state.pending_command() == "stop"


def test_clear_start_in_flight_resets_flag(clock):
    state = DeviceState(now=clock)
    state.queue_command("start")
    state.fetch_command()
    assert state.start_in_flight() is True
    state.clear_start_in_flight()
    assert state.start_in_flight() is False


def test_desired_target_is_none_until_set():
    dev = DeviceState()
    assert dev.fetch_desired_target() is None


def test_desired_target_is_handed_out_until_the_device_echoes_it():
    dev = DeviceState()
    dev.set_desired_target(7)
    # handed out on every poll: one lost response must not lose the target
    # [TH] ส่งให้ทุกครั้งที่ถาม เพราะถ้าคำตอบหายไปครั้งเดียวต้องไม่ทำให้เป้าหมายหายตาม
    assert dev.fetch_desired_target() == 7
    assert dev.fetch_desired_target() == 7
    dev.heartbeat("idle", 0, 0, target=7)
    assert dev.fetch_desired_target() is None
    assert dev.live_target == 7


def test_a_heartbeat_with_a_different_target_does_not_clear_the_desire():
    dev = DeviceState()
    dev.set_desired_target(7)
    dev.heartbeat("idle", 0, 0, target=3)
    assert dev.fetch_desired_target() == 7


def test_desired_target_expires_when_the_device_never_acks_it():
    # A device on a pre-target-echo image never reports the field back, so the
    # acknowledgement path can never fire; the clock is the only way out.
    # [TH] อุปกรณ์รุ่นก่อนที่ยังไม่ส่งเป้าหมายกลับมาจะยืนยันไม่ได้เลย การจับเวลาจึงเป็นทางออกทางเดียว
    clock = [1000.0]
    dev = DeviceState(now=lambda: clock[0])
    dev.set_desired_target(7)
    clock[0] += 10
    dev.heartbeat("idle", 0, 0)  # no target field at all — ไม่มีฟิลด์เป้าหมายเลย
    assert dev.fetch_desired_target() == 7
    clock[0] += 30
    assert dev.fetch_desired_target() is None


def test_desired_timings_are_handed_out_until_the_ttl_expires():
    clock = [1000.0]
    dev = DeviceState(now=lambda: clock[0])
    dev.set_desired_timings({"liftSec": 3, "lowerSec": 3, "holdSec": 8, "restSec": 8})
    assert dev.fetch_desired_timings()["liftSec"] == 3
    clock[0] += 10
    assert dev.fetch_desired_timings() is not None
    clock[0] += 30
    assert dev.fetch_desired_timings() is None


def test_heartbeat_omitting_target_does_not_ack_a_desired_zero():
    # A heartbeat that omits `target` entirely (an ESP32 still running a
    # pre-target-echo image) must not be read as acking a desired target of
    # exactly 0 -- an app user picking "unlimited" would have that request
    # silently discarded forever.
    dev = DeviceState()
    dev.set_desired_target(0)
    dev.heartbeat("idle", 0, 0)  # no target= passed, defaults to None
    assert dev.fetch_desired_target() == 0


def test_heartbeat_omitting_target_does_not_clobber_live_target():
    dev = DeviceState()
    dev.heartbeat("running", 10, 2, target=5)
    assert dev.live_target == 5
    dev.heartbeat("running", 11, 2)  # a later heartbeat that omits target
    assert dev.live_target == 5


def test_device_online_tracking(clock):
    state = DeviceState(now=clock)
    assert state.device_online() is False
    assert state.seconds_since_heartbeat() is None
    state.heartbeat("running", 12, 3)
    assert state.device_online() is True
    assert state.live_state == "running"
    assert state.live_elapsed_sec == 12
    assert state.live_reps == 3
    clock.advance(DEVICE_OFFLINE_AFTER_SEC + 1)
    assert state.device_online() is False
    assert state.seconds_since_heartbeat() == DEVICE_OFFLINE_AFTER_SEC + 1


def test_command_poll_keeps_device_online_without_heartbeats(clock):
    state = DeviceState(now=clock)
    state.heartbeat("running", 0, 0)
    clock.advance(DEVICE_OFFLINE_AFTER_SEC + 1)
    state.fetch_command()
    assert state.device_online()


def test_live_elapsed_counts_on_between_heartbeats_but_capped(clock):
    state = DeviceState(now=clock)
    state.heartbeat("running", 10, 2)
    clock.advance(2)
    assert state.live_elapsed_now() == 12
    clock.advance(60)
    assert state.live_elapsed_now() == 15  # capped: a silent device is not shown counting on
    state.heartbeat("idle", 20, 3)
    clock.advance(5)
    assert state.live_elapsed_now() == 20
