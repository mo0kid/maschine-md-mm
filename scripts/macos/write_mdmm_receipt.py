#!/usr/bin/env python3

from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import math
import os
import pathlib
import re
import shutil
import statistics
import subprocess
from typing import Optional


RELEASE_ROOT_MARKER = ".gearmulator-mdmm-release-root"
RELEASE_ROOT_MARKER_VERSION = "gearmulator-mdmm-release-root-v1"
FIRMWARE_SHA256 = {
    "MD": "68542e30917b9918ccaee2b2237df62c8a00479938680b85aca93ce4fbca44c8",
    "MM": "369849175602e20a9dd2b6e0ad8ac404b76f82718b14afbf1cbc01b7acabec7e",
}
SHA256_PATTERN = re.compile(r"[0-9a-f]{64}")
CORE_CAPACITY_SCHEMA = "gearmulator-mdmm-core-capacity-v1"
CORE_CAPACITY_SCOPE = "executed VST3 output-only core callback duration"
CORE_CAPACITY_LIMIT = 0.90
MACOS_DEPLOYMENT_TARGET = "10.13"
SUPPORTED_MACOS_ARCHITECTURES = ("arm64", "x86_64")
CORE_CAPACITY_WORKLOAD = {
    "plugin_format": "VST3",
    "audio_input_channels": 0,
    "audio_output_channels": 2,
    "scenario": "notes",
    "fixed_block_size": True,
    "note_phase": 127,
    "message_loop": True,
    "capacity_mode": "unpaced offline render",
    "paced_mode": "paced render",
}


def release_selection(
    architectures_value: str,
    pgo_mode: str,
    pgo_profile: pathlib.Path | None,
    pgo_provenance: pathlib.Path | None,
    require_pgo_files: bool = True,
) -> dict[str, object]:
    requested = tuple(architectures_value.split(";"))
    if (
        not requested
        or any(not architecture for architecture in requested)
        or len(set(requested)) != len(requested)
        or any(
            architecture not in SUPPORTED_MACOS_ARCHITECTURES
            for architecture in requested
        )
    ):
        raise RuntimeError(
            "GEARMULATOR_MDMM_MACOS_ARCHITECTURES must be arm64, x86_64, "
            "or arm64;x86_64"
        )
    architectures = tuple(
        architecture
        for architecture in SUPPORTED_MACOS_ARCHITECTURES
        if architecture in requested
    )
    if pgo_mode not in {"none", "use"}:
        raise RuntimeError(
            "GEARMULATOR_MDMM_APPLE_PGO_MODE must be none or use for product builds"
        )
    if pgo_mode == "use":
        if len(architectures) != 1:
            raise RuntimeError(
                "PGO product builds require exactly one macOS architecture"
            )
        if pgo_profile is None or pgo_provenance is None:
            raise RuntimeError(
                "PGO product builds require both profile and provenance paths"
            )
        if require_pgo_files:
            if not pgo_profile.is_file():
                raise RuntimeError(f"PGO profile does not exist: {pgo_profile}")
            if not pgo_provenance.is_file():
                raise RuntimeError(f"PGO provenance does not exist: {pgo_provenance}")
    elif pgo_profile is not None or pgo_provenance is not None:
        raise RuntimeError("PGO inputs were supplied while PGO mode is none")

    architecture_label = "Universal" if len(architectures) == 2 else architectures[0]
    pgo_suffix = "-PGO" if pgo_mode == "use" else ""
    return {
        "architectures": architectures,
        "cmake_architectures": ";".join(architectures),
        "pgo_mode": pgo_mode,
        "package_name": f"Maschine-MD-MM-macOS-{architecture_label}{pgo_suffix}",
    }


def _cmake_bool(value: str, name: str) -> bool:
    normalized = value.upper()
    if normalized in {"1", "ON", "TRUE", "YES", "Y"}:
        return True
    if normalized in {"0", "OFF", "FALSE", "NO", "N", "", "NOTFOUND"}:
        return False
    raise RuntimeError(f"invalid CMake boolean for {name}: {value}")


def read_cmake_cache(path: pathlib.Path) -> dict[str, str]:
    path = path.resolve()
    if not path.is_file():
        raise RuntimeError(f"CMake cache does not exist: {path}")
    values: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8", errors="surrogateescape").splitlines():
        if not line or line.startswith(("#", "//")) or "=" not in line:
            continue
        key_and_type, value = line.split("=", 1)
        if ":" not in key_and_type:
            continue
        key, _ = key_and_type.split(":", 1)
        values[key] = value
    return values


def read_cmake_compiler_identity(cache_path: pathlib.Path) -> dict[str, str]:
    metadata_files = list(
        cache_path.resolve().parent.glob("CMakeFiles/*/CMakeCXXCompiler.cmake")
    )
    if len(metadata_files) != 1:
        raise RuntimeError(
            "expected one CMake C++ compiler identity file next to the release cache; "
            f"found {len(metadata_files)}"
        )
    text = metadata_files[0].read_text(encoding="utf-8", errors="replace")
    values = {}
    for name in ("ID", "VERSION"):
        match = re.search(
            rf'^set\(CMAKE_CXX_COMPILER_{name} "([^"]+)"\)$', text, re.MULTILINE
        )
        if match is None:
            raise RuntimeError(f"CMake compiler metadata has no C++ compiler {name.lower()}")
        values[name.lower()] = match.group(1)
    return values


