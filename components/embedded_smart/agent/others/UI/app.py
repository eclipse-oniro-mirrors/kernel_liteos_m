from flask import Flask, request, jsonify, render_template
import json
import os
import socket
import threading
import uuid

app = Flask(__name__)

BOARD_IP = os.getenv("LITECRAB_AGENT_HOST", "192.168.8.10")
BOARD_PORT = int(os.getenv("LITECRAB_AGENT_PORT", "10003"))
CONNECT_TIMEOUT = float(os.getenv("LITECRAB_CONNECT_TIMEOUT", "5"))
RESPONSE_TIMEOUT = float(os.getenv("LITECRAB_RESPONSE_TIMEOUT", "600"))
UI_USER_ID = os.getenv("LITECRAB_UI_USER_ID", "litecrab-ui")
# UI 监听端口与 Agent 端口分离，避免本机 localhost 同时命中板端 TCP 服务
UI_HOST = os.getenv("LITECRAB_UI_HOST", "0.0.0.0")
UI_PORT = int(os.getenv("LITECRAB_UI_PORT", "10030"))

# 保存轻量会话句柄和会话级请求锁；TCP socket 为单次请求资源。
sessions = {}
sessions_lock = threading.Lock()


def recv_response(sock):
    data = bytearray()
    first = sock.recv(4096)
    if not first:
        raise ConnectionError("Agent 已关闭连接")
    data.extend(first)
    # Both the legacy response and JSON envelope occupy one wire line.
    # Wait for its delimiter; a short packet gap is not an end-of-message.
    max_bytes = 6 * 1024 * 1024  # JSON can escape one byte as six characters.
    while b"\n" not in data and len(data) <= max_bytes:
        chunk = sock.recv(4096)
        if not chunk:
            raise ConnectionError("Agent 返回内容不完整")
        data.extend(chunk)
    if len(data) > max_bytes:
        raise ConnectionError("Agent 返回内容超过 6 MiB 限制")
    if data.endswith(b"\n"):
        del data[-1:]
        if data.endswith(b"\r"):
            del data[-1:]
    return data.decode("utf-8", errors="replace")


class BoardSession:
    def __init__(self, board_ip, board_port, session_id, user_id):
        self.board_ip = board_ip
        self.board_port = board_port
        self.session_id = session_id
        self.user_id = user_id
        self.lock = threading.Lock()

    def send_request(self, message, decision=None, interrupt_id=None):
        """发送一行请求并返回 {"content": str, "interrupt": dict|None}。

        decision/interrupt_id 仅在审批决策请求时携带（安全计划第 11 步），
        普通请求的 payload 保持四字段不变。
        """
        with self.lock:
            # TCP connections are transport resources, not Agent sessions.
            # Open a fresh connection per request so restarting litecrab_server
            # cannot leave a stale Windows socket cached in the UI process.
            payload = {
                "userId": self.user_id,
                "sessionId": self.session_id,
                "content": message,
                "responseFormat": "json",
            }
            if decision:
                payload["decision"] = decision
            if interrupt_id:
                payload["replyToInterruptId"] = interrupt_id
            wire = json.dumps(payload, ensure_ascii=False, separators=(",", ":"))
            with socket.create_connection(
                (self.board_ip, self.board_port), timeout=CONNECT_TIMEOUT
            ) as sock:
                sock.settimeout(RESPONSE_TIMEOUT)
                sock.sendall((wire + "\n").encode("utf-8"))
                response = recv_response(sock)
                try:
                    envelope = json.loads(response)
                except (ValueError, TypeError):
                    return {"content": response, "interrupt": None}  # Older agents: plain text.
                if (isinstance(envelope, dict)
                        and envelope.get("responseFormat") == "json"
                        and isinstance(envelope.get("content"), str)):
                    interrupt = envelope.get("interrupt")
                    return {
                        "content": envelope["content"],
                        "interrupt": interrupt if isinstance(interrupt, dict) else None,
                    }
                return {"content": response, "interrupt": None}

    def send_and_recv(self, message: str):
        return self.send_request(message)["content"]

    def close(self):
        # Connections are request-scoped. Keeping this method preserves the
        # /api/close contract and allows future per-session resources.
        return None


@app.route('/')
def index():
    return render_template('index.html')


@app.route('/api/health')
def health():
    try:
        with socket.create_connection((BOARD_IP, BOARD_PORT), timeout=CONNECT_TIMEOUT):
            return jsonify({"success": True, "agent": f"{BOARD_IP}:{BOARD_PORT}"})
    except OSError as exc:
        return jsonify({"success": False, "agent": f"{BOARD_IP}:{BOARD_PORT}", "error": str(exc)}), 503


@app.route('/api/chat', methods=['POST'])
def chat():
    data = request.get_json(silent=True) or {}
    message = (data.get("message") or "").strip()
    session_id = data.get("session_id")
    decision = data.get("decision") or None
    interrupt_id = data.get("reply_to_interrupt_id") or None

    if not message:
        return jsonify({
            "success": False,
            "error": "message 不能为空"
        }), 400

    try:
        # 如果没有 session_id，就新建一个会话
        if not session_id:
            session_id = str(uuid.uuid4())
            session = BoardSession(BOARD_IP, BOARD_PORT, session_id, UI_USER_ID)
            with sessions_lock:
                sessions[session_id] = session
        else:
            with sessions_lock:
                session = sessions.get(session_id)

            if session is None:
                # UI 进程重启后，用浏览器保存的稳定 session_id 重建轻量句柄。
                session = BoardSession(BOARD_IP, BOARD_PORT, session_id, UI_USER_ID)
                with sessions_lock:
                    sessions[session_id] = session

        result = session.send_request(message, decision=decision, interrupt_id=interrupt_id)

        return jsonify({
            "success": True,
            "session_id": session_id,
            "response": result["content"],
            "interrupt": result["interrupt"],
        })

    except Exception as e:
        return jsonify({
            "success": False,
            "session_id": session_id,
            "error": str(e)
        }), 500


@app.route('/api/close', methods=['POST'])
def close_session():
    data = request.get_json(silent=True) or {}
    session_id = data.get("session_id")

    if not session_id:
        return jsonify({
            "success": False,
            "error": "session_id 不能为空"
        }), 400

    with sessions_lock:
        session = sessions.pop(session_id, None)

    if session:
        session.close()

    return jsonify({
        "success": True,
        "message": "会话已关闭"
    })


if __name__ == '__main__':
    print(f"LiteCrab UI:  http://localhost:{UI_PORT}  (Agent -> {BOARD_IP}:{BOARD_PORT})")
    debug = os.getenv("FLASK_DEBUG", "0").lower() in {"1", "true", "yes"}
    app.run(host=UI_HOST, port=UI_PORT, debug=debug)
