#!/usr/bin/env python3
import argparse
import subprocess
from pathlib import Path

import yaml


def run(command, cwd):
    print("+", " ".join(str(item) for item in command), flush=True)
    subprocess.run(command, cwd=cwd, check=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", default=str(Path(__file__).resolve().parents[2]))
    parser.add_argument("--job", action="append")
    args = parser.parse_args()
    repo = Path(args.repo).resolve()
    matrix = yaml.safe_load((repo / "tests/config_matrix/matrix.yml").read_text())
    selected = set(args.job or [])
    for job in matrix["jobs"]:
        if selected and job["id"] not in selected:
            continue
        build = repo / f"build-matrix-{job['id']}"
        configure = [
            "cmake",
            "-S",
            str(repo),
            "-B",
            str(build),
            f"-DLITECRAB_PROFILE={job['profile']}",
            f"-DLITECRAB_BUILD_TESTS={'ON' if job.get('tests') else 'OFF'}",
        ]
        if job.get("fragment"):
            configure.append(f"-DLITECRAB_CONFIG_FRAGMENT={repo / job['fragment']}")
        run(configure, repo)
        run(["cmake", "--build", str(build), "--target", job.get("target", "all"), "-j2"], repo)
        if job.get("tests"):
            run(["ctest", "--test-dir", str(build), "--output-on-failure"], repo)
        artifact = build / (
            "liblitecrab_agent.a" if job["profile"] == "robot_hi3516" else "litecrab_server"
        )
        if job.get("reject_symbols"):
            command = ["python3", str(repo / "tests/config_matrix/check_symbols.py"), str(artifact)]
            for symbol in job["reject_symbols"]:
                command.extend(["--reject", symbol])
            run(command, repo)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
