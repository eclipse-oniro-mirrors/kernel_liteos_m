from __future__ import annotations

from dataclasses import dataclass
import json
import os
from pathlib import Path


@dataclass(frozen=True)
class TraceSyncConfig:
    # HTTP listen
    listen_host: str = "0.0.0.0"
    http_port: int = 10005

    # SSH target
    ssh_host: str = "192.168.8.10"
    ssh_port: int = 22
    ssh_user: str = "enspire"
    ssh_password: str = ""

    # Remote log dir on the board (absolute path)
    remote_log_dir: str = "/mnt/home/enspire/z00840189/logs"

    # Local cache directory (where synced .jsonl files land)
    cache_dir: str = "cache"

    # Static viewer dir to serve at / (so Live mode works without CORS pain)
    viewer_dir: str = "viewer"

    # Polling interval (seconds)
    poll_interval: float = 1.0

    # Connect/retry tuning
    connect_timeout: float = 8.0
    reconnect_delay: float = 2.0

    # Trace file rotation: keep at most this many generations locally
    keep_generations: int = 8

    # Logging
    log_dir: str = "logs"
    verbose: bool = False

    def validate(self) -> None:
        if not 1 <= self.http_port <= 65535:
            raise ValueError("http_port must be between 1 and 65535")
        if not 1 <= self.ssh_port <= 65535:
            raise ValueError("ssh_port must be between 1 and 65535")
        if not self.ssh_host:
            raise ValueError("ssh_host is required")
        if not self.ssh_user:
            raise ValueError("ssh_user is required")
        if not self.ssh_password:
            raise ValueError("ssh_password is required (env LITECRAB_TRACE_SSH_PASSWORD can override)")
        if not self.remote_log_dir or not self.remote_log_dir.startswith("/"):
            raise ValueError("remote_log_dir must be an absolute POSIX path")
        if not 0.2 <= self.poll_interval <= 60:
            raise ValueError("poll_interval must be between 0.2 and 60 seconds")
        if not 1 <= self.keep_generations <= 64:
            raise ValueError("keep_generations must be between 1 and 64")


def _flatten(data: dict) -> dict:
    ssh = data.get("ssh", {})
    server = data.get("server", {})
    logging = data.get("log", data.get("logging", {}))
    return {
        "listen_host": server.get("listen_host", data.get("listen_host", "0.0.0.0")),
        "http_port": server.get("http_port", data.get("http_port", 10005)),
        "ssh_host": ssh.get("host", data.get("ssh_host", "192.168.8.10")),
        "ssh_port": ssh.get("port", data.get("ssh_port", 22)),
        "ssh_user": ssh.get("user", data.get("ssh_user", "enspire")),
        "ssh_password": ssh.get("password", data.get("ssh_password", "")),
        "remote_log_dir": ssh.get("remote_log_dir", data.get("remote_log_dir", "/mnt/home/enspire/z00840189/logs")),
        "cache_dir": data.get("cache_dir", "cache"),
        "viewer_dir": data.get("viewer_dir", "viewer"),
        "poll_interval": data.get("poll_interval", 1.0),
        "connect_timeout": ssh.get("connect_timeout", data.get("connect_timeout", 8.0)),
        "reconnect_delay": ssh.get("reconnect_delay", data.get("reconnect_delay", 2.0)),
        "keep_generations": data.get("keep_generations", 8),
        "log_dir": logging.get("dir", data.get("log_dir", "logs")),
        "verbose": data.get("verbose", False),
    }


def load_config(path: str | os.PathLike[str]) -> TraceSyncConfig:
    with Path(path).open("r", encoding="utf-8") as stream:
        raw = json.load(stream)
    if not isinstance(raw, dict):
        raise ValueError("configuration root must be an object")
    values = _flatten(raw)
    # Allow env overrides for the password and host (so config.json can be
    # committed without secrets if desired).
    overrides = {
        "ssh_password": os.getenv("LITECRAB_TRACE_SSH_PASSWORD"),
        "ssh_host": os.getenv("LITECRAB_TRACE_SSH_HOST"),
        "ssh_user": os.getenv("LITECRAB_TRACE_SSH_USER"),
    }
    values.update({k: v for k, v in overrides.items() if v})
    config = TraceSyncConfig(**values)
    config.validate()
    return config
