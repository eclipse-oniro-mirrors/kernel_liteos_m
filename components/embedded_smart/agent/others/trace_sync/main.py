from __future__ import annotations

import argparse
import signal
import threading

from .config import load_config
from .service import TraceSyncService


def main() -> int:
    parser = argparse.ArgumentParser(description="LiteCrab trace_sync: SSH-pulled live mirror of board-side traces")
    parser.add_argument("--config", required=True)
    parser.add_argument("--verbose", action="store_true", help="enable debug logging")
    args = parser.parse_args()
    config = load_config(args.config)
    if args.verbose:
        from .config import TraceSyncConfig
        config = TraceSyncConfig(**{**config.__dict__, "verbose": True})
    service = TraceSyncService(config)
    service.configure_logging()
    service.start()
    stopped = threading.Event()

    def shutdown(_signum: int, _frame: object) -> None:
        stopped.set()

    signal.signal(signal.SIGINT, shutdown)
    signal.signal(signal.SIGTERM, shutdown)
    print(f"LiteCrab trace_sync: http://localhost:{config.http_port}/  (viewer)")
    print(f"                    ssh {config.ssh_user}@{config.ssh_host}:{config.ssh_port} -> {config.remote_log_dir}")
    while not stopped.wait(1.0):
        if service.healthy():
            continue
        service.logger.error("trace_sync service unhealthy; restarting")
        service.stop()
        service.start()
    service.stop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
