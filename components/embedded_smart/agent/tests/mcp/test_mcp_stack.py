#!/usr/bin/env python3
import concurrent.futures
import http.client
import http.server
import json
import os
import signal
import socket
import subprocess
import sys
import time
import threading


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def wait_port(port, process):
    deadline = time.time() + 5
    while time.time() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"server exited early: {process.stderr.read()}")
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=.1):
                return
        except OSError:
            time.sleep(.03)
    raise RuntimeError("server did not listen")


def start_server(binary, *args, env=None):
    port = free_port()
    process = subprocess.Popen(
        [binary, "--listen", "127.0.0.1", "--port", str(port), *args],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        text=True,
        env=env,
    )
    wait_port(port, process)
    return process, port


def stop_server(process):
    if process.poll() is None:
        process.send_signal(signal.SIGTERM)
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            raise AssertionError("MCP server failed graceful shutdown")
    assert process.returncode == 0, process.stderr.read()


def rpc(port, method, params=None, *, version="2026-07-28", headers=None, content_type="application/json"):
    params = dict(params or {})
    if version == "2026-07-28":
        params.setdefault("_meta", {
            "io.modelcontextprotocol/protocolVersion": "2026-07-28",
            "io.modelcontextprotocol/clientCapabilities": {},
        })
    body = json.dumps({"jsonrpc": "2.0", "id": 1, "method": method, "params": params})
    request_headers = {
        "Content-Type": content_type,
        "Accept": "application/json, text/event-stream",
        "MCP-Protocol-Version": version,
        "Mcp-Method": method,
    }
    if method == "tools/call" and params and "name" in params:
        request_headers["Mcp-Name"] = params["name"]
    request_headers.update(headers or {})
    connection = http.client.HTTPConnection("127.0.0.1", port, timeout=3)
    connection.request("POST", "/mcp", body, request_headers)
    response = connection.getresponse()
    data = response.read()
    output = json.loads(data) if data else None
    result = response.status, dict(response.getheaders()), output
    connection.close()
    return result


def modern_protocol(port):
    status, _, output = rpc(port, "server/discover", {"_meta": {
        "io.modelcontextprotocol/protocolVersion": "2026-07-28",
        "io.modelcontextprotocol/clientCapabilities": {},
    }})
    assert status == 200 and "2026-07-28" in output["result"]["supportedVersions"]
    status, _, output = rpc(port, "tools/list", {"_meta": {
        "io.modelcontextprotocol/protocolVersion": "2026-07-28",
        "io.modelcontextprotocol/clientCapabilities": {},
    }})
    assert status == 200 and len(output["result"]["tools"]) == 4
    status, _, output = rpc(port, "tools/call", {
        "name": "device.add", "arguments": {"a": 20, "b": 22},
        "_meta": {"io.modelcontextprotocol/protocolVersion": "2026-07-28",
                  "io.modelcontextprotocol/clientCapabilities": {}},
    })
    assert status == 200 and output["result"]["structuredContent"]["sum"] == 42
    status, _, output = rpc(port, "tools/call", {
        "name": "device.echo", "arguments": {"message": "你好-MCP"},
        "_meta": {"io.modelcontextprotocol/protocolVersion": "2026-07-28",
                  "io.modelcontextprotocol/clientCapabilities": {}},
    })
    assert output["result"]["structuredContent"]["echo"] == "你好-MCP"


def legacy_protocol(port):
    status, headers, output = rpc(port, "initialize", {
        "protocolVersion": "2025-11-25", "capabilities": {},
        "clientInfo": {"name": "test", "version": "1"},
    }, version="2025-11-25")
    assert status == 200 and output["result"]["protocolVersion"] == "2025-11-25"
    session = headers.get("MCP-Session-Id")
    assert session
    status, _, output = rpc(port, "notifications/initialized", {}, version="2025-11-25",
                            headers={"MCP-Session-Id": session})
    assert status == 202 and output is None
    status, _, output = rpc(port, "tools/list", {}, version="2025-11-25",
                            headers={"MCP-Session-Id": session})
    assert status == 200 and len(output["result"]["tools"]) == 4
    status, _, output = rpc(port, "tools/list", {}, version="2025-11-25")
    assert output["error"]["code"] == -32001


