#!/bin/bash
# run_coverage.sh - one-shot coverage run for the stm32f407vet6 (QEMU) port.
# Chain: derive coverage fragment (test.config + coverage macro, /tmp copy -
# no checked-in cov fragment, nothing in the tree gets mutated and the suite
# set can never drift from test.config) -> cov build -> QEMU full-suite run
# (gcov dump over UART) -> gen_coverage.sh (gcda rebuild -> lcov -> genhtml).
#
# Location independence: the repo root is derived from this script's own path
# (walked up until a directory containing kernel/liteos_m is found) - no
# machine-specific absolute paths. Only $HOME/.local/bin is added as an hb
# fallback when hb is not already on PATH (pip user install convention).
#
# Mechanism note: LITEOS_CONFIG accepts absolute paths (liteos.gni passes the
# env value through rebase_path into genconfig's KCONFIG_ALLCONFIG), so the
# derived /tmp fragment works exactly like a checked-in one. The coverage
# symbol's Kconfig dependency (KERNEL_TEST) is satisfied by test.config.
#
# Run-time behavior (measured): QEMU executes the full 372-case suite in a
# few SECONDS; the old flow wasted the rest of a 600s timeout on a parked
# machine. This script watches the log and kills QEMU as soon as the suite's
# last line ('--- Test End ---') appears, so the whole pipeline is bounded by
# the incremental build (~30s) + QEMU (~10s) + lcov (~3-4 min). The 300s cap
# below is only a diagnostic window for a wedged run, not the expected time.
#
# Fail-fast guards:
#   - before QEMU: the out/ image must be a coverage build (config.h marker);
#     catches --skip-build onto a stale test/demo image
#   - 30s after QEMU start: the log must have bytes (boot banner); a 0-byte
#     log means qemu died/was killed at startup - abort instead of failing
#     later with a generic gen_coverage error
#
# Usage (from anywhere - the script resolves its own repo root):
#   bash kernel/liteos_m/testsuites/src/gcov/run_coverage.sh [--skip-build]
#     --skip-build   reuse the image in out/ (no recompile, no fragment needed)
#
# Output:
#   suite log          : /tmp/cq.log (pass/fail lines + GCOV_* frames)
#   HTML report        : out/stm32f407vet6/stm32f407vet6_mini/coverage_html/index.html
#   lcov .info         : out/stm32f407vet6/stm32f407vet6_mini/coverage_html/stm32f407vet6_mini.info
set -o pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# walk up until the repo root (the dir containing kernel/liteos_m) is found -
# no hardcoded depth, immune to script relocation within the tree
REPO=$SCRIPT_DIR
while [ "$REPO" != "/" ] && [ ! -d "$REPO/kernel/liteos_m" ]; do
  REPO=$(dirname "$REPO")
done
if [ ! -d "$REPO/kernel/liteos_m" ]; then
  echo "ERROR: repo root resolution failed (walked up from '$SCRIPT_DIR' without finding kernel/liteos_m)"
  exit 1
fi

CFG_SRC=$REPO/kernel/liteos_m/vendor/st/stm32f407vet6_mini/kernel_configs/test.config
CFG_COV=/tmp/cov_fragment.config
OUTDIR=$REPO/out/stm32f407vet6/stm32f407vet6_mini
IMG=$OUTDIR/OHOS_Image
GEN=$SCRIPT_DIR/gen_coverage.sh
QEMU_LOG=/tmp/cq.log

SKIP_BUILD=0
[ "$1" = "--skip-build" ] && SKIP_BUILD=1

cd "$REPO" || exit 9
export PATH="$REPO/prebuilts/build-tools/linux-x86/bin:$PATH"
if ! command -v hb >/dev/null 2>&1; then
  export PATH="$HOME/.local/bin:$PATH"   # pip user install fallback for hb
fi

