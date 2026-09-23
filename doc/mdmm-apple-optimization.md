# MD/MM Apple compiler optimization

MD/MM provides compiler settings for Release builds on macOS. They remain
opt-in for developer builds because they apply to shared emulation libraries,
so other products linked to those libraries also consume the selected build.
The official `scripts/macos/build_mdmm.sh` release path explicitly enables
ThinLTO for the MCU and DSP cores, reads the generated CMake cache back, and
fails before compilation if either setting is absent. An ordinary Release
build is not an acceptable macOS MD/MM release artifact.

The same script sets `GEARMULATOR_JUCE_PRODUCTS_ROOT` to `products` inside its
owned build directory. Ordinary developer builds retain the historical
`bin/plugins` destination, but they cannot overwrite a release build while its
cache is being validated, its modules are being measured, or its receipt is
being written. The receipt writer rejects a release cache whose products root
is not the expected build-local directory.

| CMake option | Default | Effect |
| --- | --- | --- |
| `GEARMULATOR_MDMM_APPLE_THINLTO` | `OFF` | Build `mdLib` and `68kEmu` with ThinLTO and propagate the option to their final executable/plugin links. |
| `GEARMULATOR_MDMM_APPLE_OPTIMIZE_DSP` | `OFF` | Extend the selected optimization to `dsp56kEmu` and `dsp56kBase`. |
| `GEARMULATOR_MDMM_APPLE_PGO_MODE` | `none` | Select `none`, `generate`, or `use` for profile-guided optimization. |
| `GEARMULATOR_MDMM_APPLE_PGO_PROFILE` | Empty | Path to the merged profile for `use` mode. |

`build_mdmm.sh` defaults to `arm64;x86_64` and `pgo_mode=none`, producing
`Maschine-MD-MM-macOS-Universal.zip`. Those defaults are also explicit in
the hosted workflow. A local, guarded arm64 PGO candidate uses the same build,
test, signing, measurement, packaging, and receipt path:

```sh
GEARMULATOR_MDMM_MACOS_ARCHITECTURES=arm64 \
GEARMULATOR_MDMM_APPLE_PGO_MODE=use \
GEARMULATOR_MDMM_APPLE_PGO_PROFILE=/private/path/arm64.profdata \
GEARMULATOR_MDMM_APPLE_PGO_PROVENANCE=/private/path/arm64.provenance.json \
GEARMULATOR_MD_FIRMWARE_BIN=/private/path/md-1.63.bin \
GEARMULATOR_MM_FIRMWARE_BIN=/private/path/mm-1.32b.bin \
scripts/macos/build_mdmm.sh SOURCE BUILD OUTPUT
```

That command produces `Maschine-MD-MM-macOS-arm64-PGO.zip`. The script
accepts only `arm64`, `x86_64`, or the normalized universal pair. PGO `use`
requires one architecture plus both existing profile and provenance files;
`generate` is never accepted for a product package. Keep those inputs outside
the owned build and output roots because the script cleans both roots first.

PGO requires ThinLTO and one explicit `CMAKE_OSX_ARCHITECTURES` value. Train
and build each architecture separately. One profile cannot be applied to a
universal build. The official universal build therefore uses ThinLTO for the
MCU and DSP cores as its reproducible baseline and records `pgo_mode: none`
for both slices. A separately assembled universal PGO package must identify the
profile and provenance of every optimized slice; it must not label the other
slice as PGO optimized. The current official packager supports a guarded
single-architecture PGO candidate, but does not yet assemble or receipt a
mixed-slice package. Until that support exists, its universal output is the
ThinLTO and DSP baseline. The flags apply only to Release configurations.

## Profile generation and use

1. Pin the parent and submodule revisions, compiler, architecture, macOS
   deployment target, and build definitions. Keep a record of these and the
   workload used for training.
2. Configure an instrumented Release build with
   `GEARMULATOR_MDMM_APPLE_THINLTO=ON` and
   `GEARMULATOR_MDMM_APPLE_PGO_MODE=generate`. Select the same DSP option for
   generation and use.
3. Create an empty profile directory. Launch the instrumented executable
   directly with `LLVM_PROFILE_FILE` set to an absolute path such as
   `/path/to/profiles/arm64-%p.profraw`. Exercise representative workloads in
   both instruments, including the buffer sizes and sample rates being
   targeted. Quit cleanly to flush the profile. Instrumented builds have
   additional CPU overhead and are intended for training.
4. Merge those files with the matching compiler's tool:
   `xcrun llvm-profdata merge -o /path/to/arm64.profdata /path/to/profiles/*.profraw`.