def negative_protocol(port):
    status, _, output = rpc(port, "tools/list", {}, headers={"Mcp-Method": "tools/call"})
    assert status == 200 and output["error"]["code"] == -32600
    status, _, output = rpc(port, "tools/call", {"name": "device.add", "arguments": {"a": 1, "b": 2}},
                            headers={"Mcp-Name": "device.echo"})
    assert status == 200 and output["error"]["code"] == -32600
    status, _, output = rpc(port, "tools/call", {"name": "device.delay", "arguments": {"milliseconds": 9000}})
    assert status == 200 and output["error"]["code"] == -32602
    status, _, _ = rpc(port, "tools/list", {}, content_type="text/plain")
    assert status == 415
    status, _, _ = rpc(port, "tools/list", {}, headers={"Origin": "https://evil.example"})
    assert status == 403
    status, _, output = rpc(port, "tools/list", {}, version="not-a-version")
    assert status == 200 and output["error"]["code"] == -32600
    connection = http.client.HTTPConnection("127.0.0.1", port, timeout=3)
    connection.request("GET", "/mcp")
    response = connection.getresponse()
    response.read()
    assert response.status == 405
    connection.close()
    connection = http.client.HTTPConnection("127.0.0.1", port, timeout=3)
    connection.request("POST", "/mcp", "x" * 300000, {"Content-Type": "application/json"})
    response = connection.getresponse()
    response.read()
    assert response.status == 413
    connection.close()


class BadMcpHandler(http.server.BaseHTTPRequestHandler):
    mode = "bad-id"

    def log_message(self, *_args):
        pass

    def do_POST(self):
        size = int(self.headers.get("Content-Length", "0"))
        request = json.loads(self.rfile.read(size))
        method = request.get("method")
        if self.mode == "oversize":
            body = b"{" + b" " * 300000 + b"}"
        elif method == "server/discover":
            response_id = 999 if self.mode == "bad-id" else request.get("id")
            versions = ["2099-01-01"] if self.mode == "bad-version" else ["2026-07-28"]
            body = json.dumps({"jsonrpc": "2.0", "id": response_id, "result": {
                "supportedVersions": versions, "capabilities": {"tools": {}},
            }}).encode()
        elif method == "initialize":
            body = json.dumps({"jsonrpc": "2.0", "id": 999, "result": {
                "protocolVersion": "2025-11-25", "capabilities": {},
                "serverInfo": {"name": "bad", "version": "1"},
            }}).encode()
        elif method == "tools/list":
            body = json.dumps({"jsonrpc": "2.0", "id": request.get("id"), "result": {
                "tools": [{"name": "bad\nheader", "description": "unsafe",
                           "inputSchema": {"type": "object", "properties": {}}}],
            }}).encode()
        else:
            body = json.dumps({"jsonrpc": "2.0", "id": request.get("id"), "result": {}}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass


class SseMcpHandler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_args):
        pass

    def do_POST(self):
        size = int(self.headers.get("Content-Length", "0"))
        request = json.loads(self.rfile.read(size))
        method = request.get("method")
        request_id = request.get("id")
        if method == "server/discover":
            result = {"supportedVersions": ["2026-07-28"], "capabilities": {"tools": {}}}
        elif method == "tools/list":
            result = {"tools": [
                {"name": "device.get_status", "description": "status",
                 "inputSchema": {"type": "object", "properties": {}}},
                {"name": "device.echo", "description": "echo",
                 "inputSchema": {"type": "object", "properties": {
                     "message": {"type": "string", "maxLength": 1024}},
                     "required": ["message"]}},
                {"name": "device.add", "description": "add",
                 "inputSchema": {"type": "object", "properties": {
                     "a": {"type": "integer", "minimum": -100},
                     "b": {"type": "integer", "maximum": 100}},
                     "required": ["a", "b"]}},
                {"name": "device.delay", "description": "delay",
                 "inputSchema": {"type": "object", "properties": {
                     "milliseconds": {"type": "integer", "minimum": 0, "maximum": 5000}},
                     "required": ["milliseconds"]}},
            ]}
        elif method == "tools/call":
            params = request.get("params", {})
            name = params.get("name")
            arguments = params.get("arguments", {})
            if name == "device.echo":
                value = arguments.get("message", "")
            elif name == "device.add":
                value = str(arguments.get("a", 0) + arguments.get("b", 0))
            else:
                value = "ok"
            result = {"content": [{"type": "text", "text": value}], "isError": False}
        else:
            result = {}
        response = json.dumps({"jsonrpc": "2.0", "id": request_id, "result": result})
        body = ("event: message\r\ndata: " + response + "\r\n\r\n").encode()
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)


