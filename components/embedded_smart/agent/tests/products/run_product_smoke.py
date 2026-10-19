#!/usr/bin/env python3
import argparse
import json
import subprocess
import tempfile
from pathlib import Path

import yaml


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", default=str(Path(__file__).resolve().parents[2]))
    parser.add_argument("--product", required=True)
    parser.add_argument("--spec", required=True)
    args = parser.parse_args()
    repo = Path(args.repo).resolve()
    spec = yaml.safe_load(Path(args.spec).read_text(encoding="utf-8"))
    with tempfile.TemporaryDirectory() as directory:
        subprocess.run(
            [
                "python3",
                str(repo / "tools/kconfig/configure.py"),
                "--profile",
                args.product,
                "--output-dir",
                directory,
                "--repo-root",
                str(repo),
            ],
            check=True,
        )
        report = json.loads((Path(directory) / "litecrab_capabilities.json").read_text())
    actual = set(report["capabilities"])
    prompt = (repo / f"products/{args.product}/prompt.md").read_text(encoding="utf-8")
    for case in spec.get("cases", []):
        missing = set(case.get("expect_capabilities", [])) - actual
        unexpected = set(case.get("reject_capabilities", [])) & actual
        if missing or unexpected:
            raise SystemExit(
                f"{case['name']}: missing={sorted(missing)} unexpected={sorted(unexpected)}"
            )
        expected_prompt = case.get("expect_prompt_contains")
        if expected_prompt and expected_prompt not in prompt:
            raise SystemExit(f"{case['name']}: prompt does not contain {expected_prompt!r}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