def release_optimization(
    cache_path: pathlib.Path,
    expected_architectures: tuple[str, ...] = (),
    expected_products_root: pathlib.Path | None = None,
) -> dict[str, object]:
    cache_path = cache_path.resolve()
    cache = read_cmake_cache(cache_path)
    compiler = read_cmake_compiler_identity(cache_path)
    if compiler["id"] not in {"AppleClang", "Clang"}:
        raise RuntimeError(
            f"MD/MM Apple release optimization requires Clang; found {compiler['id']}"
        )

    configuration = cache.get("CMAKE_BUILD_TYPE", "")
    if configuration != "Release":
        raise RuntimeError(
            f"MD/MM release artifacts require CMAKE_BUILD_TYPE=Release; found {configuration!r}"
        )

    deployment_target = cache.get("CMAKE_OSX_DEPLOYMENT_TARGET", "")
    if deployment_target != MACOS_DEPLOYMENT_TARGET:
        raise RuntimeError(
            "MD/MM release artifacts require macOS deployment target "
            f"{MACOS_DEPLOYMENT_TARGET}; found {deployment_target!r}"
        )

    configured_products = cache.get("GEARMULATOR_JUCE_PRODUCTS_ROOT", "")
    if not configured_products:
        raise RuntimeError("release build did not set a build-local JUCE products root")
    products_root = pathlib.Path(configured_products).resolve()
    required_products_root = (
        expected_products_root.resolve()
        if expected_products_root is not None
        else cache_path.parent / "products"
    )
    if products_root != required_products_root:
        raise RuntimeError(
            "release JUCE products root is not owned by the build directory: "
            f"expected {required_products_root}, found {products_root}"
        )

    architectures = tuple(
        architecture
        for architecture in cache.get("CMAKE_OSX_ARCHITECTURES", "").split(";")
        if architecture
    )
    if not architectures:
        raise RuntimeError("CMAKE_OSX_ARCHITECTURES must explicitly name each release architecture")
    if len(set(architectures)) != len(architectures):
        raise RuntimeError(f"duplicate CMAKE_OSX_ARCHITECTURES entries: {architectures}")
    if expected_architectures and set(architectures) != set(expected_architectures):
        raise RuntimeError(
            "release architecture mismatch: "
            f"expected {sorted(expected_architectures)}, found {sorted(architectures)}"
        )

    thinlto = _cmake_bool(
        cache.get("GEARMULATOR_MDMM_APPLE_THINLTO", "OFF"),
        "GEARMULATOR_MDMM_APPLE_THINLTO",
    )
    optimize_dsp = _cmake_bool(
        cache.get("GEARMULATOR_MDMM_APPLE_OPTIMIZE_DSP", "OFF"),
        "GEARMULATOR_MDMM_APPLE_OPTIMIZE_DSP",
    )
    if not thinlto or not optimize_dsp:
        raise RuntimeError(
            "MD/MM macOS release artifacts require ThinLTO for the MCU and DSP cores "
            "(GEARMULATOR_MDMM_APPLE_THINLTO=ON and "
            "GEARMULATOR_MDMM_APPLE_OPTIMIZE_DSP=ON)"
        )

    required_targets = {"mdLib", "68kEmu", "dsp56kEmu", "dsp56kBase"}
    applied_targets = {
        target
        for target in cache.get(
            "GEARMULATOR_MDMM_APPLE_OPTIMIZATION_APPLIED_TARGETS", ""
        ).split(";")
        if target
    }
    if applied_targets != required_targets:
        raise RuntimeError(
            "generated build did not apply MD/MM Apple optimization to every MCU/DSP "
            f"target: expected {sorted(required_targets)}, found {sorted(applied_targets)}"
        )

    pgo_mode = cache.get("GEARMULATOR_MDMM_APPLE_PGO_MODE", "none")
    if pgo_mode not in {"none", "use"}:
        raise RuntimeError(
            f"release artifacts require PGO mode none or use; found {pgo_mode!r}"
        )
    applied_pgo_mode = cache.get(
        "GEARMULATOR_MDMM_APPLE_OPTIMIZATION_APPLIED_PGO_MODE", ""
    )
    if applied_pgo_mode != pgo_mode:
        raise RuntimeError(
            f"configured PGO mode {pgo_mode!r} was not applied; found {applied_pgo_mode!r}"
        )
    profile_sha256 = None
    if pgo_mode == "use":
        if len(architectures) != 1:
            raise RuntimeError(
                "one PGO profile cannot qualify a universal build; build and record each "
                "architecture separately"
            )
        profile = pathlib.Path(cache.get("GEARMULATOR_MDMM_APPLE_PGO_PROFILE", ""))
        if not profile.is_file():
            raise RuntimeError(f"configured PGO profile does not exist: {profile}")
        profile_sha256 = sha256(profile)
        applied_profile_sha256 = cache.get(
            "GEARMULATOR_MDMM_APPLE_OPTIMIZATION_APPLIED_PROFILE_SHA256", ""
        )
        if applied_profile_sha256 != profile_sha256:
            raise RuntimeError(
                "generated build did not apply the configured PGO profile SHA-256"
            )
    elif cache.get(
        "GEARMULATOR_MDMM_APPLE_OPTIMIZATION_APPLIED_PROFILE_SHA256", ""
    ):
        raise RuntimeError("non-PGO release build retained an applied profile marker")

    slices = {}
    for architecture in sorted(architectures):
        slices[architecture] = {
            "thinlto": True,
            "dsp_optimization": True,
            "pgo_mode": pgo_mode,
            "profile_sha256": profile_sha256,
        }
    return {
        "compiler": compiler,
        "deployment_target": deployment_target,
        "products_build_local": True,
        "slices": slices,
    }


