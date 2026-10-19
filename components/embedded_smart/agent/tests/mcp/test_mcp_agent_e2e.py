#!/usr/bin/env python3
import http.server
import json
import os
from pathlib import Path
import signal
import socket
import subprocess
import sys
import tempfile
import threading
import time


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


class Provider(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    saw_remote_tool = False
    saw_remote_result = False

    def log_message(self, *_args):
        pass

    def do_POST(self):
        size = int(self.headers.get("Content-Length", "0"))
        request = json.loads(self.rfile.read(size))
        messages = request["messages"]
        system = messages[0].get("content", "") if messages else ""
        if system.startswith("# Skill Resolver"):
            reply = {"content": json.dumps({
                "selected_skill": None,
                "confidence": .99,
                "reason_code": "mcp_e2e_base",
                "evidence": "",
            })}
        else:
            names = {(tool.get("function") or {}).get("name") for tool in request.get("tools", [])}
            Provider.saw_remote_tool |= "lab__device_echo" in names
            tool_results = [message for message in messages if message.get("role") == "tool"]
            if not tool_results:
                reply = {"tool_calls": [{
                    "index": 0,
                    "id": "mcp-e2e-call",
                    "type": "function",
                    "function": {
                        "name": "lab__device_echo",
                        "arguments": json.dumps({"message": "agent-to-device-ok"}),
                    },
                }]}
            else:
                Provider.saw_remote_result |= "agent-to-device-ok" in tool_results[-1].get("content", "")
                reply = {"content": "MCP_AGENT_E2E_OK" if Provider.saw_remote_result else "MCP_AGENT_E2E_BAD"}
        if request.get("stream"):
            event = {"choices": [{"delta": reply}], "usage": {
                "prompt_tokens": 2, "completion_tokens": 2, "total_tokens": 4,
            }}
            body = ("data: " + json.dumps(event, ensure_ascii=False) + "\n\ndata: [DONE]\n\n").encode()
            content_type = "text/event-stream"
        else:
            body = json.dumps({"choices": [{"message": reply}]}, ensure_ascii=False).encode()
            content_type = "application/json"
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)


def wait_connection(process, port):
    deadline = time.time() + 8
    while time.time() < deadline:
        if process.poll() is not None:
            raise RuntimeError(process.stderr.read())
        try:
            return socket.create_connection(("127.0.0.1", port), timeout=2)
        except OSError:
            time.sleep(.05)
    raise RuntimeError("LiteCrab gateway did not start")


def recv_line(connection):
    data = bytearray()
    while not data.endswith(b"\n"):
        part = connection.recv(4096)
        if not part:
            raise RuntimeError("gateway closed before response")
        data.extend(part)
    return data.decode(errors="replace").rstrip("\r\n")


def main():
    if len(sys.argv) != 4:
        raise SystemExit("usage: test_mcp_agent_e2e.py AGENT DEVICE_SERVER REPO")
    agent_binary, device_binary, repo = sys.argv[1:]
    mcp_port, llm_port, gateway_port = free_port(), free_port(), free_port()
    device = subprocess.Popen(
        [device_binary, "--listen", "127.0.0.1", "--port", str(mcp_port)],
        stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
    provider = http.server.ThreadingHTTPServer(("127.0.0.1", llm_port), Provider)
    provider_thread = threading.Thread(target=provider.serve_forever, daemon=True)
    provider_thread.start()
    agent = None
    try:
        deadline = time.time() + 5
        while time.time() < deadline:
            try:
                with socket.create_connection(("127.0.0.1", mcp_port), timeout=.2):
                    break
            except OSError:
                if device.poll() is not None:
                    raise RuntimeError(device.stderr.read())
                time.sleep(.03)
        with tempfile.TemporaryDirectory() as temporary:
            config = Path(temporary) / "base.json"
            config.write_text(json.dumps({
                "server": {"listen_ip": "127.0.0.1", "listen_port": gateway_port,
                           "worker_threads": 2, "max_connections": 4},
                "workspace_root": repo,
                "log": {"dir": str(Path(temporary) / "logs")},
                "mcp": {
                    "enabled": True,
                    "required": True,
                    "connect_timeout_ms": 1000,
                    "request_timeout_ms": 2000,
                    "limits": {"max_message_bytes": 262144},
                    "devices": [{
                        "id": "mcp-test-device", "alias": "lab", "enabled": True,
                        "required": True, "transport": "streamable-http",
                        "url": f"http://127.0.0.1:{mcp_port}/mcp",
                    }],
                },
            }), encoding="utf-8")
            env = os.environ.copy()
            env.update({
                "LITECRAB_BASE_URL": f"http://127.0.0.1:{llm_port}/v1/chat/completions",
                "LITECRAB_API_KEY": "mcp-test-key",
                "LITECRAB_MODEL": "mcp-test-model",
            })
            agent = subprocess.Popen(
                [agent_binary, "--config", str(config), "--workspace", repo,
                 "--port", str(gateway_port), "--no-alarm"],
                cwd=repo, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
            with wait_connection(agent, gateway_port) as connection:
                request = json.dumps({"userId": "mcp-e2e", "sessionId": "mcp-e2e-session",
                                      "content": "call remote MCP echo"}, ensure_ascii=False)
                connection.sendall(request.encode() + b"\n")
                response = recv_line(connection)
                assert "MCP_AGENT_E2E_OK" in response, response
            assert Provider.saw_remote_tool, "remote MCP tool was not exposed to the model"
            assert Provider.saw_remote_result, "remote MCP result did not return through Agent"
    finally:
        if agent and agent.poll() is None:
            agent.send_signal(signal.SIGTERM)
            agent.wait(timeout=5)
        provider.shutdown()
        provider.server_close()
        if device.poll() is None:
            device.send_signal(signal.SIGTERM)
            device.wait(timeout=5)
    assert agent is not None and agent.returncode == 0, agent.stderr.read() if agent else ""
    assert device.returncode == 0, device.stderr.read()
    print("MCP Agent E2E passed: catalog -> LLM tool call -> device -> Agent response")


if __name__ == "__main__":
    main()
