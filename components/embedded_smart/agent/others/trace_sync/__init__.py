"""LiteCrab trace_sync: SSH-pulled live mirror of board-side agent_trace_*.jsonl.

Connects to the 5091 board over SSH/SFTP, periodically syncs new bytes of
``agent_trace_*.jsonl`` into a local cache directory, and exposes a small
HTTP API + serves the trace_viewer static files so a browser opened at
``http://localhost:<port>/`` can run in Live mode and refresh every second.
"""

from .config import TraceSyncConfig, load_config
from .service import TraceSyncService

__all__ = ["TraceSyncConfig", "TraceSyncService", "load_config"]