def binary_flag(value: str) -> bool:
    if value == "0":
        return False
    if value == "1":
        return True
    raise argparse.ArgumentTypeError("expected 0 or 1")


def git(repo: pathlib.Path, *args: str) -> str:
    return subprocess.check_output(["git", "-C", str(repo), *args], text=True).strip()


def _git_paths(source: pathlib.Path) -> tuple[tuple[int, pathlib.PurePosixPath], ...]:
    records = subprocess.check_output(
        ["git", "-C", str(source), "ls-files", "--stage", "-z"]
    ).split(b"\0")
    result = []
    for record in records:
        if not record:
            continue
        metadata, raw_path = record.split(b"\t", 1)
        mode = int(metadata.split(b" ", 1)[0], 8)
        path = pathlib.PurePosixPath(raw_path.decode("utf-8", errors="surrogateescape"))
        result.append((mode, path))
    return tuple(result)


def validate_cleanup_root(
    source: pathlib.Path, candidate: pathlib.Path, label: str
) -> pathlib.Path:
    source = source.resolve()
    lexical_root = pathlib.Path(os.path.abspath(os.fspath(candidate)))
    root = candidate.resolve()
    tracked_paths = _git_paths(source)
    lexical_inside_source = False
    for location in dict.fromkeys((lexical_root, root)):
        if location == source or location in source.parents:
            raise RuntimeError(f"unsafe {label} contains the source tree: {location}")

        try:
            relative = location.relative_to(source)
        except ValueError:
            continue
        if location == lexical_root:
            lexical_inside_source = True

        if relative.parts and relative.parts[0] == ".git":
            raise RuntimeError(f"unsafe {label} overlaps Git metadata: {location}")

        relative_git = pathlib.PurePosixPath(relative.as_posix())
        for mode, tracked in tracked_paths:
            if tracked == relative_git or relative_git in tracked.parents:
                raise RuntimeError(f"unsafe {label} contains tracked path: {tracked}")
            if mode == 0o160000 and tracked in relative_git.parents:
                raise RuntimeError(f"unsafe {label} is inside tracked submodule: {tracked}")
    if lexical_inside_source and lexical_root != root:
        raise RuntimeError(f"unsafe {label} follows a source-tree symlink: {lexical_root}")
    return root


def validate_release_directories(
    source: pathlib.Path, build: pathlib.Path, output: pathlib.Path
) -> tuple[pathlib.Path, pathlib.Path]:
    build = validate_cleanup_root(source, build, "build directory")
    output = validate_cleanup_root(source, output, "output directory")
    if build == output or build in output.parents or output in build.parents:
        raise RuntimeError(
            f"build and output directories must not overlap: {build}, {output}"
        )
    return build, output


def _release_root_marker_contents(root: pathlib.Path) -> str:
    return f"{RELEASE_ROOT_MARKER_VERSION}\n{root}\n"


def _require_owned_or_absent(root: pathlib.Path, label: str) -> None:
    if not root.exists():
        return
    if not root.is_dir():
        raise RuntimeError(f"refusing existing non-directory {label}: {root}")
    marker = root / RELEASE_ROOT_MARKER
    if not marker.is_file():
        raise RuntimeError(
            f"refusing existing unowned {label}: {root} "
            f"(missing {RELEASE_ROOT_MARKER})"
        )
    expected = _release_root_marker_contents(root)
    if marker.read_text(encoding="utf-8") != expected:
        raise RuntimeError(f"refusing {label} with invalid ownership marker: {root}")


def _reset_owned_root(root: pathlib.Path) -> None:
    if not root.exists():
        root.mkdir(parents=True)
        (root / RELEASE_ROOT_MARKER).write_text(
            _release_root_marker_contents(root), encoding="utf-8"
        )
        return

    for child in root.iterdir():
        if child.name == RELEASE_ROOT_MARKER:
            continue
        if child.is_symlink() or child.is_file():
            child.unlink()
        else:
            shutil.rmtree(child)


def prepare_release_directories(
    source: pathlib.Path, build: pathlib.Path, output: pathlib.Path
) -> tuple[pathlib.Path, pathlib.Path]:
    build, output = validate_release_directories(source, build, output)
    # Check ownership of both roots before deleting anything from either one.
    _require_owned_or_absent(build, "build directory")
    _require_owned_or_absent(output, "output directory")
    _reset_owned_root(build)
    _reset_owned_root(output)
    return build, output