if [ "$SKIP_BUILD" = "0" ]; then
  echo "=== [1/3] derive cov fragment from test.config + build ==="
  cp "$CFG_SRC" "$CFG_COV"
  # idempotence: drop any pre-existing coverage lines, then append ours
  sed -i -e '/^LOSCFG_TEST_KERNEL_COVERAGE=/d' \
         -e '/^# LOSCFG_TEST_KERNEL_COVERAGE is not set/d' "$CFG_COV"
  echo "LOSCFG_TEST_KERNEL_COVERAGE=y" >> "$CFG_COV"
  export LITEOS_CONFIG=$CFG_COV
  hb build > /tmp/cb_run.log 2>&1
  rc=$?
  echo "  build rc=$rc"
  grep -E 'liteos_config_file|image size' /tmp/cb_run.log | tail -2
  if [ $rc -ne 0 ]; then
    echo "ERROR: build failed (rc=$rc) - see /tmp/cb_run.log"
    exit $rc
  fi
else
  echo "=== [1/3] cov build skipped (--skip-build) ==="
fi

# image + coverage marker precheck (both branches; catches stale non-cov images)
if [ ! -f "$IMG" ]; then
  echo "ERROR: image missing: $IMG (run without --skip-build first)"
  exit 1
fi
if ! grep -q 'LOSCFG_TEST_KERNEL_COVERAGE' "$OUTDIR/config.h" 2>/dev/null; then
  echo "ERROR: out/ image is NOT a coverage build (config.h lacks the marker):"
  echo "  a test/demo build overwrote out/. Rerun without --skip-build."
  exit 1
fi

echo "=== [2/3] QEMU run (suite takes seconds; 300s cap = wedged-run window) ==="
pkill -f 'qemu-system-arm.*stm32f407vet6' 2>/dev/null
sleep 1
timeout 300 qemu-system-arm -M netduinoplus2 -cpu cortex-m4 -nographic \
  -monitor none -kernel "$IMG" > "$QEMU_LOG" 2>&1 &
QPID=$!
# fail fast: the boot banner appears within seconds on a healthy run
for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30; do
  sleep 1
  [ -s "$QEMU_LOG" ] && break
done
if [ ! -s "$QEMU_LOG" ]; then
  kill $QPID 2>/dev/null
  echo "ERROR: QEMU produced no output within 30s - it died or was killed at"
  echo "startup (closed WSL session? concurrent run? binary problem?)."
  echo "Check manually: qemu-system-arm -M netduinoplus2 -cpu cortex-m4 -nographic -kernel $IMG"
  exit 1
fi

# watch the log: stop the parked QEMU the moment the suite's last line shows
SECONDS=0
KILLED_EARLY=0
while kill -0 $QPID 2>/dev/null; do
  if grep -aq -- '--- Test End ---' "$QEMU_LOG" 2>/dev/null; then
    sleep 2   # let the trailing dump lines flush
    kill $QPID 2>/dev/null
    KILLED_EARLY=1
    echo "  suite finished after ${SECONDS}s - QEMU stopped (no parked wait)"
    break
  fi
  sleep 1
done
wait $QPID
qrc=$?
# 0/124/143 are all normal stop variants (early kill / cap reached)
echo "  qemu rc=$qrc (0/124/143 all normal)"
SUITE=$(grep -a 'failed count' "$QEMU_LOG" | tail -1 | tr -d '\r')
echo "  $SUITE"
if ! grep -aq -- '--- Test End ---' "$QEMU_LOG"; then
  echo "  WARNING: suite did NOT reach '--- Test End ---' - coverage dump is incomplete:"
  tail -4 "$QEMU_LOG"
fi
echo "  dump frames : $(grep -ac 'GCOV_BEGIN' "$QEMU_LOG")"
echo "  dump done   : $(grep -ac 'GCOV_DUMP_DONE' "$QEMU_LOG") (expect 1)"
echo "  hard faults : $(grep -acE 'Fault|Lockup' "$QEMU_LOG") (expect 0)"

echo "=== [3/3] lcov + genhtml (typically 3-4 min) ==="
bash "$GEN" "$QEMU_LOG" 2>&1 | tail -12
gen_rc=${PIPESTATUS[0]}
if [ $gen_rc -ne 0 ]; then
  echo "ERROR: gen_coverage.sh failed (rc=$gen_rc) - suite was: $SUITE"
  exit $gen_rc
fi

echo
echo "PIPELINE-DONE: $SUITE - report ready"
echo "report: $OUTDIR/coverage_html/index.html"
