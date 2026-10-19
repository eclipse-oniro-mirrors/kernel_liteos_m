#!/usr/bin/env python3
"""Memory tool, process restart, and cross-user prompt isolation."""
import http.server
import hashlib
import json
import os
import socket
import subprocess
import sys
import tempfile
import threading
import time


def port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class Provider(http.server.BaseHTTPRequestHandler):
    def do_POST(self):
        request = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        messages = request["messages"]
        if messages[0].get("content", "").startswith("# Skill Resolver"):
            reply = {"content": json.dumps({"action": "none", "skill": "", "confidence": 0.99,
                                            "reason_code": "memory_test"})}
        else:
            user = next((m.get("content") for m in reversed(messages) if m.get("role") == "user"), "")
            system = messages[0].get("content", "")
            tools = {t["function"]["name"] for t in request.get("tools", [])}
            if user == "remember-blue" and not any(m.get("role") == "tool" for m in messages):
                assert "memory_remember" in tools
                reply = {"tool_calls": [{"index": 0, "id": "memory-1", "type": "function",
                                         "function": {"name": "memory_remember", "arguments": json.dumps({
                                             "category": "preference", "content": "Favorite color is blue"})}}]}
            elif user == "remember-blue":
                result = messages[-1].get("content", "")
                if "memory was not saved" in result:
                    assert "success: false" in result
                    reply = {"content": "save-failed"}
                else:
                    assert "success: true" in result and '"code":"stored"' in result
                    reply = {"content": "stored"}
            elif user == "recall":
                reply = {"content": "found" if "Favorite color is blue" in system else "absent"}
            elif user == "check-tools":
                reply = {"content": "enabled" if "memory_remember" in tools else "disabled"}
            elif user == "read-memory" and not any(m.get("role") == "tool" for m in messages):
                alice_hash = hashlib.sha256(b"alice").hexdigest()[:16]
                reply = {"tool_calls": [{"index": 0, "id": "read-1", "type": "function",
                                         "function": {"name": "read", "arguments": json.dumps({
                                             "path": f".crab/memory/users/{alice_hash}/summary.md"})}}]}
            elif user == "read-memory":
                reply = {"content": "blocked" if "Favorite color is blue" not in messages[-1].get("content", "")
                         else "leaked"}
            else:
                reply = {"content": "ok"}
        event = {"choices": [{"delta" if request.get("stream") else "message": reply}]}
        body = (f"data: {json.dumps(event)}\n\ndata: [DONE]\n\n" if request.get("stream")
                else json.dumps(event)).encode()
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream" if request.get("stream") else "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *_):
        pass


def start(binary, workspace, provider_port, config):
    gateway = port()
    env = os.environ.copy()
    env.update({"LITECRAB_BASE_URL": f"http://127.0.0.1:{provider_port}/v1/chat/completions",
                "LITECRAB_API_KEY": "test", "LITECRAB_MODEL": "mock",
                "LITECRAB_LOG_DIR": os.path.join(workspace, "logs")})
    process = subprocess.Popen([binary, "--config", config, "--workspace", workspace,
                                "--port", str(gateway)], env=env,
                               stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
    for _ in range(100):
        try:
            with socket.create_connection(("127.0.0.1", gateway), timeout=.1):
                return process, gateway
        except OSError:
            if process.poll() is not None:
                raise RuntimeError(process.stderr.read())
            time.sleep(.05)
    raise RuntimeError("server did not start")


def ask(gateway, user, session, content):
    with socket.create_connection(("127.0.0.1", gateway), timeout=2) as s:
        s.settimeout(10)
        s.sendall((json.dumps({"userId": user, "sessionId": session, "content": content}) + "\n").encode())
        data = bytearray()
        while not data.endswith(b"\n"):
            data.extend(s.recv(4096))
        return data.decode().strip()


def main():
    binary = os.path.abspath(sys.argv[1])
    provider_port = port()
    provider = http.server.ThreadingHTTPServer(("127.0.0.1", provider_port), Provider)
    threading.Thread(target=provider.serve_forever, daemon=True).start()
    try:
        with tempfile.TemporaryDirectory(prefix="litecrab-memory-e2e-") as workspace:
            config = os.path.join(workspace, "config.json")
            with open(config, "w", encoding="utf-8") as f:
                json.dump({"memory": {"enabled": True}}, f)
            process, gateway = start(binary, workspace, provider_port, config)
            try:
                assert ask(gateway, "alice", "session-a", "remember-blue") == "stored"
            finally:
                process.terminate(); process.wait(timeout=5)
            process, gateway = start(binary, workspace, provider_port, config)
            try:
                assert ask(gateway, "alice", "session-b", "recall") == "found"
                assert ask(gateway, "alice", "session-b", "read-memory") == "blocked"
                assert ask(gateway, "bob", "session-c", "recall") == "absent"
                bob_hash = hashlib.sha256(b"bob").hexdigest()[:16]
                bob_dir = os.path.join(workspace, ".crab", "memory", "users", bob_hash)
                os.makedirs(os.path.join(bob_dir, "summary.md"))
                assert ask(gateway, "bob", "session-c", "remember-blue") == "save-failed"
            finally:
                process.terminate(); process.wait(timeout=5)
            with open(config, "w", encoding="utf-8") as f:
                json.dump({"memory": {"enabled": False}}, f)
            process, gateway = start(binary, workspace, provider_port, config)
            try:
                assert ask(gateway, "alice", "session-d", "check-tools") == "disabled"
                assert ask(gateway, "alice", "session-d", "recall") == "absent"
                assert ask(gateway, "alice", "session-d", "read-memory") == "blocked"
            finally:
                process.terminate(); process.wait(timeout=5)
            memory_dir = os.path.join(workspace, ".crab", "memory")
            moved_memory_dir = os.path.join(workspace, "moved-memory")
            os.rename(memory_dir, moved_memory_dir)
            os.symlink(moved_memory_dir, memory_dir)
            with open(config, "w", encoding="utf-8") as f:
                json.dump({"memory": {"enabled": True}}, f)
            failed_gateway = port()
            env = os.environ.copy()
            env.update({"LITECRAB_BASE_URL": f"http://127.0.0.1:{provider_port}/v1/chat/completions",
                        "LITECRAB_API_KEY": "test", "LITECRAB_MODEL": "mock",
                        "LITECRAB_LOG_DIR": os.path.join(workspace, "logs")})
            process = subprocess.Popen([binary, "--config", config, "--workspace", workspace,
                                        "--port", str(failed_gateway)], env=env,
                                       stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
            _, stderr = process.communicate(timeout=5)
            assert process.returncode != 0
            assert "failed to initialize LiteCrab" in stderr
            assert "buffer overflow detected" not in stderr
    finally:
        provider.shutdown(); provider.server_close()


if __name__ == "__main__":
    main()