def require_clean(
    source: pathlib.Path,
    include_untracked: bool,
    allowed_untracked_roots: tuple[pathlib.Path, ...] = (),
) -> None:
    status = git(
        source,
        "status",
        "--porcelain=v1",
        "--untracked-files=no",
        "--ignore-submodules=none",
    )
    if status:
        raise RuntimeError(f"refusing dirty source tree:\n{status}")
    if not include_untracked:
        return

    source = source.resolve()
    allowed = []
    for candidate in allowed_untracked_roots:
        root = candidate.resolve()
        if root == source or root in source.parents:
            raise RuntimeError(f"allowed untracked root contains source tree: {root}")
        try:
            root.relative_to(source)
        except ValueError:
            continue
        allowed.append(root)

    untracked = subprocess.check_output(
        ["git", "-C", str(source), "ls-files", "--others", "--exclude-standard", "-z"]
    ).split(b"\0")
    unexpected = []
    for raw_path in untracked:
        if not raw_path:
            continue
        relative = pathlib.PurePosixPath(raw_path.decode("utf-8", errors="surrogateescape"))
        if relative.is_absolute() or ".." in relative.parts:
            raise RuntimeError(f"unsafe untracked Git path: {relative}")
        path = source.joinpath(*relative.parts)
        if any(path == root or root in path.parents for root in allowed):
            continue
        unexpected.append(relative.as_posix())
    if unexpected:
        raise RuntimeError(
            "refusing untracked source files:\n" + "\n".join(f"?? {path}" for path in unexpected)
        )


def submodule_commit(source: pathlib.Path, name: str) -> str:
    relative = pathlib.PurePosixPath(
        git(source, "config", "-f", ".gitmodules", "--get", f"submodule.{name}.path")
    )
    if relative.is_absolute() or ".." in relative.parts:
        raise RuntimeError(f"unsafe path for submodule {name}: {relative}")

    checkout = (source / pathlib.Path(*relative.parts)).resolve()
    try:
        checkout.relative_to(source)
    except ValueError as error:
        raise RuntimeError(f"submodule {name} resolves outside the source tree: {checkout}") from error

    expected = git(source, "rev-parse", f"HEAD:{relative.as_posix()}")
    actual_root = pathlib.Path(git(checkout, "rev-parse", "--show-toplevel")).resolve()
    if actual_root != checkout:
        raise RuntimeError(
            f"submodule {name} is not initialized at {relative} "
            f"(Git resolved it to {actual_root})"
        )

    actual = git(checkout, "rev-parse", "HEAD")
    if actual != expected:
        raise RuntimeError(
            f"submodule {name} checkout does not match the parent gitlink: "
            f"expected {expected}, found {actual}"
        )
    return actual


def source_tuple(source: pathlib.Path) -> dict[str, str]:
    return {
        "source_commit": git(source, "rev-parse", "HEAD"),
        "dsp56300_commit": submodule_commit(source, "source/dsp56300"),
        "mc68k_commit": submodule_commit(source, "source/mc68k"),
        "juce_commit": submodule_commit(source, "source/JUCE"),
    }


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def bundle_module(bundle: pathlib.Path) -> pathlib.Path:
    candidates = [path for path in (bundle / "Contents" / "MacOS").iterdir() if path.is_file()]
    if len(candidates) != 1:
        raise RuntimeError(f"expected one executable module in {bundle}, found {len(candidates)}")
    return candidates[0]


def _require_sha256(value: object, label: str) -> None:
    if not isinstance(value, str) or SHA256_PATTERN.fullmatch(value) is None:
        raise RuntimeError(f"{label} is not a SHA-256 digest")


def _finite_number(value: object, label: str) -> float:
    try:
        number = float(value)
    except (TypeError, ValueError) as error:
        raise RuntimeError(f"{label} is not numeric") from error
    if not math.isfinite(number):
        raise RuntimeError(f"{label} is non-finite")
    return number


