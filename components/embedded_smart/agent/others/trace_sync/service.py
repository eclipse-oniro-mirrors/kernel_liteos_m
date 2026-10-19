from __future__ import annotations

from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import logging
from logging.handlers import RotatingFileHandler
import os
from pathlib import Path
import threading
import time
from typing import Optional
from urllib.parse import parse_qs, urlparse

try:
    import paramiko
    _PARAMIKO_AVAILABLE = True
except ImportError:  # pragma: no cover
    paramiko = None
    _PARAMIKO_AVAILABLE = False

from .config import TraceSyncConfig


class TraceSyncService:
    """Pulls agent_trace_*.jsonl from the board via SFTP and serves them over HTTP."""

    def __init__(self, config: TraceSyncConfig):
        config.validate()
        if not _PARAMIKO_AVAILABLE:
            raise RuntimeError(
                "paramiko is not installed; run "
                "`python -m pip install paramiko` and retry"
            )
        self.config = config
        self.started_at = time.monotonic()
        self._http: Optional[ThreadingHTTPServer] = None
        self._sync_thread: Optional[threading.Thread] = None
        self._stop = threading.Event()
        # Sync state guarded by _mu:
        #   files[name] = {"size": int, "mtime": int}
        self._mu = threading.Lock()
        self._files: dict[str, dict] = {}
        self._ssh_state = {"connected": False, "last_error": "", "last_sync_ms": 0}
        self.logger = logging.getLogger(f"litecrab.trace_sync.{id(self)}")
        self.logger.setLevel(logging.INFO)
        self.logger.propagate = False

    # ------------------------------------------------------------------ logging

    def configure_logging(self) -> None:
        directory = Path(self.config.log_dir)
        directory.mkdir(parents=True, exist_ok=True)
        fmt = logging.Formatter("%(asctime)s %(levelname)s %(message)s")
        file_handler = RotatingFileHandler(
            directory / "trace_sync.log",
            maxBytes=2 * 1024 * 1024,
            backupCount=3,
            encoding="utf-8",
        )
        file_handler.setFormatter(fmt)
        self.logger.addHandler(file_handler)
        console = logging.StreamHandler()
        console.setFormatter(fmt)
        self.logger.addHandler(console)
        self.logger.setLevel(logging.DEBUG if self.config.verbose else logging.INFO)

    # ------------------------------------------------------------------ start/stop

    def start(self) -> None:
        Path(self.config.cache_dir).mkdir(parents=True, exist_ok=True)
        # Seed local index from whatever is already on disk so the HTTP API
        # returns the existing files immediately, before the first sync.
        self._scan_local_cache()

        station = self

        class Handler(BaseHTTPRequestHandler):
            server_version = "LiteCrabTraceSync/1.0"

            def log_message(self, fmt: str, *args: object) -> None:
                station.logger.info("http " + fmt, *args)

            def _cors(self) -> None:
                self.send_header("Access-Control-Allow-Origin", "*")
                self.send_header("Access-Control-Allow-Headers", "Content-Type")
                self.send_header("Access-Control-Allow-Methods", "GET, OPTIONS")

            def _send_bytes(self, status: int, body: bytes, content_type: str = "text/plain; charset=utf-8") -> None:
                self.send_response(status)
                self.send_header("Content-Type", content_type)
                self.send_header("Content-Length", str(len(body)))
                self._cors()
                self.end_headers()
                self.wfile.write(body)

            def _send_json(self, status: int, value: object) -> None:
                body = json.dumps(value, ensure_ascii=False).encode("utf-8")
                self._send_bytes(status, body, "application/json; charset=utf-8")

            def do_OPTIONS(self) -> None:
                self.send_response(204)
                self._cors()
                self.end_headers()

            def do_GET(self) -> None:
                parsed = urlparse(self.path)
                path = parsed.path
                params = {k: v[0] for k, v in parse_qs(parsed.query).items()}

                if path == "/api/health":
                    self._send_json(200, {"status": "ok", "uptime_seconds": round(time.monotonic() - station.started_at, 3)})
                    return
                if path == "/api/status":
                    with station._mu:
                        ssh_state = dict(station._ssh_state)
                        files = [
                            {"name": n, "size": f["size"], "mtime": f["mtime"]}
                            for n, f in sorted(station._files.items())
                        ]
                    self._send_json(200, {
                        "status": "running",
                        "ssh": ssh_state,
                        "files": files,
                        "remote_log_dir": station.config.remote_log_dir,
                        "poll_interval": station.config.poll_interval,
                    })
                    return
                if path == "/api/traces/list":
                    with station._mu:
                        files = [
                            {"name": n, "size": f["size"], "mtime": f["mtime"]}
                            for n, f in sorted(station._files.items())
                        ]
                    self._send_json(200, {"files": files})
                    return
                if path == "/api/traces/tail":
                    name = params.get("name", "")
                    if not name or "/" in name or "\\" in name or ".." in name:
                        self._send_json(400, {"error": "invalid name"})
                        return
                    offset = 0
                    try:
                        offset = int(params.get("offset", "0"))
                        if offset < 0:
                            raise ValueError
                    except ValueError:
                        self._send_json(400, {"error": "invalid offset"})
                        return
                    local_path = Path(station.config.cache_dir) / name
                    if not local_path.is_file():
                        self._send_json(404, {"error": "file not found"})
                        return
                    size = local_path.stat().st_size
                    if offset > size:
                        # Viewer asked past EOF; signal so it can reset to 0
                        # (e.g. after a file rotation).
                        self._send_json(416, {"error": "offset beyond size", "size": size})
                        return
                    with local_path.open("rb") as fp:
                        fp.seek(offset)
                        chunk = fp.read()
                    self._send_bytes(200, chunk, "application/jsonl; charset=utf-8")
                    return
                if path == "/api/traces/file":
                    name = params.get("name", "")
                    if not name or "/" in name or "\\" in name or ".." in name:
                        self._send_json(400, {"error": "invalid name"})
                        return
                    local_path = Path(station.config.cache_dir) / name
                    if not local_path.is_file():
                        self._send_json(404, {"error": "file not found"})
                        return
                    data = local_path.read_bytes()
                    self._send_bytes(200, data, "application/jsonl; charset=utf-8")
                    return

                # Static file serving (the trace_viewer itself) so Live mode
                # works from http://localhost:<port>/ without CORS pain.
                if path == "/" or path == "/index.html":
                    self._serve_static("index.html", "text/html; charset=utf-8")
                    return
                if path in ("/viewer.js", "/viewer.css", "/trace_model.js"):
                    ctype = {
                        "/viewer.js": "application/javascript; charset=utf-8",
                        "/viewer.css": "text/css; charset=utf-8",
                        "/trace_model.js": "application/javascript; charset=utf-8",
                    }[path]
                    self._serve_static(path.lstrip("/"), ctype)
                    return
                self._send_json(404, {"error": "not found"})

            def _serve_static(self, relative: str, content_type: str) -> None:
                root = Path(station.config.viewer_dir).resolve()
                candidate = (root / relative).resolve()
                try:
                    candidate.relative_to(root)
                except ValueError:
                    self._send_json(400, {"error": "bad path"})
                    return
                if not candidate.is_file():
                    self._send_json(404, {"error": "not found"})
                    return
                data = candidate.read_bytes()
                self.send_response(200)
                self.send_header("Content-Type", content_type)
                self.send_header("Content-Length", str(len(data)))
                self._cors()
                self.end_headers()
                self.wfile.write(data)

        self._http = ThreadingHTTPServer((self.config.listen_host, self.config.http_port), Handler)
        thread = threading.Thread(target=self._http.serve_forever, name="litecrab-trace_sync-http", daemon=True)
        thread.start()

        self._stop.clear()
        self._sync_thread = threading.Thread(target=self._sync_loop, name="litecrab-trace_sync-ssh", daemon=True)
        self._sync_thread.start()

        self.logger.info(
            "trace_sync started http=http://%s:%d ssh=%s@%s:%s remote=%s cache=%s viewer=%s poll=%.2fs",
            self.config.listen_host,
            self.config.http_port,
            self.config.ssh_user,
            self.config.ssh_host,
            self.config.ssh_port,
            self.config.remote_log_dir,
            os.path.abspath(self.config.cache_dir),
            os.path.abspath(self.config.viewer_dir),
            self.config.poll_interval,
        )

    def stop(self) -> None:
        self._stop.set()
        if self._http:
            self._http.shutdown()
            self._http.server_close()
            self._http = None
        if self._sync_thread:
            self._sync_thread.join(timeout=5)
            self._sync_thread = None

    def healthy(self) -> bool:
        return self._http is not None and self._sync_thread is not None and self._sync_thread.is_alive()

    # ------------------------------------------------------------------ local cache scan

    def _scan_local_cache(self) -> None:
        cache = Path(self.config.cache_dir)
        cache.mkdir(parents=True, exist_ok=True)
        with self._mu:
            self._files.clear()
            for entry in cache.iterdir():
                if not entry.is_file():
                    continue
                if not entry.name.startswith("agent_trace_") or not entry.name.endswith(".jsonl"):
                    continue
                stat = entry.stat()
                self._files[entry.name] = {"size": stat.st_size, "mtime": int(stat.st_mtime)}

    # ------------------------------------------------------------------ ssh sync loop

    def _sync_loop(self) -> None:
        while not self._stop.is_set():
            try:
                self._sync_once()
                with self._mu:
                    self._ssh_state["connected"] = True
                    self._ssh_state["last_error"] = ""
                    self._ssh_state["last_sync_ms"] = int(time.time() * 1000)
            except Exception as exc:
                with self._mu:
                    self._ssh_state["connected"] = False
                    self._ssh_state["last_error"] = f"{type(exc).__name__}: {exc}"
                self.logger.warning("sync failed: %s", exc)
            # Wait poll_interval, but wake up early on stop.
            self._stop.wait(max(0.2, self.config.poll_interval))
            if self._stop.is_set():
                break

    def _sync_once(self) -> None:
        """Open one SSH/SFTP session, list remote files, download deltas, close."""
        client = paramiko.SSHClient()
        client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
        client.connect(
            hostname=self.config.ssh_host,
            port=self.config.ssh_port,
            username=self.config.ssh_user,
            password=self.config.ssh_password,
            timeout=self.config.connect_timeout,
            allow_agent=False,
            look_for_keys=False,
        )
        try:
            sftp = client.open_sftp()
            try:
                remote_entries = self._list_remote(sftp)
                self._apply_deltas(sftp, remote_entries)
                self._prune_local(remote_entries)
            finally:
                sftp.close()
        finally:
            client.close()

    def _list_remote(self, sftp) -> list[dict]:
        out: list[dict] = []
        try:
            for entry in sftp.listdir_attr(self.config.remote_log_dir):
                if not entry.filename.startswith("agent_trace_"):
                    continue
                if not entry.filename.endswith(".jsonl"):
                    continue
                out.append({
                    "name": entry.filename,
                    "size": entry.st_size,
                    "mtime": int(entry.st_mtime),
                })
        except FileNotFoundError:
            self.logger.warning("remote log dir does not exist: %s", self.config.remote_log_dir)
        except PermissionError as exc:
            self.logger.warning("cannot list remote dir: %s", exc)
        return out

    def _apply_deltas(self, sftp, remote_entries: list[dict]) -> None:
        cache = Path(self.config.cache_dir)
        cache.mkdir(parents=True, exist_ok=True)
        for r in remote_entries:
            name = r["name"]
            local_path = cache / name
            with self._mu:
                local_meta = self._files.get(name)
            if local_meta is None:
                # Not present locally: download full.
                self._download_full(sftp, name, local_path, r)
            elif r["size"] > local_meta["size"]:
                # Remote grew: append bytes [local_size:remote_size].
                self._download_tail(sftp, name, local_path, local_meta["size"], r["size"])
            elif r["size"] < local_meta["size"]:
                # Remote smaller (likely rotated/truncated): re-download.
                self._download_full(sftp, name, local_path, r)
            else:
                # Same size: update mtime only, nothing to fetch.
                with self._mu:
                    self._files[name] = {"size": r["size"], "mtime": r["mtime"]}

    def _download_full(self, sftp, name: str, local_path: Path, remote: dict) -> None:
        remote_path = f"{self.config.remote_log_dir}/{name}"
        # paramiko's get() does not resume; write to a .part then rename so
        # half-fetched files never become visible to the HTTP API.
        tmp = local_path.with_suffix(local_path.suffix + ".part")
        sftp.get(remote_path, str(tmp))
        os.replace(tmp, local_path)
        with self._mu:
            self._files[name] = {"size": remote["size"], "mtime": remote["mtime"]}
        self.logger.info("downloaded %s (%d bytes)", name, remote["size"])

    def _download_tail(self, sftp, name: str, local_path: Path, local_size: int, remote_size: int) -> None:
        remote_path = f"{self.config.remote_log_dir}/{name}"
        want = remote_size - local_size
        if want <= 0:
            return
        with sftp.open(remote_path, "rb") as rf:
            rf.seek(local_size)
            # Read in chunks so very large deltas don't blow memory.
            with local_path.open("ab") as lf:
                remaining = want
                while remaining > 0:
                    chunk = rf.read(min(65536, remaining))
                    if not chunk:
                        break
                    lf.write(chunk)
                    remaining -= len(chunk)
        with self._mu:
            self._files[name] = {"size": remote_size, "mtime": int(time.time())}
        self.logger.info("appended %d bytes to %s (now %d)", want, name, remote_size)

    def _prune_local(self, remote_entries: list[dict]) -> None:
        """Remove local files that no longer exist remotely, keeping at most
        keep_generations of the newest files (mirroring board-side rotation).
        """
        remote_names = {r["name"] for r in remote_entries}
        cache = Path(self.config.cache_dir)
        # Sort local newest-first by mtime so we keep the freshest ones.
        local_sorted = sorted(
            (e for e in cache.iterdir()
             if e.is_file() and e.name.startswith("agent_trace_") and e.name.endswith(".jsonl")),
            key=lambda p: p.stat().st_mtime,
            reverse=True,
        )
        for idx, entry in enumerate(local_sorted):
            keep = entry.name in remote_names or idx < self.config.keep_generations
            if not keep:
                try:
                    entry.unlink()
                except OSError:
                    pass
                with self._mu:
                    self._files.pop(entry.name, None)
                self.logger.info("pruned old local file %s", entry.name)