5. Configure a separate Release build with the same options and source,
   `GEARMULATOR_MDMM_APPLE_PGO_MODE=use`, and
   `GEARMULATOR_MDMM_APPLE_PGO_PROFILE=/path/to/arm64.profdata`.
6. Verify audio, emulated timing, and callback performance against an ordinary
   Release control before using the result as a release candidate. A profile
   specialized to one workload can hurt another workload.

Replacing the merged profile at the same path automatically rebuilds the
optimized Release libraries when the profile contents change.

Profile-use builds treat Clang's out-of-date profile diagnostics as errors.
Regenerate the profile when source, compiler, architecture, or relevant
definitions change; some function-hash mismatches can be treated as missing
data without an out-of-date warning. A successful build does not establish
that an old profile still represents the intended workload. An unprofiled-file
warning can occur for translation units not reached by the training executable,
or when none of a file's function hashes match. Review these warnings for stale
profiles as well as gaps in training; this compiler diagnostic alone cannot
validate a profile.

Keep generated `.profraw` and `.profdata` files as local build inputs. The
resulting optimized executable does not require the profile at runtime.

## Release receipts and core-capacity check

`write_mdmm_receipt.py` derives optimization claims from `CMakeCache.txt`.
For every packaged architecture, the receipt records whether ThinLTO includes
the DSP and whether PGO was used. A single-architecture PGO receipt records the
SHA-256 of the exact profile and requires `--pgo-provenance` with schema
`gearmulator.mdmm.apple-pgo-profile.v1`. Keep that private provenance record
with the parent, DSP, MCU and JUCE revisions, compiler version, architecture,
deployment target, build definitions and training workload. The writer rejects
a source, compiler, architecture, deployment-target, profile or
required-workload mismatch. A profile hash proves identity; the provenance
record establishes whether that identity belongs to the build.
The provenance JSON records the deployment target as
`build.deployment_target`; the guarded alpha.11 path requires the string
`"10.13"`.

Local release builds require the pinned firmware and run
`check_mdmm_core_capacity.py` against the exact signed VST3 modules. This is a
headless, output-only microgate: the host opens zero input channels, creates no
editor, and runs both products at 48 kHz with 128-sample blocks. For each
product it makes three unpaced capacity measurements and three separately
paced observations. The median capacity p50 must use at most 90% of the
callback period. Three direct unpaced measurements of the preserved ThinLTO and
DSP MD baseline used 86.2%, 84.4%, and 86.1% (86.1% median) under active system
load. The ordinary alpha.11 result used about 101% in the comparable 48 kHz,
128-sample workload. The final build must reproduce the capacity result for
both products.

The paced runs report p99, maximum duration, render overruns, scheduler arrival
lateness, and completion-after-deadline counts. A paced tail is marked
qualified only when every measured callback meets its render-duration budget;
the core microgate never calls a run with occasional overruns qualified.
Scheduler arrival lateness remains separate from plug-in render duration and
does not make the capacity discriminator depend on OS wake-up jitter.

The core receipt fixes and validates the duration, warm-up, callback counts,
firmware hashes, host identity, bus layout, scenario, and hashes of every raw
capture before the private captures are removed. It names the one selected
architecture executed by the host; an explicit x86_64 single-slice run may be
executing under Rosetta rather than on Intel hardware. The universal build
receipt records the executed slice as measured and every other slice as
`not_run`; overall release acceptance remains `partial`. Fixture-free hosted CI
can build the optimized baseline but cannot issue this firmware-backed
microgate.

## Local acceptance after packaging

The build script produces a signed, verified candidate. It does not approve a
release. Before publication, exercise the exact extracted package and record:

1. The output-only core-capacity check on every packaged architecture. On an
   Apple-silicon build host, the automatic universal run covers arm64 only;
   x86_64 still needs an Intel or explicitly selected Rosetta run.
2. Three isolated, paced `latency/run.py --scenario input` captures for MD and
   MM at 48 kHz with 128-sample blocks. Require finite, non-silent output, a
   detected input correlation/delay in all four analysis windows, and review
   every render overrun separately from scheduler lateness.
3. A first-launch standalone run for MD and MM in fresh state. Confirm each
   starts with output only, produces sustained clean audio, selects the
   automatic Metal renderer on supported macOS systems, and settles to the
   expected idle UI CPU while the panel is static.
4. In another isolated standalone state, explicitly enable the physical stereo
   input in Audio/MIDI Settings. Confirm the choice remains available and that
   sustained input processing and the UI stay usable. Then relaunch fresh state
   once more to confirm the default is still output only.

Keep ROMs, NVRAM, rendered audio, and full run receipts in private evidence.
Only sanitized summaries and their hashes belong beside the public package.
