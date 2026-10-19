#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build_dir="${1:-${repo_dir}/build-baseline}"
output_file="${2:-${build_dir}/litecrab-baseline.json}"

cmake -S "${repo_dir}" -B "${build_dir}" -DLITECRAB_BUILD_TESTS=ON
cmake --build "${build_dir}" -j2
ctest --test-dir "${build_dir}" --output-on-failure
test_count="$(ctest --test-dir "${build_dir}" -N | sed -n 's/^Total Tests: //p')"

binary="${build_dir}/litecrab_server"
library="${build_dir}/liblitecrab.a"
revision="$(git -C "${repo_dir}" rev-parse HEAD 2>/dev/null || printf unknown)"
compiler="$(cc --version | sed -n '1p')"
cmake_version="$(cmake --version | sed -n '1p')"
sha256="$(sha256sum "${binary}" | cut -d' ' -f1)"
read -r text_size data_size bss_size _ < <(size "${binary}" | sed -n '2p')
symbol_count="$(nm -g --defined-only "${library}" | sed -n 's/.* [TDBR] //p' | sort -u | wc -l)"
dependencies="$(ldd "${binary}" | sed -n 's/^[[:space:]]*\([^[:space:]]*\).*/\1/p' | sort -u | paste -sd, -)"

mkdir -p "$(dirname "${output_file}")"
python3 - "${output_file}" "${revision}" "${compiler}" "${cmake_version}" \
  "${sha256}" "${text_size}" "${data_size}" "${bss_size}" "${symbol_count}" \
  "${dependencies}" "${test_count}" <<'PY'
import json
import sys

path, revision, compiler, cmake, sha256, text, data, bss, symbols, dependencies, tests = sys.argv[1:]
report = {
    "schema": 1,
    "source_revision": revision,
    "toolchain": {"compiler": compiler, "cmake": cmake},
    "profile": "legacy_diagnostic_full",
    "tests": {"passed": int(tests), "failed": 0},
    "artifacts": {
        "litecrab_server": {
            "sha256": sha256,
            "text": int(text),
            "data": int(data),
            "bss": int(bss),
            "dynamic_dependencies": [item for item in dependencies.split(",") if item],
        },
        "liblitecrab.a": {"public_symbol_count": int(symbols)},
    },
}
with open(path, "w", encoding="utf-8", newline="\n") as stream:
    json.dump(report, stream, ensure_ascii=False, indent=2, sort_keys=True)
    stream.write("\n")
PY

printf 'Baseline report: %s\n' "${output_file}"
