"""In-memory device-facing state: one-slot command queue + liveness tracking."""

import time

COMMAND_TTL_SEC = 30
DEVICE_OFFLINE_AFTER_SEC = 10


class DeviceState:
    def __init__(self, now=time.time):
        self._now = now
        self._pending_command: str | None = None
        self._queued_at = 0.0
        self._start_command_fetched = False
        self._start_fetched_at = 0.0
        self._last_heartbeat_at: float | None = None
        self.live_state = "idle"
        self.live_elapsed_sec = 0
        self.live_reps = 0

    def queue_command(self, command: str) -> None:
        self._pending_command = command
        self._queued_at = self._now()

    def pending_command(self) -> str | None:
        """Pending command, dropping it first if older than COMMAND_TTL_SEC."""
        if (
            self._pending_command is not None
            and self._now() - self._queued_at > COMMAND_TTL_SEC
        ):
            self._pending_command = None
        return self._pending_command

    def fetch_command(self) -> str | None:
        """Consume-on-read: hand the command to the device and clear the slot."""
        command = self.pending_command()
        self._pending_command = None
        if command == "start":
            self._start_command_fetched = True
            self._start_fetched_at = self._now()
        elif command is not None:
            self._start_command_fetched = False
        return command

    def consume_start_origin(self) -> bool:
        """True exactly once if the upcoming 'started' event follows an app command."""
        fetched = self._start_command_fetched
        self._start_command_fetched = False
        return fetched

    def start_in_flight(self) -> bool:
        """True while a fetched start awaits its 'started' confirmation (bounded by COMMAND_TTL_SEC)."""
        return (
            self._start_command_fetched
            and self._now() - self._start_fetched_at <= COMMAND_TTL_SEC
        )

    def heartbeat(self, state: str, elapsed_sec: int, reps: int) -> None:
        self._last_heartbeat_at = self._now()
        self.live_state = state
        self.live_elapsed_sec = elapsed_sec
        self.live_reps = reps

    def device_online(self) -> bool:
        if self._last_heartbeat_at is None:
            return False
        return self._now() - self._last_heartbeat_at <= DEVICE_OFFLINE_AFTER_SEC

    def seconds_since_heartbeat(self) -> float | None:
        if self._last_heartbeat_at is None:
            return None
        return self._now() - self._last_heartbeat_at
