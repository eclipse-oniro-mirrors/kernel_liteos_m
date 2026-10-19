#!/usr/bin/env python3
"""Interactive approval end-to-end tests (security plan step 10/11).

Covers: approve / reject / always_allow roundtrips, natural-language red
line, protocol validation errors, readonly and excluded tools, restart
recovery, disabled-config regression, and expiry unlock.
"""

import http.server
import json
import os
import socket
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def recv_line(sock):
    data = bytearray()
    while not data.endswith(b"\n"):
        chunk = sock.recv(4096)
        if not chunk:
            raise RuntimeError("agent closed the connection")
        data.extend(chunk)
    return data.decode(errors="replace").rstrip("\r\n")


def tool_call(call_id, name, arguments):
    return {
        "index": 0,
        "id": call_id,
        "type": "function",
        "function": {"name": name, "arguments": json.dumps(arguments)},
    }


class Provider(http.server.BaseHTTPRequestHandler):
    """Content-scripted mock LLM: decisions derive from the latest user prompt,
    so scripting survives agent restarts."""

    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        request = json.loads(self.rfile.read(length) or b"{}")
        messages = request.get("messages", [])
        prompt = ""
        for m in reversed(messages):
            if m.get("role") == "user":
                prompt = m.get("content", "")
                break
        tool_messages = [m for m in messages if m.get("role") == "tool"]
        reply = self.decide(prompt, tool_messages)
        body = json.dumps({"choices": [{"message": reply}], "usage": {
            "prompt_tokens": 3, "completion_tokens": 2, "total_tokens": 5
        }}, ensure_ascii=False).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *_):
        pass

    def decide(self, prompt, tool_messages):
        if prompt.startswith("DO-WRITE"):
            quiet = prompt.startswith("DO-WRITE-QUIET")
            marker = prompt.split(" ", 1)[1] if " " in prompt else "marker.txt"
            call_id = "w-" + marker
            mine = [m for m in tool_messages if m.get("tool_call_id") == call_id]
            if not mine:
                reply = {"tool_calls": [tool_call(call_id, "write", {
                    "path": marker, "content": "approval-e2e", "mode": "create",
                })]}
                if not quiet:
                    reply["content"] = "好的，我来创建这个文件。"
                return reply
            last = mine[-1].get("content", "")
            if "pending_approval" in last:
                return {"content": "SUSPENDED_SEEN"}
            if "rejected_by_operator" in last:
                return {"content": "REJECTED_SEEN"}
            if "approval_finalized" in last:
                return {"content": "FINALIZED_SEEN"}
            ok = "success: true" in last or "success:true" in last
            return {"content": "WRITTEN_SEEN" if ok else "WRITE_BAD:" + last[:160]}
        if prompt.startswith("DO-READ"):
            if not any(m.get("tool_call_id") == "r-1" for m in tool_messages):
                return {"tool_calls": [tool_call("r-1", "glob", {"pattern": "*.md"})]}
            return {"content": "READ_SEEN"}
        if prompt.startswith("DO-MEMORY"):
            if not any(m.get("tool_call_id") == "m-1" for m in tool_messages):
                return {"tool_calls": [tool_call("m-1", "memory_remember", {
                    "category": "note", "content": "approval e2e memory",
                })]}
            return {"content": "MEMORY_SEEN"}
        return {"content": "ECHO:" + prompt}


def ask(sock, content, session, decision=None, interrupt=None, json_mode=True,
        user="approval-user"):
    payload = {"userId": user, "sessionId": session, "content": content}
    if json_mode:
        payload["responseFormat"] = "json"
    if decision:
        payload["decision"] = decision
    if interrupt:
        payload["replyToInterruptId"] = interrupt
    sock.sendall((json.dumps(payload, ensure_ascii=False) + "\n").encode())
    line = recv_line(sock)
    return json.loads(line) if json_mode else line


def wait_for_port(port, timeout=10):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.2):
                return
        except OSError:
            time.sleep(0.05)
    raise RuntimeError("agent did not start")