def validate_core_capacity_check(
    check: dict[str, object],
    artifact_hashes: dict[str, str],
    optimization: dict[str, object],
) -> dict[str, object]:
    if check.get("schema") != CORE_CAPACITY_SCHEMA:
        raise RuntimeError("unknown core-capacity check schema")
    if check.get("scope") != CORE_CAPACITY_SCOPE:
        raise RuntimeError("core-capacity check has the wrong scope")
    if check.get("core_microgate_passed") is not True:
        raise RuntimeError("output-only core-capacity microgate did not pass")
    if (
        check.get("sample_rate") != 48000
        or check.get("block_size") != 128
        or check.get("seconds") != 20
        or check.get("warm_start_seconds") != 12
        or check.get("repetitions") != {"capacity": 3, "paced": 3}
        or check.get("workload") != CORE_CAPACITY_WORKLOAD
    ):
        raise RuntimeError("core-capacity check does not match the fixed release workload")
    period_ms = _finite_number(check.get("period_ms"), "core-capacity period")
    if not math.isclose(period_ms, 128 * 1000.0 / 48000, rel_tol=0, abs_tol=1e-12):
        raise RuntimeError("core-capacity check has the wrong callback period")

    host_architecture = check.get("host_architecture")
    slices = optimization.get("slices")
    if not isinstance(slices, dict) or host_architecture not in slices:
        raise RuntimeError(
            f"core-capacity host architecture is not packaged: {host_architecture!r}"
        )
    if check.get("host_name") != "latency_host" or not check.get("host_os"):
        raise RuntimeError("core-capacity check has an invalid host identity")
    _require_sha256(check.get("host_sha256"), "core-capacity host")
    if check.get("firmware_sha256") != FIRMWARE_SHA256:
        raise RuntimeError("core-capacity check did not use the pinned firmware images")

    qualified_hashes = check.get("plugin_module_sha256")
    if not isinstance(qualified_hashes, dict):
        raise RuntimeError("core-capacity check has no plug-in module hashes")
    expected_hashes = {
        name: artifact_hashes[name]
        for name in ("Gearmulator MD.vst3", "Gearmulator MM.vst3")
        if name in artifact_hashes
    }
    if qualified_hashes != expected_hashes:
        raise RuntimeError(
            "core-capacity plug-ins do not match the packaged VST3 modules"
        )
    for name, digest in qualified_hashes.items():
        _require_sha256(digest, f"{name} module")

    models = check.get("models")
    if not isinstance(models, dict) or set(models) != {"MD", "MM"}:
        raise RuntimeError("core-capacity check must cover MD and MM")
    paced_tail_by_model = {}
    for model_name, model in models.items():
        if not isinstance(model, dict):
            raise RuntimeError(f"invalid {model_name} core-capacity check")
        capacity_runs = model.get("capacity_runs")
        paced_runs = model.get("paced_runs")
        gate = model.get("gate")
        if (
            not isinstance(capacity_runs, list)
            or len(capacity_runs) != 3
            or not isinstance(paced_runs, list)
            or len(paced_runs) != 3
            or not isinstance(gate, dict)
        ):
            raise RuntimeError(
                f"{model_name} core-capacity check requires exactly three capacity and paced runs"
            )
        for mode, runs in (("capacity", capacity_runs), ("paced", paced_runs)):
            for index, run in enumerate(runs, 1):
                if not isinstance(run, dict):
                    raise RuntimeError(f"invalid {model_name} {mode} run {index}")
                if (
                    run.get("total_callbacks") != 7500
                    or run.get("total_samples") != 960000
                    or run.get("warm_callbacks") != 3000
                ):
                    raise RuntimeError(
                        f"{model_name} {mode} run {index} has the wrong capture length"
                    )
                for field in (
                    "p50_ms",
                    "p99_ms",
                    "max_ms",
                    "p50_budget_fraction",
                    "p99_budget_fraction",
                    "max_budget_fraction",
                    "over_budget_fraction",
                    "completion_after_deadline_fraction",
                    "audio_peak_before_quantization",
                ):
                    if _finite_number(run.get(field), f"{model_name} {mode} {field}") < 0:
                        raise RuntimeError(f"{model_name} {mode} {field} is negative")
                _finite_number(
                    run.get("scheduler_late_p99_ms"),
                    f"{model_name} {mode} scheduler_late_p99_ms",
                )
                if float(run["audio_peak_before_quantization"]) <= 1e-5:
                    raise RuntimeError(f"{model_name} {mode} run produced no audible output")
                for duration_field, fraction_field in (
                    ("p50_ms", "p50_budget_fraction"),
                    ("p99_ms", "p99_budget_fraction"),
                    ("max_ms", "max_budget_fraction"),
                ):
                    if not math.isclose(
                        float(run[fraction_field]),
                        float(run[duration_field]) / period_ms,
                        rel_tol=0,
                        abs_tol=1e-9,
                    ):
                        raise RuntimeError(
                            f"{model_name} {mode} duration fractions are inconsistent"
                        )
                p50_ms = float(run["p50_ms"])
                p99_ms = float(run["p99_ms"])
                max_ms = float(run["max_ms"])
                if not 0 <= p50_ms <= p99_ms <= max_ms:
                    raise RuntimeError(
                        f"{model_name} {mode} duration quantiles are inconsistent"
                    )
                over_budget = run.get("over_budget")
                completed_late = run.get("completion_after_deadline")
                if (
                    not isinstance(over_budget, int)
                    or not 0 <= over_budget <= 3000
                    or not isinstance(completed_late, int)
                    or not 0 <= completed_late <= 3000
                    or not math.isclose(
                        float(run["over_budget_fraction"]),
                        over_budget / 3000,
                        rel_tol=0,
                        abs_tol=1e-12,
                    )
                    or not math.isclose(
                        float(run["completion_after_deadline_fraction"]),
                        completed_late / 3000,
                        rel_tol=0,
                        abs_tol=1e-12,
                    )
                ):
                    raise RuntimeError(f"{model_name} {mode} run counts are inconsistent")
                if (max_ms > period_ms) is not (over_budget > 0):
                    raise RuntimeError(
                        f"{model_name} {mode} maximum and render-overrun count are inconsistent"
                    )
                capture_hashes = run.get("capture_sha256")
                required_captures = {
                    "capture.blocks.csv",
                    "capture.json",
                    "capture.wav",
                    "host.log",
                }
                if not isinstance(capture_hashes, dict) or set(capture_hashes) != required_captures:
                    raise RuntimeError(f"{model_name} {mode} run has incomplete capture hashes")
                for capture_name, digest in capture_hashes.items():
                    _require_sha256(digest, f"{model_name} {mode} {capture_name}")

        capacity_limit = _finite_number(gate.get("capacity_limit"), "capacity limit")
        capacity_value = statistics.median(
            float(run["p50_budget_fraction"]) for run in capacity_runs
        )
        recorded_capacity = _finite_number(
            gate.get("capacity_median_p50_budget_fraction"), "capacity median"
        )
        if (
            gate.get("core_microgate_passed") is not True
            or capacity_limit > CORE_CAPACITY_LIMIT
            or capacity_value > capacity_limit
            or not math.isclose(recorded_capacity, capacity_value, rel_tol=0, abs_tol=1e-12)
        ):
            raise RuntimeError(
                f"{model_name} output-only core microgate is missing or weaker than release policy"
            )

        paced_p99 = statistics.median(
            float(run["p99_budget_fraction"]) for run in paced_runs
        )
        paced_over = statistics.median(
            float(run["over_budget_fraction"]) for run in paced_runs
        )
        paced_overruns = sum(int(run["over_budget"]) for run in paced_runs)
        tail_qualified = paced_overruns == 0
        if (
            not math.isclose(
                _finite_number(gate.get("paced_median_p99_budget_fraction"), "paced p99"),
                paced_p99,
                rel_tol=0,
                abs_tol=1e-12,
            )
            or not math.isclose(
                _finite_number(
                    gate.get("paced_median_over_budget_fraction"), "paced over-budget median"
                ),
                paced_over,
                rel_tol=0,
                abs_tol=1e-12,
            )
            or gate.get("paced_render_overruns") != paced_overruns
            or gate.get("paced_tail_qualification_rule")
            != "zero render-duration overruns in every paced run"
            or gate.get("paced_tail_qualification_passed") is not tail_qualified
        ):
            raise RuntimeError(f"{model_name} paced tail observation is inconsistent")
        paced_tail_by_model[model_name] = tail_qualified

    paced_tail_qualified = all(paced_tail_by_model.values())
    if check.get("paced_tail_qualification_passed") is not paced_tail_qualified:
        raise RuntimeError("overall paced tail qualification is inconsistent")
    return {
        "host_architecture": host_architecture,
        "core_microgate_passed": True,
        "paced_tail_qualification_passed": paced_tail_qualified,
    }