def reject_malicious_servers(probe):
    for mode in ("bad-id", "bad-version", "unsafe-tool", "oversize"):
        handler = type(f"BadMcpHandler_{mode}", (BadMcpHandler,), {"mode": mode})
        server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            completed = subprocess.run(
                [probe, f"http://127.0.0.1:{server.server_port}/mcp"],
                text=True, capture_output=True, timeout=10)
            assert completed.returncode != 0, f"malicious server accepted: {mode}"
        finally:
            server.shutdown()
            server.server_close()
            thread.join(timeout=2)
def concurrency_protocol(port):
    def one(index):
        _, _, output = rpc(port, "tools/call", {
            "name": "device.echo", "arguments": {"message": f"msg-{index}"},
        })
        return output["result"]["structuredContent"]["echo"]
    with concurrent.futures.ThreadPoolExecutor(max_workers=12) as pool:
        values = list(pool.map(one, range(80)))
    assert values == [f"msg-{i}" for i in range(80)]


def run_probe(probe, port, *args, env=None):
    completed = subprocess.run(
        [probe, f"http://127.0.0.1:{port}/mcp", *args],
        text=True, capture_output=True, timeout=15, env=env,
    )
    assert completed.returncode == 0, (completed.stdout, completed.stderr)
    return completed.stdout


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_mcp_stack.py DEVICE_SERVER CLIENT_PROBE")
    server_binary, probe_binary = sys.argv[1:]

    server, port = start_server(server_binary)
    try:
        modern_protocol(port)
        legacy_protocol(port)
        negative_protocol(port)
        concurrency_protocol(port)
        assert "ok" in run_probe(probe_binary, port)
        assert "ok" in run_probe(probe_binary, port, "--timeout-test")
    finally:
        stop_server(server)

    legacy, legacy_port = start_server(server_binary, "--legacy-only")
    try:
        assert "ok" in run_probe(probe_binary, legacy_port)
    finally:
        stop_server(legacy)

    token_env = os.environ.copy()
    token_env["MCP_TEST_TOKEN"] = "test-secret-token"
    token_env["MCP_TEST_USE_TOKEN"] = "1"
    protected, protected_port = start_server(
        server_binary, "--token-env", "MCP_TEST_TOKEN", env=token_env)
    try:
        status, _, _ = rpc(protected_port, "server/discover")
        assert status == 401
        assert "ok" in run_probe(probe_binary, protected_port, env=token_env)
    finally:
        stop_server(protected)

    unused = free_port()
    assert "optional-offline: ok" in run_probe(probe_binary, unused, "--optional-offline")
    sse = http.server.ThreadingHTTPServer(("127.0.0.1", 0), SseMcpHandler)
    sse_thread = threading.Thread(target=sse.serve_forever, daemon=True)
    sse_thread.start()
    try:
        assert "ok" in run_probe(probe_binary, sse.server_port)
    finally:
        sse.shutdown()
        sse.server_close()
        sse_thread.join(timeout=2)
    reject_malicious_servers(probe_binary)
    print("MCP stack tests passed: modern, legacy, JSON/SSE, auth, limits, concurrency, timeout, offline")


if __name__ == "__main__":
    main()
