#!/usr/bin/env python3

from __future__ import annotations

import json
import pathlib
import subprocess
import tempfile
import unittest

import write_mdmm_receipt as receipt


class ReceiptPathSafetyTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = pathlib.Path(self.temporary.name).resolve()
        self.source = self.root / "source"
        self.source.mkdir()
        self.git("init", "--quiet")
        self.git("config", "user.email", "release-test@example.invalid")
        self.git("config", "user.name", "Release Test")
        (self.source / "tracked.txt").write_text("tracked\n", encoding="utf-8")
        (self.source / ".gitignore").write_text("ignored-build\n", encoding="utf-8")
        tracked_dir = self.source / "tracked-dir"
        tracked_dir.mkdir()
        (tracked_dir / "input.txt").write_text("input\n", encoding="utf-8")
        self.git("add", ".gitignore", "tracked.txt", "tracked-dir/input.txt")
        self.git("commit", "--quiet", "-m", "fixture")

    def git(self, *args: str) -> None:
        subprocess.run(
            ["git", "-C", str(self.source), *args],
            check=True,
            stdout=subprocess.DEVNULL,
        )

    def git_output(self, *args: str) -> str:
        return subprocess.check_output(
            ["git", "-C", str(self.source), *args], text=True
        ).strip()

    def test_firmware_receipt_flag_accepts_ci_and_release_modes(self) -> None:
        base = ["--source", str(self.source)]

        self.assertFalse(receipt.parse_args(base).firmware_tests_required)
        ci_args = receipt.parse_args([*base, "--firmware-tests-required", "0"])
        release_args = receipt.parse_args([*base, "--firmware-tests-required", "1"])
        self.assertFalse(ci_args.firmware_tests_required)
        self.assertTrue(release_args.firmware_tests_required)
        # Retain compatibility with the original flag-only spelling.
        legacy_args = receipt.parse_args([*base, "--firmware-tests-required"])
        self.assertTrue(legacy_args.firmware_tests_required)

    def test_package_support_files_are_repeatable(self) -> None:
        args = receipt.parse_args(
            [
                "--source",
                str(self.source),
                "--package-file",
                "setup.command",
                "--package-file",
                "INSTALL.txt",
            ]
        )
        self.assertEqual(
            args.package_file,
            [pathlib.Path("setup.command"), pathlib.Path("INSTALL.txt")],
        )

    def test_release_selection_defaults_to_universal_without_pgo(self) -> None:
        result = receipt.release_selection(
            "arm64;x86_64", "none", None, None
        )

        self.assertEqual(result["architectures"], ("arm64", "x86_64"))
        self.assertEqual(result["cmake_architectures"], "arm64;x86_64")
        self.assertEqual(
            result["package_name"], "Maschine-MD-MM-macOS-Universal"
        )

    def test_release_selection_accepts_single_architecture_pgo(self) -> None:
        profile = self.root / "arm64.profdata"
        provenance = self.root / "arm64.provenance.json"
        profile.write_bytes(b"profile")
        provenance.write_text("{}", encoding="utf-8")

        result = receipt.release_selection(
            "arm64", "use", profile, provenance
        )

        self.assertEqual(result["architectures"], ("arm64",))
        self.assertEqual(
            result["package_name"], "Maschine-MD-MM-macOS-arm64-PGO"
        )

    def test_release_selection_rejects_universal_or_incomplete_pgo(self) -> None:
        profile = self.root / "arm64.profdata"
        provenance = self.root / "arm64.provenance.json"
        profile.write_bytes(b"profile")
        provenance.write_text("{}", encoding="utf-8")

        cases = (
            ("arm64;x86_64", "use", profile, provenance, "exactly one"),
            ("arm64", "use", profile, None, "both profile and provenance"),
            ("arm64", "generate", None, None, "none or use"),
            ("arm64", "none", profile, None, "while PGO mode is none"),
            ("native", "none", None, None, "must be arm64"),
        )
        for architectures, mode, profile_path, provenance_path, message in cases:
            with self.subTest(architectures=architectures, mode=mode):
                with self.assertRaisesRegex(RuntimeError, message):
                    receipt.release_selection(
                        architectures,
                        mode,
                        profile_path,
                        provenance_path,
                    )

    def write_cache(
        self,
        *,
        architectures: str = "arm64;x86_64",
        thinlto: str = "ON",
        optimize_dsp: str = "ON",
        pgo_mode: str = "none",
        profile: pathlib.Path | None = None,
    ) -> pathlib.Path:
        cache = self.root / f"cache-{len(list(self.root.glob('cache-*')))}.txt"
        compiler_metadata = self.root / "CMakeFiles" / "fixture" / "CMakeCXXCompiler.cmake"
        compiler_metadata.parent.mkdir(parents=True, exist_ok=True)
        compiler_metadata.write_text(
            'set(CMAKE_CXX_COMPILER_ID "AppleClang")\n'
            'set(CMAKE_CXX_COMPILER_VERSION "16.0")\n',
            encoding="utf-8",
        )
        cache.write_text(
            "CMAKE_BUILD_TYPE:STRING=Release\n"
            f"CMAKE_OSX_DEPLOYMENT_TARGET:STRING={receipt.MACOS_DEPLOYMENT_TARGET}\n"
            f"CMAKE_OSX_ARCHITECTURES:STRING={architectures}\n"
            f"GEARMULATOR_JUCE_PRODUCTS_ROOT:PATH={self.root / 'products'}\n"
            f"GEARMULATOR_MDMM_APPLE_THINLTO:BOOL={thinlto}\n"
            f"GEARMULATOR_MDMM_APPLE_OPTIMIZE_DSP:BOOL={optimize_dsp}\n"
            f"GEARMULATOR_MDMM_APPLE_PGO_MODE:STRING={pgo_mode}\n"
            f"GEARMULATOR_MDMM_APPLE_PGO_PROFILE:FILEPATH={profile or ''}\n"
            f"GEARMULATOR_MDMM_APPLE_OPTIMIZATION_APPLIED_TARGETS:INTERNAL="
            f"mdLib;68kEmu;dsp56kEmu;dsp56kBase\n"
            f"GEARMULATOR_MDMM_APPLE_OPTIMIZATION_APPLIED_PGO_MODE:INTERNAL={pgo_mode}\n"
            f"GEARMULATOR_MDMM_APPLE_OPTIMIZATION_APPLIED_PROFILE_SHA256:INTERNAL="
            f"{receipt.sha256(profile) if profile else ''}\n",
            encoding="utf-8",
        )
        return cache

    def test_universal_release_optimization_is_cache_derived(self) -> None:
        result = receipt.release_optimization(
            self.write_cache(), ("arm64", "x86_64")
        )

        self.assertEqual(result["deployment_target"], receipt.MACOS_DEPLOYMENT_TARGET)
        self.assertEqual(set(result["slices"]), {"arm64", "x86_64"})
        for settings in result["slices"].values():
            self.assertEqual(
                settings,
                {
                    "thinlto": True,
                    "dsp_optimization": True,
                    "pgo_mode": "none",
                    "profile_sha256": None,
                },
            )

    def test_release_optimization_rejects_silent_fallbacks(self) -> None:
        cases = (
            ({"thinlto": "OFF"}, "require ThinLTO"),
            ({"optimize_dsp": "OFF"}, "require ThinLTO"),
            ({"pgo_mode": "generate"}, "PGO mode none or use"),
        )
        for options, message in cases:
            with self.subTest(options=options), self.assertRaisesRegex(RuntimeError, message):
                receipt.release_optimization(self.write_cache(**options))

    def test_release_optimization_requires_an_applied_target_marker(self) -> None:
        cache = self.write_cache()
        cache.write_text(
            cache.read_text(encoding="utf-8").replace(
                "GEARMULATOR_MDMM_APPLE_OPTIMIZATION_APPLIED_TARGETS:INTERNAL="
                "mdLib;68kEmu;dsp56kEmu;dsp56kBase\n",
                "",
            ),
            encoding="utf-8",
        )

        with self.assertRaisesRegex(RuntimeError, "did not apply"):
            receipt.release_optimization(cache)

    def test_release_optimization_rejects_shared_product_output(self) -> None:
        cache = self.write_cache()
        cache.write_text(
            cache.read_text(encoding="utf-8").replace(
                f"GEARMULATOR_JUCE_PRODUCTS_ROOT:PATH={self.root / 'products'}",
                f"GEARMULATOR_JUCE_PRODUCTS_ROOT:PATH={self.source / 'bin/plugins'}",
            ),
            encoding="utf-8",
        )

        with self.assertRaisesRegex(RuntimeError, "owned by the build directory"):
            receipt.release_optimization(cache)

    def test_release_optimization_rejects_architecture_mismatch(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "architecture mismatch"):
            receipt.release_optimization(
                self.write_cache(architectures="arm64"), ("arm64", "x86_64")
            )

    def test_release_optimization_rejects_wrong_deployment_target(self) -> None:
        cache = self.write_cache()
        cache.write_text(
            cache.read_text(encoding="utf-8").replace(
                f"CMAKE_OSX_DEPLOYMENT_TARGET:STRING={receipt.MACOS_DEPLOYMENT_TARGET}",
                "CMAKE_OSX_DEPLOYMENT_TARGET:STRING=10.12",
            ),
            encoding="utf-8",
        )

        with self.assertRaisesRegex(RuntimeError, "deployment target"):
            receipt.release_optimization(cache)

    def test_pgo_receipt_hashes_a_single_architecture_profile(self) -> None:
        profile = self.root / "current.profdata"
        profile.write_bytes(b"profile-data")
        result = receipt.release_optimization(
            self.write_cache(
                architectures="arm64", pgo_mode="use", profile=profile
            ),
            ("arm64",),
        )

        self.assertEqual(
            result["slices"]["arm64"]["profile_sha256"], receipt.sha256(profile)
        )

    def test_one_profile_cannot_claim_a_universal_pgo_build(self) -> None:
        profile = self.root / "current.profdata"
        profile.write_bytes(b"profile-data")
        with self.assertRaisesRegex(RuntimeError, "cannot qualify a universal build"):
            receipt.release_optimization(
                self.write_cache(pgo_mode="use", profile=profile)
            )

    def test_pgo_provenance_binds_profile_source_compiler_and_training(self) -> None:
        profile = self.root / "current.profdata"
        profile.write_bytes(b"profile-data")
        optimization = receipt.release_optimization(
            self.write_cache(
                architectures="arm64", pgo_mode="use", profile=profile
            )
        )
        commits = {
            "source_commit": "parent",
            "dsp56300_commit": "dsp",
            "mc68k_commit": "mcu",
            "juce_commit": "juce",
        }
        provenance = self.root / "current.provenance.json"
        provenance.write_text(
            json.dumps(
                {
                    "schema": "gearmulator.mdmm.apple-pgo-profile.v1",
                    "profile": {"sha256": receipt.sha256(profile)},
                    "build": {
                        "architecture": "arm64",
                        "configuration": "Release",
                        "deployment_target": receipt.MACOS_DEPLOYMENT_TARGET,
                        "compiler_id": "AppleClang",
                        "compiler_version": "16.0",
                        "thinlto": True,
                        "dsp_optimization": True,
                        "pgo_mode": "generate",
                        "optimized_targets": [
                            "mdLib",
                            "68kEmu",
                            "dsp56kEmu",
                            "dsp56kBase",
                        ],
                    },
                    "source": {
                        "parent_revision": "parent",
                        "dsp_revision": "dsp",
                        "mc68k_revision": "mcu",
                        "juce_revision": "juce",
                    },
                    "training": {
                        "host_sample_rate_hz": 48000,
                        "block_frames": 128,
                        "callbacks_per_model": 12000,
                        "measured_callbacks_per_model": 7500,
                        "models": [
                            {
                                "model": model,
                                "firmware_sha256": receipt.FIRMWARE_SHA256[model],
                                "raw_profile_sha256": "c" * 64 if model == "MD" else "d" * 64,
                            }
                            for model in ("MD", "MM")
                        ],
                    },
                }
            ),
            encoding="utf-8",
        )

        result = receipt.validate_pgo_provenance(
            provenance, optimization, commits
        )

        self.assertEqual(result["profile_sha256"], receipt.sha256(profile))
        self.assertEqual(result["source"]["juce_revision"], "juce")
        self.assertEqual(result["deployment_target"], receipt.MACOS_DEPLOYMENT_TARGET)

        wrong_target = json.loads(provenance.read_text(encoding="utf-8"))
        wrong_target["build"]["deployment_target"] = "10.12"
        provenance.write_text(json.dumps(wrong_target), encoding="utf-8")
        with self.assertRaisesRegex(RuntimeError, "build configuration"):
            receipt.validate_pgo_provenance(provenance, optimization, commits)

    def test_pgo_provenance_rejects_a_different_parent_source(self) -> None:
        profile = self.root / "current.profdata"
        profile.write_bytes(b"profile-data")
        optimization = receipt.release_optimization(
            self.write_cache(
                architectures="arm64", pgo_mode="use", profile=profile
            )
        )
        provenance = self.root / "current.provenance.json"
        provenance.write_text(
            json.dumps(
                {
                    "schema": "gearmulator.mdmm.apple-pgo-profile.v1",
                    "profile": {"sha256": receipt.sha256(profile)},
                    "build": {},
                    "source": {
                        "parent_revision": "old-parent",
                        "dsp_revision": "dsp",
                        "mc68k_revision": "mcu",
                        "juce_revision": "juce",
                    },
                    "training": {},
                }
            ),
            encoding="utf-8",
        )

        with self.assertRaisesRegex(RuntimeError, "profile source mismatch"):
            receipt.validate_pgo_provenance(
                provenance,
                optimization,
                {
                    "source_commit": "parent",
                    "dsp56300_commit": "dsp",
                    "mc68k_commit": "mcu",
                    "juce_commit": "juce",
                },
            )

    def core_capacity_check(self) -> dict[str, object]:
        period = 128 * 1000 / 48000

        def run(p50: float = 0.8, p99: float = 0.9) -> dict[str, object]:
            return {
                "total_callbacks": 7500,
                "total_samples": 960000,
                "warm_callbacks": 3000,
                "p50_ms": p50 * period,
                "p99_ms": p99 * period,
                "max_ms": 0.95 * period,
                "p50_budget_fraction": p50,
                "p99_budget_fraction": p99,
                "max_budget_fraction": 0.95,
                "over_budget": 0,
                "over_budget_fraction": 0.0,
                "scheduler_late_p99_ms": 0.01,
                "completion_after_deadline": 0,
                "completion_after_deadline_fraction": 0.0,
                "audio_peak_before_quantization": 0.1,
                "capture_sha256": {
                    "capture.blocks.csv": "1" * 64,
                    "capture.json": "2" * 64,
                    "capture.wav": "3" * 64,
                    "host.log": "4" * 64,
                },
            }

        gate = {
            "capacity_median_p50_budget_fraction": 0.8,
            "capacity_limit": 0.90,
            "core_microgate_passed": True,
            "paced_median_p99_budget_fraction": 0.9,
            "paced_median_over_budget_fraction": 0.0,
            "paced_render_overruns": 0,
            "paced_tail_qualification_rule": (
                "zero render-duration overruns in every paced run"
            ),
            "paced_tail_qualification_passed": True,
        }
        return {
            "schema": receipt.CORE_CAPACITY_SCHEMA,
            "scope": receipt.CORE_CAPACITY_SCOPE,
            "core_microgate_passed": True,
            "paced_tail_qualification_passed": True,
            "host_architecture": "arm64",
            "host_os": "Mac OSX test",
            "host_name": "latency_host",
            "host_sha256": "0" * 64,
            "sample_rate": 48000,
            "block_size": 128,
            "period_ms": period,
            "seconds": 20,
            "warm_start_seconds": 12,
            "repetitions": {"capacity": 3, "paced": 3},
            "workload": receipt.CORE_CAPACITY_WORKLOAD,
            "firmware_sha256": receipt.FIRMWARE_SHA256,
            "plugin_module_sha256": {
                "Gearmulator MD.vst3": "a" * 64,
                "Gearmulator MM.vst3": "b" * 64,
            },
            "models": {
                model: {
                    "capacity_runs": [run(), run(), run()],
                    "paced_runs": [run(), run(), run()],
                    "gate": dict(gate),
                }
                for model in ("MD", "MM")
            },
        }

    def artifact_hashes(self) -> dict[str, str]:
        return {
            "Gearmulator MD.vst3": "a" * 64,
            "Gearmulator MM.vst3": "b" * 64,
        }

    def test_core_check_is_bound_to_native_slice_and_packaged_plugins(self) -> None:
        result = receipt.validate_core_capacity_check(
            self.core_capacity_check(),
            self.artifact_hashes(),
            {"slices": {"arm64": {}, "x86_64": {}}},
        )

        self.assertEqual(result["host_architecture"], "arm64")
        self.assertTrue(result["core_microgate_passed"])
        self.assertTrue(result["paced_tail_qualification_passed"])

    def test_core_check_rejects_a_weaker_microgate(self) -> None:
        check = self.core_capacity_check()
        check["models"]["MD"]["gate"]["capacity_limit"] = 0.95

        with self.assertRaisesRegex(RuntimeError, "weaker than release policy"):
            receipt.validate_core_capacity_check(
                check,
                self.artifact_hashes(),
                {"slices": {"arm64": {}, "x86_64": {}}},
            )

    def test_core_check_recomputes_the_microgate_from_measurements(self) -> None:
        check = self.core_capacity_check()
        period = check["period_ms"]
        for run in check["models"]["MD"]["capacity_runs"]:
            run["p50_budget_fraction"] = 0.91
            run["p50_ms"] = 0.91 * period
            run["p99_budget_fraction"] = 0.92
            run["p99_ms"] = 0.92 * period

        with self.assertRaisesRegex(RuntimeError, "weaker than release policy"):
            receipt.validate_core_capacity_check(
                check,
                self.artifact_hashes(),
                {"slices": {"arm64": {}, "x86_64": {}}},
            )

    def test_core_check_rejects_an_unmeasured_architecture(self) -> None:
        check = self.core_capacity_check()
        check["host_architecture"] = "x86_64"

        with self.assertRaisesRegex(RuntimeError, "host architecture is not packaged"):
            receipt.validate_core_capacity_check(
                check,
                self.artifact_hashes(),
                {"slices": {"arm64": {}}},
            )

    def test_core_check_validates_fixed_workload_and_raw_hashes(self) -> None:
        mutations = (
            (lambda check: check.__setitem__("seconds", 21), "fixed release workload"),
            (
                lambda check: check["models"]["MD"]["capacity_runs"][0].__setitem__(
                    "warm_callbacks", 2999
                ),
                "wrong capture length",
            ),
            (
                lambda check: check.__setitem__(
                    "firmware_sha256", {**receipt.FIRMWARE_SHA256, "MD": "f" * 64}
                ),
                "pinned firmware",
            ),
            (lambda check: check.__setitem__("scope", "generic timing"), "wrong scope"),
            (lambda check: check.__setitem__("host_name", "other-host"), "host identity"),
            (
                lambda check: check["models"]["MM"]["paced_runs"][0][
                    "capture_sha256"
                ].__setitem__("capture.wav", "not-a-hash"),
                "SHA-256",
            ),
        )
        for mutate, message in mutations:
            check = self.core_capacity_check()
            mutate(check)
            with self.subTest(message=message), self.assertRaisesRegex(RuntimeError, message):
                receipt.validate_core_capacity_check(
                    check,
                    self.artifact_hashes(),
                    {"slices": {"arm64": {}, "x86_64": {}}},
                )

    def test_render_overrun_is_reported_as_an_unqualified_tail(self) -> None:
        check = self.core_capacity_check()
        run = check["models"]["MD"]["paced_runs"][0]
        run["over_budget"] = 1
        run["over_budget_fraction"] = 1 / 3000
        run["max_ms"] = 1.01 * check["period_ms"]
        run["max_budget_fraction"] = 1.01
        gate = check["models"]["MD"]["gate"]
        gate["paced_render_overruns"] = 1
        gate["paced_tail_qualification_passed"] = False
        check["paced_tail_qualification_passed"] = False

        result = receipt.validate_core_capacity_check(
            check,
            self.artifact_hashes(),
            {"slices": {"arm64": {}, "x86_64": {}}},
        )

        self.assertTrue(result["core_microgate_passed"])
        self.assertFalse(result["paced_tail_qualification_passed"])

    def test_core_check_rejects_impossible_zero_overrun_tail(self) -> None:
        check = self.core_capacity_check()
        run = check["models"]["MD"]["paced_runs"][0]
        run["max_ms"] = 2 * check["period_ms"]
        run["max_budget_fraction"] = 2.0

        with self.assertRaisesRegex(RuntimeError, "maximum and render-overrun"):
            receipt.validate_core_capacity_check(
                check,
                self.artifact_hashes(),
                {"slices": {"arm64": {}, "x86_64": {}}},
            )

    def test_universal_acceptance_is_partial_and_reports_each_slice(self) -> None:
        result = receipt.release_acceptance(
            {"slices": {"arm64": {}, "x86_64": {}}},
            {
                "host_architecture": "arm64",
                "core_microgate_passed": True,
                "paced_tail_qualification_passed": False,
            },
        )

        self.assertEqual(result["status"], "partial")
        self.assertEqual(
            result["per_slice"],
            {
                "arm64": {
                    "output_only_core_microgate": "passed",
                    "output_only_paced_tail": "not_qualified",
                },
                "x86_64": {
                    "output_only_core_microgate": "not_run",
                    "output_only_paced_tail": "not_run",
                },
            },
        )
        self.assertFalse(result["output_only_core_microgate_all_slices_measured"])

    def test_exact_untracked_package_and_archive_are_allowed(self) -> None:
        package = self.source / "artifacts" / "package"
        package.mkdir(parents=True)
        (package / "plugin.bin.test").write_text("plugin\n", encoding="utf-8")
        archive = self.source / "artifacts" / "package.zip"
        archive.write_text("archive\n", encoding="utf-8")

        receipt.require_clean(self.source, True, (package, archive))

    def test_unexpected_output_sibling_is_rejected(self) -> None:
        package = self.source / "artifacts" / "package"
        package.mkdir(parents=True)
        (package / "plugin").write_text("plugin\n", encoding="utf-8")
        (package.parent / "unexpected.syx").write_text("fixture\n", encoding="utf-8")

        with self.assertRaisesRegex(RuntimeError, "unexpected.syx"):
            receipt.require_clean(self.source, True, (package,))

    def test_source_or_ancestor_allow_root_is_rejected(self) -> None:
        for root in (self.source, self.source.parent, self.source / "artifacts" / ".."):
            with self.subTest(root=root), self.assertRaisesRegex(RuntimeError, "contains source tree"):
                receipt.require_clean(self.source, True, (root,))

    def test_untracked_symlink_cannot_borrow_allowed_target(self) -> None:
        package = self.source / "artifacts" / "package"
        package.mkdir(parents=True)
        target = package / "plugin"
        target.write_text("plugin\n", encoding="utf-8")
        (self.source / "outside-link").symlink_to(target)

        with self.assertRaisesRegex(RuntimeError, "outside-link"):
            receipt.require_clean(self.source, True, (package,))

    def test_tracked_changes_remain_rejected(self) -> None:
        (self.source / "tracked.txt").write_text("changed\n", encoding="utf-8")

        with self.assertRaisesRegex(RuntimeError, "dirty source tree"):
            receipt.require_clean(self.source, True, (self.source / "artifacts",))

    def test_cleanup_root_rejects_source_ancestor_and_tracked_content(self) -> None:
        for root in (self.source, self.source.parent, self.source / "tracked-dir"):
            with self.subTest(root=root), self.assertRaises(RuntimeError):
                receipt.validate_cleanup_root(self.source, root, "test directory")

    def test_cleanup_root_rejects_git_metadata(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "Git metadata"):
            receipt.validate_cleanup_root(self.source, self.source / ".git" / "release", "test directory")

    def test_cleanup_root_rejects_tracked_submodule_interior(self) -> None:
        commit = self.git_output("rev-parse", "HEAD")
        self.git("update-index", "--add", "--cacheinfo", f"160000,{commit},vendor")

        with self.assertRaisesRegex(RuntimeError, "inside tracked submodule"):
            receipt.validate_cleanup_root(self.source, self.source / "vendor" / "build", "test directory")

    def test_cleanup_root_rejects_tracked_symlink_before_resolving_it(self) -> None:
        external = self.root / "external"
        external.mkdir()
        link = self.source / "tracked-output-link"
        link.symlink_to(external, target_is_directory=True)
        self.git("add", "tracked-output-link")

        with self.assertRaisesRegex(RuntimeError, "contains tracked path"):
            receipt.validate_cleanup_root(self.source, link, "test directory")

    def test_cleanup_root_rejects_ignored_source_symlink(self) -> None:
        external = self.root / "external"
        external.mkdir()
        link = self.source / "ignored-build"
        link.symlink_to(external, target_is_directory=True)

        with self.assertRaisesRegex(RuntimeError, "follows a source-tree symlink"):
            receipt.validate_cleanup_root(self.source, link, "test directory")

    def test_release_directories_must_not_overlap(self) -> None:
        output = self.source / "artifacts"
        build = output / "build"

        with self.assertRaisesRegex(RuntimeError, "must not overlap"):
            receipt.validate_release_directories(self.source, build, output)

    def test_safe_untracked_release_directories_are_accepted(self) -> None:
        build = self.source / "build" / "release"
        output = self.source / "artifacts" / "release"

        self.assertEqual(
            receipt.validate_release_directories(self.source, build, output),
            (build, output),
        )

    def test_prepare_creates_owned_release_roots(self) -> None:
        build = self.root / "build"
        output = self.root / "output"

        receipt.prepare_release_directories(self.source, build, output)

        for root in (build, output):
            marker = root / receipt.RELEASE_ROOT_MARKER
            self.assertEqual(
                marker.read_text(encoding="utf-8"),
                receipt._release_root_marker_contents(root),
            )

    def test_owned_markers_can_be_the_only_allowed_untracked_files(self) -> None:
        build = self.source / "release-build"
        output = self.source / "release-output"
        receipt.prepare_release_directories(self.source, build, output)

        receipt.require_clean(
            self.source,
            include_untracked=True,
            allowed_untracked_roots=(
                build / receipt.RELEASE_ROOT_MARKER,
                output / receipt.RELEASE_ROOT_MARKER,
            ),
        )

    def test_prepare_refuses_unowned_existing_root_without_deleting_it(self) -> None:
        build = self.root / "build"
        output = self.root / "output"
        build.mkdir()
        output.mkdir()
        sentinel = output / "human-file.txt"
        sentinel.write_text("keep me\n", encoding="utf-8")

        with self.assertRaisesRegex(RuntimeError, "unowned build directory"):
            receipt.prepare_release_directories(self.source, build, output)

        self.assertEqual(sentinel.read_text(encoding="utf-8"), "keep me\n")

    def test_prepare_checks_both_roots_before_deleting_either(self) -> None:
        build = self.root / "build"
        output = self.root / "output"
        receipt.prepare_release_directories(self.source, build, output)
        build_sentinel = build / "old-build.txt"
        build_sentinel.write_text("keep until both roots pass\n", encoding="utf-8")
        (output / receipt.RELEASE_ROOT_MARKER).unlink()

        with self.assertRaisesRegex(RuntimeError, "unowned output directory"):
            receipt.prepare_release_directories(self.source, build, output)

        self.assertTrue(build_sentinel.is_file())

    def test_prepare_resets_owned_roots_and_preserves_markers(self) -> None:
        build = self.root / "build"
        output = self.root / "output"
        receipt.prepare_release_directories(self.source, build, output)
        (build / "old-file").write_text("old\n", encoding="utf-8")
        old_dir = output / "old-dir"
        old_dir.mkdir()
        (old_dir / "old-file").write_text("old\n", encoding="utf-8")

        receipt.prepare_release_directories(self.source, build, output)

        for root in (build, output):
            self.assertEqual(
                {path.name for path in root.iterdir()},
                {receipt.RELEASE_ROOT_MARKER},
            )

    def test_prepare_rejects_marker_copied_from_another_root(self) -> None:
        build = self.root / "build"
        output = self.root / "output"
        receipt.prepare_release_directories(self.source, build, output)
        copied = self.root / "copied-build"
        copied.mkdir()
        (copied / receipt.RELEASE_ROOT_MARKER).write_text(
            (build / receipt.RELEASE_ROOT_MARKER).read_text(encoding="utf-8"),
            encoding="utf-8",
        )

        with self.assertRaisesRegex(RuntimeError, "invalid ownership marker"):
            receipt.prepare_release_directories(self.source, copied, output)


if __name__ == "__main__":
    unittest.main()
