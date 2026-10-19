#!/usr/bin/env python3
"""Validate a LiteCrab product profile and emit deterministic build inputs."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import sys
import tempfile
from pathlib import Path
from typing import Any

try:
    import yaml
except ImportError as exc:  # pragma: no cover - exercised by CMake error handling
    raise SystemExit("PyYAML is required; install tools/kconfig/requirements.lock") from exc


class ConfigError(ValueError):
    pass


class UniqueKeyLoader(yaml.SafeLoader):
    pass


def _construct_mapping(loader: UniqueKeyLoader, node: yaml.MappingNode, deep: bool = False) -> dict:
    result: dict[Any, Any] = {}
    for key_node, value_node in node.value:
        key = loader.construct_object(key_node, deep=deep)
        if key in result:
            raise ConfigError(f"duplicate YAML key: {key}")
        result[key] = loader.construct_object(value_node, deep=deep)
    return result


UniqueKeyLoader.add_constructor(
    yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG, _construct_mapping
)


def load_yaml(path: Path) -> dict:
    try:
        with path.open("r", encoding="utf-8") as stream:
            value = yaml.load(stream, Loader=UniqueKeyLoader)
    except (OSError, yaml.YAMLError) as exc:
        raise ConfigError(f"cannot read YAML {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise ConfigError(f"YAML root must be a mapping: {path}")
    return value


def parse_kconfig(path: Path) -> tuple[dict[str, str], list[list[str]]]:
    symbols: dict[str, str] = {}
    choices: list[list[str]] = []
    active_choice: list[str] | None = None
    current: str | None = None
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("choice"):
            if active_choice is not None:
                raise ConfigError(f"nested choice at {path}:{number}")
            active_choice = []
            choices.append(active_choice)
            current = None
            continue
        if line == "endchoice":
            if active_choice is None:
                raise ConfigError(f"unmatched endchoice at {path}:{number}")
            active_choice = None
            current = None
            continue
        match = re.fullmatch(r"config\s+([A-Z][A-Z0-9_]*)", line)
        if match:
            current = match.group(1)
            if current in symbols:
                raise ConfigError(f"duplicate Kconfig symbol {current}")
            symbols[current] = "unknown"
            if active_choice is not None:
                active_choice.append(current)
            continue
        if current and line in {"bool", "string", "int", "hex"}:
            symbols[current] = line
            continue
        if current:
            type_match = re.match(r"(bool|string|int|hex)(?:\s|$)", line)
            if type_match:
                symbols[current] = type_match.group(1)
    unknown = sorted(name for name, kind in symbols.items() if kind == "unknown")
    if unknown:
        raise ConfigError(f"Kconfig symbols without a supported type: {', '.join(unknown)}")
    return symbols, choices


def parse_defconfig(
    path: Path, symbols: dict[str, str], fill_defaults: bool = True
) -> dict[str, Any]:
    values: dict[str, Any] = {}
    assignment = re.compile(r"CONFIG_([A-Z][A-Z0-9_]*)=(.*)")
    disabled = re.compile(r"# CONFIG_([A-Z][A-Z0-9_]*) is not set")
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.strip()
        if not line:
            continue
        match = disabled.fullmatch(line)
        if match:
            name, value = match.group(1), False
        else:
            match = assignment.fullmatch(line)
            if not match:
                if line.startswith("#"):
                    continue
                raise ConfigError(f"invalid defconfig line {path}:{number}: {line}")
            name, raw_value = match.groups()
            if raw_value == "y":
                value = True
            elif raw_value == "n":
                value = False
            elif len(raw_value) >= 2 and raw_value[0] == raw_value[-1] == '"':
                try:
                    value = json.loads(raw_value)
                except json.JSONDecodeError as exc:
                    raise ConfigError(f"invalid quoted value at {path}:{number}") from exc
            elif re.fullmatch(r"-?[0-9]+", raw_value):
                value = int(raw_value)
            else:
                raise ConfigError(f"unsupported value at {path}:{number}: {raw_value}")
        if name not in symbols:
            raise ConfigError(f"undefined Kconfig symbol CONFIG_{name} at {path}:{number}")
        if name in values:
            raise ConfigError(f"duplicate defconfig symbol CONFIG_{name} at {path}:{number}")
        expected = symbols[name]
        if expected == "bool" and not isinstance(value, bool):
            raise ConfigError(f"CONFIG_{name} must be bool")
        if expected == "string" and not isinstance(value, str):
            raise ConfigError(f"CONFIG_{name} must be string")
        values[name] = value
    if fill_defaults:
        for name, kind in symbols.items():
            if name not in values:
                values[name] = False if kind == "bool" else ""
    return values


def _string_list(owner: str, field: str, value: Any) -> list[str]:
    if value is None:
        return []
    if not isinstance(value, list) or any(not isinstance(item, str) or not item for item in value):
        raise ConfigError(f"{owner}.{field} must be a list of non-empty strings")
    if len(value) != len(set(value)):
        raise ConfigError(f"{owner}.{field} contains duplicates")
    return value


def _repo_file(repo: Path, relative: str, owner: str) -> Path:
    candidate = (repo / relative).resolve()
    try:
        candidate.relative_to(repo.resolve())
    except ValueError as exc:
        raise ConfigError(f"{owner} path escapes repository: {relative}") from exc
    if not candidate.is_file():
        raise ConfigError(f"{owner} references missing file: {relative}")
    return candidate


def validate(
    repo: Path,
    profile: str,
    symbols: dict[str, str],
    choices: list[list[str]],
    values: dict[str, Any],
    manifest: dict,
    product: dict,
) -> tuple[str, str, list[str], list[str], list[str]]:
    for choice in choices:
        enabled = [name for name in choice if values.get(name) is True]
        if len(enabled) != 1:
            raise ConfigError(
                "Kconfig choice requires exactly one enabled symbol: " + ", ".join(choice)
            )
    if manifest.get("schema") != 1:
        raise ConfigError("capabilities.yml schema must be 1")
    if product.get("schema") != 1 or product.get("id") != profile:
        raise ConfigError("product schema/id does not match requested profile")
    if values.get("LITECRAB_PRODUCT_ID") != profile:
        raise ConfigError("defconfig product ID does not match requested profile")
    expected_defconfig = f"defconfigs/{profile}.defconfig"
    if product.get("defconfig") != expected_defconfig:
        raise ConfigError(f"product defconfig must be {expected_defconfig}")
    prompt = product.get("prompt")
    if not isinstance(prompt, str):
        raise ConfigError("product prompt must be a repository-relative path")
    _repo_file(repo, prompt, "product")

    platforms = manifest.get("platforms")
    capabilities = manifest.get("capabilities")
    if not isinstance(platforms, dict) or not isinstance(capabilities, dict):
        raise ConfigError("manifest platforms and capabilities must be mappings")
    symbol_owners: dict[str, str] = {}
    selected_platforms: list[str] = []
    for platform_id, spec in platforms.items():
        if not isinstance(platform_id, str) or not isinstance(spec, dict):
            raise ConfigError("invalid platform entry")
        symbol = spec.get("symbol")
        if symbol not in symbols or symbols[symbol] != "bool":
            raise ConfigError(f"platform {platform_id} has undefined bool symbol {symbol}")
        if symbol in symbol_owners:
            raise ConfigError(f"symbol {symbol} is owned by multiple manifest entries")
        symbol_owners[symbol] = f"platform {platform_id}"
        _string_list(f"platform {platform_id}", "provides", spec.get("provides"))
        _string_list(f"platform {platform_id}", "sources", spec.get("sources"))
        if values[symbol]:
            selected_platforms.append(platform_id)
    if len(selected_platforms) != 1:
        raise ConfigError("exactly one manifest platform must be selected")
    platform = selected_platforms[0]
    provided = set(platforms[platform].get("provides", []))

    core_sources = _string_list("manifest", "core_sources", manifest.get("core_sources"))
    source_owner: dict[str, str] = {}
    for source in core_sources:
        normalized = Path(source).as_posix()
        _repo_file(repo, normalized, "core_sources")
        source_owner[normalized] = "core"

    platform_sources = platforms[platform].get("sources", [])
    for source in platform_sources:
        normalized = Path(source).as_posix()
        _repo_file(repo, normalized, f"platform {platform}")
        if normalized in source_owner:
            raise ConfigError(
                f"source {normalized} belongs to both {source_owner[normalized]} and platform {platform}"
            )
        source_owner[normalized] = f"platform {platform}"

    selected: set[str] = set()
    for cap_id, spec in capabilities.items():
        if not re.fullmatch(r"[a-z][a-z0-9_]*", str(cap_id)) or not isinstance(spec, dict):
            raise ConfigError(f"invalid capability entry: {cap_id}")
        symbol = spec.get("symbol")
        if symbol not in symbols or symbols[symbol] != "bool":
            raise ConfigError(f"capability {cap_id} has undefined bool symbol {symbol}")
        if symbol in symbol_owners:
            raise ConfigError(f"symbol {symbol} is owned by multiple manifest entries")
        symbol_owners[symbol] = f"capability {cap_id}"
        for field in ("sources", "requires", "conflicts", "requires_platform", "libraries"):
            _string_list(f"capability {cap_id}", field, spec.get(field))
        adapter = spec.get("adapter")
        if adapter is not None and (not isinstance(adapter, str) or not adapter):
            raise ConfigError(f"capability {cap_id}.adapter must be a non-empty string")
        for source in spec.get("sources", []):
            normalized = Path(source).as_posix()
            _repo_file(repo, normalized, f"capability {cap_id}")
            if normalized in source_owner:
                raise ConfigError(
                    f"source {normalized} belongs to both {source_owner[normalized]} and {cap_id}"
                )
            source_owner[normalized] = cap_id
        if values[symbol]:
            selected.add(cap_id)

    for cap_id, spec in capabilities.items():
        for dependency in spec.get("requires", []):
            if dependency not in capabilities:
                raise ConfigError(f"capability {cap_id} requires unknown capability {dependency}")
        for conflict in spec.get("conflicts", []):
            if conflict not in capabilities:
                raise ConfigError(f"capability {cap_id} conflicts with unknown capability {conflict}")
        if cap_id not in selected:
            continue
        missing = sorted(set(spec.get("requires", [])) - selected)
        if missing:
            raise ConfigError(f"capability {cap_id} requires: {', '.join(missing)}")
        conflict = sorted(set(spec.get("conflicts", [])) & selected)
        if conflict:
            raise ConfigError(f"capability {cap_id} conflicts with: {', '.join(conflict)}")
        missing_features = sorted(set(spec.get("requires_platform", [])) - provided)
        if missing_features:
            raise ConfigError(
                f"capability {cap_id} is unsupported on {platform}; missing platform features: "
                + ", ".join(missing_features)
            )

    ordered: list[str] = []
    temporary: list[str] = []
    permanent: set[str] = set()

    def visit(cap_id: str) -> None:
        if cap_id in permanent:
            return
        if cap_id in temporary:
            start = temporary.index(cap_id)
            raise ConfigError("capability dependency cycle: " + " -> ".join(temporary[start:] + [cap_id]))
        temporary.append(cap_id)
        for dependency in sorted(capabilities[cap_id].get("requires", [])):
            if dependency in selected:
                visit(dependency)
        temporary.pop()
        permanent.add(cap_id)
        ordered.append(cap_id)

    for cap_id in sorted(selected):
        visit(cap_id)

    artifact = "executable" if values["LITECRAB_ARTIFACT_EXECUTABLE"] else "static_library"
    sources = list(core_sources) + list(platform_sources)
    libraries: list[str] = []
    for cap_id in ordered:
        sources.extend(capabilities[cap_id].get("sources", []))
        libraries.extend(capabilities[cap_id].get("libraries", []))
    libraries = sorted(set(libraries))
    return platform, artifact, ordered, sources, libraries


def _config_text(symbols: dict[str, str], values: dict[str, Any]) -> str:
    lines: list[str] = []
    for name in sorted(symbols):
        value = values[name]
        if symbols[name] == "bool":
            lines.append(f"CONFIG_{name}=y" if value else f"# CONFIG_{name} is not set")
        elif isinstance(value, str):
            lines.append(f"CONFIG_{name}={json.dumps(value, ensure_ascii=False)}")
        else:
            lines.append(f"CONFIG_{name}={value}")
    return "\n".join(lines) + "\n"


def _autoconf_text(symbols: dict[str, str], values: dict[str, Any]) -> str:
    lines = ["#ifndef LITECRAB_AUTOCONF_H", "#define LITECRAB_AUTOCONF_H"]
    for name in sorted(symbols):
        value = values[name]
        if value is False or value == "":
            continue
        if value is True:
            value_text = "1"
        elif isinstance(value, str):
            value_text = json.dumps(value, ensure_ascii=False)
        else:
            value_text = str(value)
        lines.append(f"#define CONFIG_{name} {value_text}")
    lines.extend(["#endif", ""])
    return "\n".join(lines)


def _cmake_text(
    manifest: dict,
    platform: str,
    artifact: str,
    selected: list[str],
    sources: list[str],
    libraries: list[str],
) -> str:
    lines = ["# Generated by tools/kconfig/configure.py; do not edit."]
    lines.append(f"set(LITECRAB_PLATFORM {platform})")
    lines.append(f"set(LITECRAB_ARTIFACT {artifact})")
    lines.append("set(LITECRAB_GENERATED_CORE_SOURCES")
    lines.extend(
        f'  "${{CMAKE_CURRENT_SOURCE_DIR}}/{source}"'
        for source in manifest.get("core_sources", [])
    )
    lines.append(")")
    lines.append("set(LITECRAB_GENERATED_PLATFORM_SOURCES")
    lines.extend(
        f'  "${{CMAKE_CURRENT_SOURCE_DIR}}/{source}"'
        for source in manifest["platforms"][platform].get("sources", [])
    )
    lines.append(")")
    lines.append("set(LITECRAB_GENERATED_SOURCES")
    lines.extend(f'  "${{CMAKE_CURRENT_SOURCE_DIR}}/{source}"' for source in sources)
    lines.append(")")
    lines.append("set(LITECRAB_GENERATED_LIBRARIES " + " ".join(libraries) + ")")
    lines.append("set(LITECRAB_SELECTED_CAPABILITIES " + ";".join(selected) + ")")
    for cap_id in selected:
        lines.append(f"set(LITECRAB_CAP_{cap_id.upper()}_ENABLED ON)")
        lines.append(f"set(LITECRAB_CAP_{cap_id.upper()}_SOURCES")
        lines.extend(
            f'  "${{CMAKE_CURRENT_SOURCE_DIR}}/{source}"'
            for source in manifest["capabilities"][cap_id].get("sources", [])
        )
        lines.append(")")
    lines.append("")
    return "\n".join(lines)


def _gni_text(sources: list[str], libraries: list[str]) -> str:
    lines = ["# Generated by tools/kconfig/configure.py; do not edit.", "litecrab_sources = ["]
    lines.extend(f'  "//third_party/litecrab/{source}",' for source in sources)
    lines.extend(["]", "litecrab_system_libraries = ["])
    lines.extend(f'  "{library}",' for library in libraries)
    lines.extend(["]", ""])
    return "\n".join(lines)


def _registry_text(manifest: dict, selected: list[str]) -> str:
    adapters = [manifest["capabilities"][item].get("adapter") for item in selected]
    adapters = [item for item in adapters if item]
    lines = ["/* Generated by tools/kconfig/configure.py; do not edit. */"]
    lines.extend(f"extern const LiteCrabCapabilityOps {adapter};" for adapter in adapters)
    lines.append("static const LiteCrabCapabilityOps *const kLiteCrabGeneratedCapabilities[] = {")
    lines.extend(f"    &{adapter}," for adapter in adapters)
    lines.extend(["};", f"#define LITECRAB_GENERATED_CAPABILITY_COUNT {len(adapters)}u", ""])
    return "\n".join(lines)


def _prompt_c_text(prompt: str) -> str:
    encoded = prompt.encode("utf-8")
    chunks = [encoded[index : index + 16] for index in range(0, len(encoded), 16)]
    lines = ["/* Generated product prompt; do not edit. */", "#include <stddef.h>"]
    lines.append("const unsigned char litecrab_generated_product_prompt[] = {")
    lines.extend("    " + ", ".join(f"0x{byte:02x}" for byte in chunk) + "," for chunk in chunks)
    lines.extend(
        [
            "    0x00,",
            "};",
            f"const size_t litecrab_generated_product_prompt_size = {len(encoded)}u;",
            "",
        ]
    )
    return "\n".join(lines)


def generate(
    repo: Path,
    profile: str,
    output_dir: Path,
    check_only: bool,
    fragment: Path | None = None,
) -> dict:
    symbols, choices = parse_kconfig(repo / "Kconfig")
    defconfig = repo / "defconfigs" / f"{profile}.defconfig"
    product_path = repo / "products" / profile / "product.yml"
    if not defconfig.is_file() or not product_path.is_file():
        raise ConfigError(f"unknown or incomplete profile: {profile}")
    values = parse_defconfig(defconfig, symbols)
    if fragment:
        overrides = parse_defconfig(fragment, symbols, fill_defaults=False)
        protected = {
            "LITECRAB_PRODUCT_ID",
            "LITECRAB_PLATFORM_POSIX",
            "LITECRAB_PLATFORM_HI3516",
            "LITECRAB_ARTIFACT_EXECUTABLE",
            "LITECRAB_ARTIFACT_STATIC_LIBRARY",
        }
        changed_protected = sorted(protected & overrides.keys())
        if changed_protected:
            raise ConfigError(
                "config fragments cannot override product/platform/artifact symbols: "
                + ", ".join(changed_protected)
            )
        values.update(overrides)
    manifest = load_yaml(repo / "build" / "capabilities.yml")
    product = load_yaml(product_path)
    platform, artifact, selected, sources, libraries = validate(
        repo, profile, symbols, choices, values, manifest, product
    )
    config_text = _config_text(symbols, values)
    digest_input = {
        "config": config_text,
        "manifest": manifest,
        "product": product,
        "prompt_sha256": hashlib.sha256(
            (repo / product["prompt"]).read_bytes()
        ).hexdigest(),
    }
    config_hash = hashlib.sha256(
        json.dumps(digest_input, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode()
    ).hexdigest()
    report = {
        "schema": 1,
        "product": profile,
        "platform": platform,
        "artifact": artifact,
        "artifact_name": product.get("artifact_name", "litecrab"),
        "capabilities": selected,
        "sources": sources,
        "libraries": libraries,
        "config_hash": config_hash,
    }
    if check_only:
        return report

    output_dir = output_dir.resolve()
    output_dir.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=f".{output_dir.name}.", dir=output_dir.parent))
    try:
        files = {
            ".config": config_text,
            "include/litecrab/autoconf.h": _autoconf_text(symbols, values),
            "litecrab_sources.cmake": _cmake_text(
                manifest, platform, artifact, selected, sources, libraries
            ),
            "litecrab_sources.gni": _gni_text(sources, libraries),
            "generated_capability_registry.inc": _registry_text(manifest, selected),
            "litecrab_capabilities.json": json.dumps(
                report, ensure_ascii=False, indent=2, sort_keys=True
            )
            + "\n",
            "generated_product_prompt.c": _prompt_c_text(
                (repo / product["prompt"]).read_text(encoding="utf-8")
            ),
        }
        for relative, content in files.items():
            target = staging / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(content, encoding="utf-8", newline="\n")
        if output_dir.exists():
            shutil.rmtree(output_dir)
        os.replace(staging, output_dir)
    finally:
        if staging.exists():
            shutil.rmtree(staging)
    return report


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--profile", required=True)
    parser.add_argument("--output-dir")
    parser.add_argument("--repo-root", default=str(Path(__file__).resolve().parents[2]))
    parser.add_argument("--fragment")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if not args.check and not args.output_dir:
        parser.error("--output-dir is required unless --check is used")
    try:
        report = generate(
            Path(args.repo_root).resolve(),
            args.profile,
            Path(args.output_dir).resolve() if args.output_dir else Path.cwd() / "generated",
            args.check,
            Path(args.fragment).resolve() if args.fragment else None,
        )
    except (ConfigError, OSError) as exc:
        print(f"configure error: {exc}", file=sys.stderr)
        return 2
    print(
        f"LiteCrab profile={report['product']} platform={report['platform']} "
        f"artifact={report['artifact']} capabilities={','.join(report['capabilities'])} "
        f"hash={report['config_hash']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
