from app.state import COMMAND_TTL_SEC, DEVICE_OFFLINE_AFTER_SEC, DeviceState


def test_fetch_consumes_command(clock):
    state = DeviceState(now=clock)
    state.queue_command("start")
    assert state.fetch_command() == "start"
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
