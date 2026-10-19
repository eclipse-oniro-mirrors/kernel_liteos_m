#!/usr/bin/env python3
import json
from pathlib import Path
import subprocess
import sys
import tempfile


def base(device):
    return {
        "server": {"listen_ip": "127.0.0.1", "listen_port": 19099,
                   "worker_threads": 1, "max_connections": 1},
        "mcp": {"enabled": True, "devices": device if isinstance(device, list) else [device]},
    }


def rejected(binary, config, expected):
    with tempfile.TemporaryDirectory() as temporary:
        path = Path(temporary) / "config.json"
        path.write_text(json.dumps(config), encoding="utf-8")
        result = subprocess.run([binary, "--config", str(path), "--no-alarm"],
                                text=True, capture_output=True, timeout=5)
    assert result.returncode == 2, (result.returncode, result.stderr)
    assert expected in result.stderr, result.stderr


def main():
    binary = sys.argv[1]
    valid = {"id": "dev-1", "alias": "lab", "transport": "streamable-http",
             "url": "http://127.0.0.1:9/mcp"}
    rejected(binary,
             base({**valid, "url": "http://127.0.0.1.evil.example/mcp"}),
             "invalid or insecure MCP device URL")
    rejected(binary,
             base([{**valid}, {**valid, "id": "dev-2"}]),
             "duplicate MCP device id or alias")
    rejected(binary,
             base({**valid, "token_env": "BAD ENV"}),
             "invalid MCP token environment name")
    rejected(binary,
             base({**valid, "token_env": "9_BAD"}),
             "invalid MCP token environment name")
    rejected(binary,
             base({**valid, "ca_file": 42}),
             "invalid MCP device entry")
    rejected(binary,
             base({**valid, "transport": "private-ws"}),
             "unsupported MCP transport")
    rejected(binary,
             {"mcp": {"enabled": "yes", "devices": []}},
             "mcp.enabled must be boolean")
    rejected(binary,
             {"mcp": {"enabled": True, "required": True, "devices": []}},
             "MCP is required but no device is enabled")
    print("MCP config validation passed")


if __name__ == "__main__":
    main()