def release_acceptance(
    optimization: dict[str, object],
    core_capacity_result: dict[str, object] | None,
) -> dict[str, object]:
    slices = optimization.get("slices")
    if not isinstance(slices, dict) or not slices:
        raise RuntimeError("release optimization has no packaged architecture slices")

    measured_architecture = (
        core_capacity_result.get("host_architecture")
        if core_capacity_result is not None
        else None
    )
    per_slice = {}
    for architecture in slices:
        measured = measured_architecture == architecture
        per_slice[architecture] = {
            "output_only_core_microgate": "passed" if measured else "not_run",
            "output_only_paced_tail": (
                "qualified"
                if measured
                and core_capacity_result is not None
                and core_capacity_result["paced_tail_qualification_passed"]
                else "not_qualified" if measured else "not_run"
            ),
        }

    all_slices_measured = all(
        status["output_only_core_microgate"] == "passed"
        for status in per_slice.values()
    )
    return {
        # Input, standalone, and renderer checks remain human/local even when
        # the output-only microgate has covered every architecture.
        "status": "partial",
        "per_slice": per_slice,
        "output_only_core_microgate_all_slices_measured": all_slices_measured,
        "remaining_local_checks": [
            "output-only core capacity on every packaged architecture",
            "input-enabled VST3 audio function and callback timing",
            "standalone output-only first launch and sustained audio",
            "standalone input opt-in with sustained audio",
            "standalone renderer selection and idle UI CPU",
        ],
    }


