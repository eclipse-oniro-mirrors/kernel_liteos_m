#!/usr/bin/env python3
import importlib.util
import json
import shutil
import tempfile
import unittest
from pathlib import Path

import yaml


SOURCE_REPO = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "litecrab_configure", SOURCE_REPO / "tools/kconfig/configure.py"
)
configure = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
SPEC.loader.exec_module(configure)


class ConfigureTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.repo = Path(self.temp.name) / "repo"
        for relative in ("Kconfig", "build/capabilities.yml"):
            target = self.repo / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(SOURCE_REPO / relative, target)
        shutil.copytree(SOURCE_REPO / "defconfigs", self.repo / "defconfigs")
        shutil.copytree(SOURCE_REPO / "products", self.repo / "products")
        manifest = yaml.safe_load((self.repo / "build/capabilities.yml").read_text(encoding="utf-8"))
        sources = list(manifest["core_sources"])
        for spec in manifest["platforms"].values():
            sources.extend(spec.get("sources", []))
        for spec in manifest["capabilities"].values():
            sources.extend(spec.get("sources", []))
        for relative in sources:
            target = self.repo / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.touch()

    def tearDown(self):
        self.temp.cleanup()

    def manifest(self):
        return yaml.safe_load((self.repo / "build/capabilities.yml").read_text(encoding="utf-8"))

    def write_manifest(self, value):
        (self.repo / "build/capabilities.yml").write_text(
            yaml.safe_dump(value, sort_keys=False), encoding="utf-8"
        )

    def test_valid_profile_is_deterministic(self):
        first_dir = Path(self.temp.name) / "first"
        second_dir = Path(self.temp.name) / "second"
        first = configure.generate(self.repo, "diagnostic_sd5091", first_dir, False)
        second = configure.generate(self.repo, "diagnostic_sd5091", second_dir, False)
        self.assertEqual(first["config_hash"], second["config_hash"])
        self.assertEqual(
            (first_dir / "litecrab_capabilities.json").read_bytes(),
            (second_dir / "litecrab_capabilities.json").read_bytes(),
        )
        report = json.loads((first_dir / "litecrab_capabilities.json").read_text())
        self.assertEqual(report["product"], "diagnostic_sd5091")
        self.assertIn("alarm", report["capabilities"])

    def test_all_formal_profiles_validate(self):
        for profile in ("diagnostic_sd5091", "robot_hi3516", "energy_inspection"):
            report = configure.generate(
                self.repo, profile, Path(self.temp.name) / profile, True
            )
            self.assertEqual(report["product"], profile)

    def test_fragment_changes_only_capabilities(self):
        fragment = Path(self.temp.name) / "fragment.conf"
        fragment.write_text("# CONFIG_LITECRAB_CAP_ALARM is not set\n", encoding="utf-8")
        report = configure.generate(
            self.repo,
            "diagnostic_sd5091",
            Path(self.temp.name) / "fragment-output",
            True,
            fragment,
        )
        self.assertNotIn("alarm", report["capabilities"])

    def test_undefined_defconfig_symbol_fails(self):
        with (self.repo / "defconfigs/diagnostic_sd5091.defconfig").open("a", encoding="utf-8") as stream:
            stream.write("CONFIG_LITECRAB_CAP_MISSING=y\n")
        with self.assertRaisesRegex(configure.ConfigError, "undefined Kconfig symbol"):
            configure.generate(self.repo, "diagnostic_sd5091", Path(self.temp.name) / "out", True)

    def test_missing_source_fails(self):
        manifest = self.manifest()
        manifest["capabilities"]["alarm"]["sources"].append("src/alarm/missing.c")
        self.write_manifest(manifest)
        with self.assertRaisesRegex(configure.ConfigError, "missing file"):
            configure.generate(self.repo, "diagnostic_sd5091", Path(self.temp.name) / "out", True)

    def test_missing_platform_source_fails(self):
        manifest = self.manifest()
        manifest["platforms"]["posix"]["sources"].append("src/platform/missing.c")
        self.write_manifest(manifest)
        with self.assertRaisesRegex(configure.ConfigError, "platform posix references missing file"):
            configure.generate(self.repo, "diagnostic_sd5091", Path(self.temp.name) / "out", True)

    def test_missing_capability_dependency_fails(self):
        path = self.repo / "defconfigs/diagnostic_sd5091.defconfig"
        text = path.read_text(encoding="utf-8").replace(
            "CONFIG_LITECRAB_CAP_WORKSPACE_TOOLS=y",
            "# CONFIG_LITECRAB_CAP_WORKSPACE_TOOLS is not set",
        )
        path.write_text(text, encoding="utf-8")
        with self.assertRaisesRegex(configure.ConfigError, "requires: workspace_tools"):
            configure.generate(self.repo, "diagnostic_sd5091", Path(self.temp.name) / "out", True)

    def test_conflict_fails(self):
        manifest = self.manifest()
        manifest["capabilities"]["alarm"]["conflicts"] = ["tcp_gateway"]
        self.write_manifest(manifest)
        with self.assertRaisesRegex(configure.ConfigError, "conflicts with"):
            configure.generate(self.repo, "diagnostic_sd5091", Path(self.temp.name) / "out", True)

    def test_dependency_cycle_reports_path(self):
        manifest = self.manifest()
        manifest["capabilities"]["workspace_tools"]["requires"] = ["skills"]
        self.write_manifest(manifest)
        with self.assertRaisesRegex(configure.ConfigError, "dependency cycle"):
            configure.generate(self.repo, "diagnostic_sd5091", Path(self.temp.name) / "out", True)

    def test_path_traversal_fails(self):
        manifest = self.manifest()
        manifest["capabilities"]["alarm"]["sources"] = ["../outside.c"]
        self.write_manifest(manifest)
        with self.assertRaisesRegex(configure.ConfigError, "escapes repository"):
            configure.generate(self.repo, "diagnostic_sd5091", Path(self.temp.name) / "out", True)

    def test_duplicate_yaml_key_fails(self):
        path = self.repo / "build/capabilities.yml"
        path.write_text(path.read_text(encoding="utf-8") + "schema: 1\n", encoding="utf-8")
        with self.assertRaisesRegex(configure.ConfigError, "duplicate YAML key"):
            configure.generate(self.repo, "diagnostic_sd5091", Path(self.temp.name) / "out", True)

    def test_wrong_platform_capability_fails(self):
        manifest = self.manifest()
        manifest["capabilities"]["embedded_agent_api"]["requires_platform"] = ["openssl"]
        self.write_manifest(manifest)
        with self.assertRaisesRegex(configure.ConfigError, "unsupported on hi3516"):
            configure.generate(self.repo, "robot_hi3516", Path(self.temp.name) / "out", True)


if __name__ == "__main__":
    unittest.main()