def spawn(binary, workspace, provider_port, approval_enabled=True, timeout_ms=600000):
    config_dir = tempfile.mkdtemp(prefix="litecrab-approval-cfg-")
    base = Path(config_dir) / "base.json"
    llm = Path(config_dir) / "llm.json"
    base.write_text(json.dumps({
        "approval": {"enabled": approval_enabled, "timeout_ms": timeout_ms}
    }), encoding="utf-8")
    llm.write_text(json.dumps({"llm": {
        "default": "mock",
        "mock": {
            "baseUrl": f"http://127.0.0.1:{provider_port}/v1/chat/completions",
            "apiKeyEnv": "APPROVAL_API_KEY",
            "modelName": "approval-mock",
        },
        "max_tokens": 512,
        "temperature": 0.1,
        "stream": False,
        "timeout_ms": 5000,
    }}), encoding="utf-8")
    port = free_port()
    env = os.environ.copy()
    env["APPROVAL_API_KEY"] = "approval-secret"
    for name in ("LITECRAB_BASE_URL", "LITECRAB_API_KEY", "LITECRAB_MODEL"):
        env.pop(name, None)
    process = subprocess.Popen([
        binary, "--config", str(base), "--llm-config", str(llm),
        "--workspace", workspace, "--port", str(port),
    ], cwd=workspace, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
    wait_for_port(port)
    return process, port


def stop(process):
    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=3)


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_approval_e2e.py LITECRAB_SERVER")
    binary = sys.argv[1]
    failures = []
    total = [0]

    def check(condition, name):
        total[0] += 1
        print(("PASS  " if condition else "FAIL  ") + name)
        if not condition:
            failures.append(name)

    provider_port = free_port()
    provider = http.server.ThreadingHTTPServer(("127.0.0.1", provider_port), Provider)
    threading.Thread(target=provider.serve_forever, daemon=True).start()

    with tempfile.TemporaryDirectory(prefix="litecrab-approval-e2e-") as workspace:
        def marker(name):
            return os.path.join(workspace, name)

        # ---------- 默认配置（审批开启） ----------
        process, port = spawn(binary, workspace, provider_port)
        client = socket.create_connection(("127.0.0.1", port), timeout=20)
        client.settimeout(20)
        try:
            # 1 批准：挂起 -> interrupt -> approve -> 执行
            r = ask(client, "DO-WRITE m1.txt", "s1")
            check("interrupt" in r and r["interrupt"]["toolName"] == "write",
                  "suspend response carries structured interrupt")
            check(r.get("content") == "好的，我来创建这个文件。",
                  "suspension carries the model's own text")
            iid = r.get("interrupt", {}).get("interruptId", "")
            check(iid.startswith("apr-"), "interrupt id has apr- prefix")
            check(not os.path.exists(marker("m1.txt")), "no side effect before decision")
            r2 = ask(client, "approve", "s1", decision="approve", interrupt=iid)
            check("WRITTEN_SEEN" in r2.get("content", ""),
                  "approve executes the tool and model sees result")
            check(os.path.exists(marker("m1.txt")), "marker file exists after approval")
            check("interrupt" not in r2, "no interrupt envelope after resolution")

            # 2 拒绝（模型无附带文本 → 中性兜底文案）
            r = ask(client, "DO-WRITE-QUIET m2.txt", "s1")
            check(r.get("content") == "这个操作需要你的确认。",
                  "suspension falls back to neutral text without model text")
            iid = r.get("interrupt", {}).get("interruptId", "")
            r2 = ask(client, "reject", "s1", decision="reject", interrupt=iid)
            check("REJECTED_SEEN" in r2.get("content", ""),
                  "reject informs the model without executing")
            check(not os.path.exists(marker("m2.txt")), "no marker after reject")

            # 3 本会话总是允许
            r = ask(client, "DO-WRITE m3.txt", "s1")
            iid = r.get("interrupt", {}).get("interruptId", "")
            r2 = ask(client, "always", "s1", decision="always_allow", interrupt=iid)
            check("WRITTEN_SEEN" in r2.get("content", ""), "always_allow executes")
            check(os.path.exists(marker("m3.txt")), "always_allow marker exists")
            r3 = ask(client, "DO-WRITE m4.txt", "s1")
            check("interrupt" not in r3 and "WRITTEN_SEEN" in r3.get("content", ""),
                  "second write in session skips popup")
            check(os.path.exists(marker("m4.txt")), "second marker exists")

            # 4 自然语言红线
            r = ask(client, "DO-WRITE m5.txt", "s2")
            iid = r.get("interrupt", {}).get("interruptId", "")
            r2 = ask(client, "是的，请执行", "s2")
            check("interrupt" in r2 and "等待你的确认" in r2.get("content", ""),
                  "natural language gets guidance with interrupt envelope")
            check(not os.path.exists(marker("m5.txt")),
                  "natural language never executes the tool")

            # 5-7 协议校验
            r = ask(client, "x", "s2", decision="approve")
            check(r.get("content", "").startswith("ERROR: decision requires"),
                  "decision without reference is rejected")
            r = ask(client, "x", "s2", interrupt="apr-00000001")
            check(r.get("content", "").startswith("ERROR: approval reference requires"),
                  "approval reference without decision is rejected")
            r = ask(client, "x", "s2", decision="maybe", interrupt="apr-00000001")
            check("invalid decision" in r.get("content", ""),
                  "invalid decision value is rejected")

            # 8 只读工具不触发
            r = ask(client, "DO-READ", "s5")
            check("interrupt" not in r and "READ_SEEN" in r.get("content", ""),
                  "readonly tools never interrupt")

            # 9 排除清单：memory_remember 不触发
            r = ask(client, "DO-MEMORY", "s5")
            check("interrupt" not in r and "MEMORY_SEEN" in r.get("content", ""),
                  "excluded memory_remember never interrupts")

            # 13 跨会话决策被拒，原会话仍可决策
            r = ask(client, "DO-WRITE m6.txt", "s3")
            iid = r.get("interrupt", {}).get("interruptId", "")
            r2 = ask(client, "decide", "s4", decision="approve", interrupt=iid)
            check("未找到对应的确认" in r2.get("content", ""),
                  "cross-session decision is rejected")
            r3 = ask(client, "decide", "s3", decision="approve", interrupt=iid)
            check("WRITTEN_SEEN" in r3.get("content", ""),
                  "owning session can still decide after cross-session attempt")
            check(os.path.exists(marker("m6.txt")), "cross-session attempt left no side effect")

            # 14 老客户端（纯文本模式）兼容
            r = ask(client, "plain check", "s2", json_mode=False)
            check("等待你的确认" in r, "plain-text client sees readable guidance")
        finally:
            client.close()
            stop(process)

        # ---------- 重启恢复 ----------
        process, port = spawn(binary, workspace, provider_port)
        client = socket.create_connection(("127.0.0.1", port), timeout=20)
        client.settimeout(20)
        try:
            r = ask(client, "DO-WRITE m7.txt", "s6")
            iid = r.get("interrupt", {}).get("interruptId", "")
            check("interrupt" in r, "suspension for restart test")
        finally:
            client.close()
            stop(process)
        process, port = spawn(binary, workspace, provider_port)
        client = socket.create_connection(("127.0.0.1", port), timeout=20)
        client.settimeout(20)
        try:
            r2 = ask(client, "approve", "s6", decision="approve", interrupt=iid)
            check("WRITTEN_SEEN" in r2.get("content", ""),
                  "pending approval survives restart and executes")
            check(os.path.exists(marker("m7.txt")), "restart marker exists")
        finally:
            client.close()
            stop(process)

        # ---------- 审批关闭回归 ----------
        process, port = spawn(binary, workspace, provider_port, approval_enabled=False)
        client = socket.create_connection(("127.0.0.1", port), timeout=20)
        client.settimeout(20)
        try:
            r = ask(client, "DO-WRITE m8.txt", "s7")
            check("interrupt" not in r and "WRITTEN_SEEN" in r.get("content", ""),
                  "disabled approval executes directly (regression)")
            check(os.path.exists(marker("m8.txt")), "disabled-config marker exists")
        finally:
            client.close()
            stop(process)

        # ---------- 过期解锁 ----------
        process, port = spawn(binary, workspace, provider_port, timeout_ms=10000)
        client = socket.create_connection(("127.0.0.1", port), timeout=30)
        client.settimeout(30)
        try:
            r = ask(client, "DO-WRITE m9.txt", "s8")
            check("interrupt" in r, "suspension for expiry test")
            time.sleep(11)
            r2 = ask(client, "hello after expiry", "s8")
            check("interrupt" not in r2 and "ECHO:" in r2.get("content", ""),
                  "session unblocked after expiry (lazy resolution)")
            check(not os.path.exists(marker("m9.txt")),
                  "expired approval never executes")
        finally:
            client.close()
            stop(process)

    provider.shutdown()
    provider.server_close()
    print("approval e2e: %d checks, %d failures" % (total[0], len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