def validate_pgo_provenance(
    path: pathlib.Path | None,
    optimization: dict[str, object],
    commits: dict[str, str],
) -> dict[str, object] | None:
    slices = optimization["slices"]
    pgo_slices = {
        architecture: settings
        for architecture, settings in slices.items()
        if settings["pgo_mode"] == "use"
    }
    if not pgo_slices:
        if path is not None:
            raise RuntimeError("PGO provenance was supplied for a build that does not use PGO")
        return None
    if path is None:
        raise RuntimeError("PGO release receipts require profile provenance")
    if len(pgo_slices) != 1:
        raise RuntimeError("each PGO provenance receipt must describe one architecture")

    path = path.resolve()
    provenance = json.loads(path.read_text(encoding="utf-8"))
    if provenance.get("schema") != "gearmulator.mdmm.apple-pgo-profile.v1":
        raise RuntimeError("unknown PGO profile provenance schema")
    architecture, settings = next(iter(pgo_slices.items()))
    profile = provenance.get("profile")
    build = provenance.get("build")
    source = provenance.get("source")
    training = provenance.get("training")
    if not all(isinstance(value, dict) for value in (profile, build, source, training)):
        raise RuntimeError("incomplete PGO profile provenance")
    if profile.get("sha256") != settings["profile_sha256"]:
        raise RuntimeError("PGO provenance does not match the configured profile")
    expected_source = {
        "parent_revision": commits["source_commit"],
        "dsp_revision": commits["dsp56300_commit"],
        "mc68k_revision": commits["mc68k_commit"],
        "juce_revision": commits["juce_commit"],
    }
    actual_source = {key: source.get(key) for key in expected_source}
    if actual_source != expected_source:
        raise RuntimeError(
            f"PGO profile source mismatch: expected {expected_source}, found {actual_source}"
        )
    if (
        build.get("architecture") != architecture
        or build.get("configuration") != "Release"
        or build.get("deployment_target") != optimization["deployment_target"]
        or build.get("thinlto") is not True
        or build.get("dsp_optimization") is not True
        or build.get("pgo_mode") != "generate"
        or set(build.get("optimized_targets", ()))
        != {"mdLib", "68kEmu", "dsp56kEmu", "dsp56kBase"}
        or not build.get("compiler_id")
        or not build.get("compiler_version")
    ):
        raise RuntimeError("PGO provenance build configuration does not match release policy")
    if (
        build["compiler_id"] != optimization["compiler"]["id"]
        or build["compiler_version"] != optimization["compiler"]["version"]
    ):
        raise RuntimeError("PGO profile compiler does not match the release compiler")
    trained_models = training.get("models")
    if not isinstance(trained_models, list):
        raise RuntimeError("PGO provenance does not cover the required MD/MM workload")
    trained_by_name = {
        model.get("model"): model for model in trained_models if isinstance(model, dict)
    }
    if (
        set(trained_by_name) != {"MD", "MM"}
        or training.get("host_sample_rate_hz") != 48000
        or training.get("block_frames") != 128
        or int(training.get("callbacks_per_model", 0)) < 12000
        or int(training.get("measured_callbacks_per_model", 0)) < 7500
        or any(
            trained_by_name[model].get("firmware_sha256") != FIRMWARE_SHA256[model]
            or not trained_by_name[model].get("raw_profile_sha256")
            for model in ("MD", "MM")
        )
    ):
        raise RuntimeError("PGO provenance does not cover the required MD/MM workload")
    for model in ("MD", "MM"):
        _require_sha256(
            trained_by_name[model]["raw_profile_sha256"],
            f"{model} raw PGO profile",
        )
    return {
        "sha256": sha256(path),
        "profile_sha256": profile["sha256"],
        "architecture": architecture,
        "compiler_id": build["compiler_id"],
        "compiler_version": build["compiler_version"],
        "deployment_target": build["deployment_target"],
        "source": expected_source,
        "training": {
            "host_sample_rate_hz": training["host_sample_rate_hz"],
            "block_frames": training["block_frames"],
            "models": ["MD", "MM"],
        },
    }


