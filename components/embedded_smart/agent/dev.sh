#!/usr/bin/env bash
# LiteCrab 本地调试一键启动脚本
#
# 用法:
#   ./dev.sh              # 编译(增量) + 启动 Agent + 启动网页 UI，Ctrl+C 全部退出
#   ./dev.sh stop         # 停止后台 Agent
#   ./dev.sh build        # 只编译不启动
#   ./dev.sh server       # 只启动 Agent(后台)，不启动 UI
#
# 环境变量:
#   SILICONFLOW_API_KEY    LLM API Key（必需，provider=siliconflow 时）
#   LITECRAB_LLM_PROVIDER  LLM Provider: siliconflow(默认) / cs / relayhub
#   LITECRAB_UI_PORT       网页 UI 端口，默认 10030
#
# 网页地址: http://localhost:10030

set -e

SERVER_PORT=10003
UI_PORT="${LITECRAB_UI_PORT:-10030}"
BUILD_DIR=build-diagnostic-host
PID_FILE=logs/dev_server.pid
SERVER_LOG=logs/dev_server.log
PROVIDER="${LITECRAB_LLM_PROVIDER:-siliconflow}"

case "$PROVIDER" in
  siliconflow) KEY_VAR=SILICONFLOW_API_KEY ;;
  cs)          KEY_VAR=CS_API_KEY ;;
  relayhub)    KEY_VAR=RELAYHUB_API_KEY ;;
  *) echo "未知 provider: $PROVIDER (可选 siliconflow/cs/relayhub)"; exit 1 ;;
esac

cd "$(dirname "$0")"
mkdir -p logs

port_listening() {
  local hex
  hex=$(printf '%04X' "$1")
  awk 'NR>1 && $4=="0A" {print $2}' /proc/net/tcp /proc/net/tcp6 2>/dev/null \
    | grep -qi ":$hex\$"
}

stop_server() {
  if [ -f "$PID_FILE" ]; then
    PID=$(cat "$PID_FILE")
    if kill -0 "$PID" 2>/dev/null; then
      echo "[dev] 停止 Agent (pid $PID)"
      kill "$PID" 2>/dev/null || true
      for _ in 1 2 3 4 5 6 7 8 9 10; do
        kill -0 "$PID" 2>/dev/null || break
        sleep 0.5
      done
      kill -9 "$PID" 2>/dev/null || true
    fi
    rm -f "$PID_FILE"
  fi
}

ensure_config() {
  [ -f config/base_config.json ] || { cp config/base_config.example.json config/base_config.json; echo "[dev] 已生成 config/base_config.json"; }
  [ -f config/llm_config.json ]  || { cp config/llm_config.example.json config/llm_config.json;  echo "[dev] 已生成 config/llm_config.json"; }
}

do_build() {
  if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    echo "[dev] 首次配置 CMake ($BUILD_DIR)"
    cmake -S . -B "$BUILD_DIR" \
      -DLITECRAB_PROFILE=diagnostic_sd5091 \
      -DCMAKE_BUILD_TYPE=Release
  fi
  echo "[dev] 增量编译..."
  cmake --build "$BUILD_DIR" -j4
}

start_server() {
  if [ ! -x "$BUILD_DIR/litecrab_server" ]; then
    echo "[dev] 错误: 未找到 $BUILD_DIR/litecrab_server，请先 ./dev.sh build"; exit 1
  fi
  stop_server
  if port_listening "$SERVER_PORT"; then
    echo "[dev] 错误: 端口 $SERVER_PORT 已被占用，无法启动 Agent"; exit 1
  fi
  echo "[dev] 启动 Agent: $BUILD_DIR/litecrab_server (provider=$PROVIDER)"
  : > "$SERVER_LOG"
  SSL_CERT_FILE=./config/certs/ca-bundle.crt \
  "$BUILD_DIR/litecrab_server" \
    --config config/base_config.json \
    --llm-config config/llm_config.json \
    --llm-provider "$PROVIDER" \
    --workspace . \
    >> "$SERVER_LOG" 2>&1 &
  echo $! > "$PID_FILE"

  for _ in $(seq 1 20); do
    if port_listening "$SERVER_PORT"; then
      echo "[dev] Agent 就绪: 127.0.0.1:$SERVER_PORT"
      return 0
    fi
    if ! kill -0 "$(cat "$PID_FILE")" 2>/dev/null; then
      echo "[dev] Agent 启动失败，日志尾部:"; tail -20 "$SERVER_LOG"; exit 1
    fi
    sleep 0.5
  done
  echo "[dev] 错误: Agent 10 秒内未监听 $SERVER_PORT，日志尾部:"; tail -20 "$SERVER_LOG"; exit 1
}

start_all() {
  if [ -z "${!KEY_VAR:-}" ]; then
    echo "[dev] 错误: 未设置 $KEY_VAR 环境变量，例如:"
    echo "       export $KEY_VAR='你的key'"
    exit 1
  fi
  command -v python3 >/dev/null || { echo "[dev] 错误: 缺少 python3"; exit 1; }
  python3 -c 'import flask' 2>/dev/null || { echo "[dev] 错误: 缺少 flask，请安装: sudo apt install python3-flask"; exit 1; }

  ensure_config
  do_build
  start_server

  trap 'stop_server; exit 0' INT TERM
  echo "[dev] 网页 UI 启动: http://localhost:$UI_PORT  (Ctrl+C 退出并停止 Agent)"
  LITECRAB_AGENT_HOST=127.0.0.1 LITECRAB_AGENT_PORT="$SERVER_PORT" \
    python3 others/UI/app.py
  stop_server
}

case "${1:-start}" in
  start)  start_all ;;
  stop)   stop_server; echo "[dev] 已停止" ;;
  build)  do_build ;;
  server) ensure_config; start_server; echo "[dev] 仅 Agent 模式，停止请执行 ./dev.sh stop" ;;
  *) echo "用法: ./dev.sh [start|stop|build|server]"; exit 1 ;;
esac