def parse_args(argv: Optional[list[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path)
    parser.add_argument("--artifact", type=pathlib.Path, action="append", default=[])
    parser.add_argument("--package-file", type=pathlib.Path, action="append", default=[])
    parser.add_argument("--archive", type=pathlib.Path)
    parser.add_argument("--build-cache", type=pathlib.Path)
    parser.add_argument("--core-capacity-check", type=pathlib.Path)
    parser.add_argument("--pgo-provenance", type=pathlib.Path)
    parser.add_argument("--print-release-selection", action="store_true")
    parser.add_argument(
        "--release-architectures", default="arm64;x86_64", metavar="ARCH[;ARCH]"
    )
    parser.add_argument("--release-pgo-mode", default="none")
    parser.add_argument("--release-pgo-profile", type=pathlib.Path)
    parser.add_argument("--check-source-only", action="store_true")
    parser.add_argument("--validate-build-optimization", type=pathlib.Path)
    parser.add_argument(
        "--expected-architecture", action="append", default=[], metavar="ARCH"
    )
    parser.add_argument("--expected-products-root", type=pathlib.Path)
    parser.add_argument(
        "--firmware-tests-required",
        nargs="?",
        const=True,
        default=False,
        type=binary_flag,
        metavar="0|1",
    )
    parser.add_argument("--expected-source-tuple")
    parser.add_argument("--allow-untracked-root", type=pathlib.Path, action="append", default=[])
    parser.add_argument("--validate-build-root", type=pathlib.Path)
    parser.add_argument("--validate-output-root", type=pathlib.Path)
    parser.add_argument("--prepare-build-root", type=pathlib.Path)
    parser.add_argument("--prepare-output-root", type=pathlib.Path)
    return parser.parse_args(argv)


def main() -> None:
    args = parse_args()

    source = args.source.resolve()
    if args.print_release_selection:
        selection = release_selection(
            args.release_architectures,
            args.release_pgo_mode,
            args.release_pgo_profile,
            args.pgo_provenance,
        )
        print(f"{selection['cmake_architectures']}|{selection['package_name']}")
        return
    if args.validate_build_optimization is not None:
        optimization = release_optimization(
            args.validate_build_optimization,
            tuple(args.expected_architecture),
            args.expected_products_root,
        )
        provenance = validate_pgo_provenance(
            args.pgo_provenance,
            optimization,
            source_tuple(source),
        )
        print(
            json.dumps(
                {"optimization": optimization, "pgo_profile_provenance": provenance},
                sort_keys=True,
            )
        )
        return
    if args.validate_build_root is not None or args.validate_output_root is not None:
        if args.validate_build_root is None or args.validate_output_root is None:
            raise RuntimeError(
                "--validate-build-root and --validate-output-root are required together"
            )
        validate_release_directories(source, args.validate_build_root, args.validate_output_root)
        return
    if args.prepare_build_root is not None or args.prepare_output_root is not None:
        if args.prepare_build_root is None or args.prepare_output_root is None:
            raise RuntimeError(
                "--prepare-build-root and --prepare-output-root are required together"
            )
        prepare_release_directories(source, args.prepare_build_root, args.prepare_output_root)
        return
    require_clean(
        source,
        include_untracked=args.check_source_only,
        allowed_untracked_roots=tuple(args.allow_untracked_root),
    )
    commits = source_tuple(source)
    expected_commits = None
    if args.expected_source_tuple is not None:
        expected_commits = json.loads(args.expected_source_tuple)
        if expected_commits != commits:
            raise RuntimeError(
                "source/dependency commits changed before the build receipt was written: "
                f"expected {expected_commits}, found {commits}"
            )
    if args.check_source_only:
        print(json.dumps(commits, sort_keys=True))
        return
    if (
        args.output is None
        or not args.artifact
        or args.archive is None
        or args.build_cache is None
    ):
        raise RuntimeError(
            "--output, --archive, --build-cache, and at least one --artifact "
            "are required when writing a receipt"
        )

    optimization = release_optimization(
        args.build_cache,
        tuple(args.expected_architecture),
        args.expected_products_root,
    )
    pgo_provenance = validate_pgo_provenance(args.pgo_provenance, optimization, commits)

    artifacts = []
    for bundle in args.artifact:
        module = bundle_module(bundle.resolve())
        artifacts.append(
            {
                "name": bundle.name,
                "module": module.name,
                "bytes": module.stat().st_size,
                "sha256": sha256(module),
            }
        )

    package_files = []
    for package_file in args.package_file:
        package_file = package_file.resolve()
        if not package_file.is_file():
            raise RuntimeError(f"package support file does not exist: {package_file}")
        package_files.append(
            {
                "name": package_file.name,
                "bytes": package_file.stat().st_size,
                "sha256": sha256(package_file),
            }
        )

    archive = args.archive.resolve()
    require_clean(source, include_untracked=True, allowed_untracked_roots=tuple(args.allow_untracked_root))
    final_commits = source_tuple(source)
    if final_commits != commits or (expected_commits is not None and final_commits != expected_commits):
        raise RuntimeError(
            "source/dependency commits changed while hashing release artifacts: "
            f"started with {commits}, finished with {final_commits}"
        )
    core_capacity_check = None
    core_capacity_result = None
    if args.core_capacity_check is not None:
        check_path = args.core_capacity_check.resolve()
        if not check_path.is_file():
            raise RuntimeError(
                f"core-capacity receipt does not exist: {check_path}"
            )
        core_capacity_check = json.loads(check_path.read_text(encoding="utf-8"))
        core_capacity_result = validate_core_capacity_check(
            core_capacity_check,
            {artifact["name"]: artifact["sha256"] for artifact in artifacts},
            optimization,
        )
    if args.firmware_tests_required and core_capacity_check is None:
        raise RuntimeError(
            "firmware-backed release receipts require a passing output-only core microgate"
        )

    acceptance = release_acceptance(optimization, core_capacity_result)

    receipt = {
        "schema": "gearmulator-elektron-macos-build-v2",
        "created_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "configuration": "Release",
        "architecture": " ".join(sorted(optimization["slices"])),
        "signing": "ad-hoc",
        "notarized": False,
        **final_commits,
        "source_tree_clean": True,
        "firmware_included": False,
        "tests_run": True,
        "firmware_tests_required": args.firmware_tests_required,
        "release_approved": False,
        "release_acceptance": acceptance,
        "automated_output_only_core_microgate_passed_for_measured_slice": bool(
            core_capacity_result is not None
        ),
        "automated_output_only_core_microgate_all_slices_measured": acceptance[
            "output_only_core_microgate_all_slices_measured"
        ],
        "optimization": optimization,
        "pgo_profile_provenance": pgo_provenance,
        "core_capacity_check": core_capacity_check,
        "packaged_firmware_smoke": {
            "required": args.firmware_tests_required,
            "fixture_format": "external complete 8 MiB .bin",
            "fixture_sha256": {
                "md": FIRMWARE_SHA256["MD"],
                "mm": FIRMWARE_SHA256["MM"],
            },
            "audio_callback_blocks": 256 if args.firmware_tests_required else 16,
            "scope": "exact extracted VST3 load, device initialization, and callback execution",
        },
        "archive": {
            "name": archive.name,
            "bytes": archive.stat().st_size,
            "sha256": sha256(archive),
        },
        "artifacts": artifacts,
        "package_files": package_files,
    }
    args.output.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
