#!/bin/bash

set -euo pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
source_dir="$(cd "${1:-${script_dir}/../..}" && pwd)"
build_dir_input="${2:-${source_dir}/build/macos-mdmm-universal}"
output_dir_input="${3:-${source_dir}/artifacts/macos-mdmm-universal}"
build_dir="$(python3 -c 'import pathlib, sys; print(pathlib.Path(sys.argv[1]).resolve())' "${build_dir_input}")"
output_dir="$(python3 -c 'import pathlib, sys; print(pathlib.Path(sys.argv[1]).resolve())' "${output_dir_input}")"
require_firmware_tests="${GEARMULATOR_REQUIRE_FIRMWARE_TESTS:-1}"
md_firmware_bin="${GEARMULATOR_MD_FIRMWARE_BIN:-}"
mm_firmware_bin="${GEARMULATOR_MM_FIRMWARE_BIN:-}"
release_architectures_input="${GEARMULATOR_MDMM_MACOS_ARCHITECTURES:-arm64;x86_64}"
release_pgo_mode="${GEARMULATOR_MDMM_APPLE_PGO_MODE:-none}"
release_pgo_profile="${GEARMULATOR_MDMM_APPLE_PGO_PROFILE:-}"
release_pgo_provenance="${GEARMULATOR_MDMM_APPLE_PGO_PROVENANCE:-}"
readonly md_firmware_bin_sha256="68542e30917b9918ccaee2b2237df62c8a00479938680b85aca93ce4fbca44c8"
readonly mm_firmware_bin_sha256="369849175602e20a9dd2b6e0ad8ac404b76f82718b14afbf1cbc01b7acabec7e"

validate_firmware_bin() {
  local label="$1"
  local path="$2"
  local expected_sha256="$3"
  if [[ ! -f "${path}" ]]; then
    echo "Required ${label} complete firmware image is missing: ${path}" >&2
    return 1
  fi
  local bytes
  bytes="$(/usr/bin/stat -f '%z' "${path}")"
  if [[ "${bytes}" != "8388608" ]]; then
    echo "${label} firmware image must be exactly 8 MiB; found ${bytes} bytes" >&2
    return 1
  fi
  local actual_sha256
  actual_sha256="$(env LC_ALL=C LANG=C /usr/bin/shasum -a 256 "${path}" | /usr/bin/awk '{print $1}')"
  if [[ "${actual_sha256}" != "${expected_sha256}" ]]; then
    echo "${label} firmware image hash mismatch: expected ${expected_sha256}, found ${actual_sha256}" >&2
    return 1
  fi
}

selection_pgo_args=()
if [[ -n "${release_pgo_profile}" ]]; then
  selection_pgo_args+=(--release-pgo-profile "${release_pgo_profile}")
fi
if [[ -n "${release_pgo_provenance}" ]]; then
  selection_pgo_args+=(--pgo-provenance "${release_pgo_provenance}")
fi
release_selection="$(python3 "${script_dir}/write_mdmm_receipt.py" \
  --source "${source_dir}" \
  --print-release-selection \
  --release-architectures "${release_architectures_input}" \
  --release-pgo-mode "${release_pgo_mode}" \
  ${selection_pgo_args[@]+"${selection_pgo_args[@]}"})"
if [[ "${release_selection}" != *"|"* ]]; then
  echo "Release selection helper returned an invalid result: ${release_selection}" >&2
  exit 2
fi
release_architectures="${release_selection%%|*}"
package_name="${release_selection#*|}"
IFS=';' read -r -a release_architecture_array <<< "${release_architectures}"

if [[ "${release_pgo_mode}" == "use" ]]; then
  release_pgo_profile="$(python3 -c \
    'import pathlib, sys; print(pathlib.Path(sys.argv[1]).resolve(strict=True))' \
    "${release_pgo_profile}")"
  release_pgo_provenance="$(python3 -c \
    'import pathlib, sys; print(pathlib.Path(sys.argv[1]).resolve(strict=True))' \
    "${release_pgo_provenance}")"
fi

expected_architecture_args=()
for architecture in "${release_architecture_array[@]}"; do
  expected_architecture_args+=(--expected-architecture "${architecture}")
done

pgo_cache_args=(
  -DGEARMULATOR_MDMM_APPLE_PGO_MODE="${release_pgo_mode}"
  -DGEARMULATOR_MDMM_APPLE_PGO_PROFILE="${release_pgo_profile}"
)
pgo_provenance_args=()
if [[ "${release_pgo_mode}" == "use" ]]; then
  pgo_provenance_args=(--pgo-provenance "${release_pgo_provenance}")
fi

python3 "${script_dir}/write_mdmm_receipt.py" \
  --source "${source_dir}" \
  --validate-build-root "${build_dir_input}" \
  --validate-output-root "${output_dir_input}"
if [[ "${require_firmware_tests}" != "0" && "${require_firmware_tests}" != "1" ]]; then
  echo "GEARMULATOR_REQUIRE_FIRMWARE_TESTS must be 0 or 1" >&2
  exit 2
fi
if [[ "${require_firmware_tests}" == "1" ]]; then
  if [[ -z "${md_firmware_bin}" || -z "${mm_firmware_bin}" ]]; then
    echo "Release verification requires GEARMULATOR_MD_FIRMWARE_BIN and GEARMULATOR_MM_FIRMWARE_BIN" >&2
    exit 2
  fi
  validate_firmware_bin "MD" "${md_firmware_bin}" "${md_firmware_bin_sha256}"
  validate_firmware_bin "MM" "${mm_firmware_bin}" "${mm_firmware_bin_sha256}"
fi

python3 "${script_dir}/write_mdmm_receipt.py" \
  --source "${source_dir}" \
  --prepare-build-root "${build_dir_input}" \
  --prepare-output-root "${output_dir_input}"

source_tuple_before="$(python3 "${script_dir}/write_mdmm_receipt.py" \
  --source "${source_dir}" \
  --check-source-only \
  --allow-untracked-root "${build_dir}/.gearmulator-mdmm-release-root" \
  --allow-untracked-root "${output_dir}/.gearmulator-mdmm-release-root")"

artifact_root="${build_dir}/products/Release"
md_app="${artifact_root}/Standalone/Gearmulator MD.app"
mm_app="${artifact_root}/Standalone/Gearmulator MM.app"
mdmm_app="${artifact_root}/Standalone/Maschine MD-MM.app"
md_vst3="${artifact_root}/VST3/Gearmulator MD.vst3"
mm_vst3="${artifact_root}/VST3/Gearmulator MM.vst3"
md_au="${artifact_root}/AU/Gearmulator MD.component"
mm_au="${artifact_root}/AU/Gearmulator MM.component"
build_runtime_home="${build_dir}/build-runtime-home"
build_runtime_data="${build_runtime_home}/Documents"
core_capacity_check="${build_dir}/mdmm-core-capacity.json"

cleanup_build_runtime_home() {
  rm -rf -- "${build_runtime_home}"
}

# The staged firmware is private test material. Remove it after success, after
# any failing command, and when an interactive build is interrupted.
trap cleanup_build_runtime_home EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

# JUCE executes each VST3 while generating its metadata. Keep that build-time
# process out of the caller's real Gearmulator folders. For a release build,
# give it only the same pinned firmware images used by the package smoke.
mkdir -p "${build_runtime_data}"
if [[ "${require_firmware_tests}" == "1" ]]; then
  md_build_rom_dir="${build_runtime_data}/Gearmulator Preview/Machinedrum/roms"
  mm_build_rom_dir="${build_runtime_data}/Gearmulator Preview/Monomachine/roms"
  mkdir -p "${md_build_rom_dir}" "${mm_build_rom_dir}"
  /usr/bin/ditto "${md_firmware_bin}" "${md_build_rom_dir}/validated-md.bin"
  /usr/bin/ditto "${mm_firmware_bin}" "${mm_build_rom_dir}/validated-mm.bin"
  validate_firmware_bin "staged MD" "${md_build_rom_dir}/validated-md.bin" \
    "${md_firmware_bin_sha256}"
  validate_firmware_bin "staged MM" "${mm_build_rom_dir}/validated-mm.bin" \
    "${mm_firmware_bin_sha256}"
fi

cmake -S "${source_dir}" -B "${build_dir}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES="${release_architectures}" \
  -DGEARMULATOR_MDMM_APPLE_THINLTO=ON \
  -DGEARMULATOR_MDMM_APPLE_OPTIMIZE_DSP=ON \
  "${pgo_cache_args[@]}" \
  -DGEARMULATOR_JUCE_PRODUCTS_ROOT="${build_dir}/products" \
  -DBUILD_TESTING=ON \
  -Dgearmulator_BUILD_JUCEPLUGIN=ON \
  -Dgearmulator_BUILD_FX_PLUGIN=OFF \
  -Dgearmulator_BUILD_JUCEPLUGIN_VST2=OFF \
  -Dgearmulator_BUILD_JUCEPLUGIN_VST3=ON \
  -Dgearmulator_BUILD_JUCEPLUGIN_CLAP=OFF \
  -Dgearmulator_BUILD_JUCEPLUGIN_LV2=OFF \
  -Dgearmulator_BUILD_JUCEPLUGIN_AU=ON \
  -Dgearmulator_BUILD_JUCEPLUGIN_Standalone=ON \
  -Dgearmulator_SYNTH_ELEKTRON=ON \
  -Dgearmulator_SYNTH_OSIRUS=OFF \
  -Dgearmulator_SYNTH_OSTIRUS=OFF \
  -Dgearmulator_SYNTH_VAVRA=OFF \
  -Dgearmulator_SYNTH_XENIA=OFF \
  -Dgearmulator_SYNTH_NODALRED2X=OFF \
  -Dgearmulator_SYNTH_JE8086=OFF

# Read back the generated cache. This prevents a renamed option, stale cache,
# or later CMake change from silently producing an ordinary Release package.
python3 "${script_dir}/write_mdmm_receipt.py" \
  --source "${source_dir}" \
  --validate-build-optimization "${build_dir}/CMakeCache.txt" \
  "${expected_architecture_args[@]}" \
  --expected-products-root "${build_dir}/products" \
  ${pgo_provenance_args[@]+"${pgo_provenance_args[@]}"}

HOME="${build_runtime_home}" GEARMULATOR_DATA_ROOT="${build_runtime_data}" \
cmake --build "${build_dir}" --parallel 4 --target \
  mdJucePlugin_VST3 \
  mmJucePlugin_VST3 \
  mdJucePlugin_AU \
  mmJucePlugin_AU \
  mdJucePlugin_Standalone \
  mmJucePlugin_Standalone \
  mdmmJucePlugin_Standalone \
  pluginTester \
  latency_host \
  baseLibBinaryStreamTest \
  synthLibAudioTest \
  mdLibTest \
  mdStateTest \
  mdFlashTest \
  mdUwFirmwareTest \
  mdAudioQueueTest \
  mdAudioFirmwareTest \
  mdAudioIoLayoutTest \
  mdProjectStateRestoreTest \
  mdAudioProbePlugin_VST3 \
  vst3ProgramChangeTest \
  mdProgramChangeProbe_VST3 \
  mdStandaloneRendererPolicyTest \
  mdPanelRenderingTest \
  juceRmlMouseInputTest \
  mdFrontPanelPresentationTest \
  mdCombinedMidiRouterTest \
  mdMaschineNihiaProtocolTest \
  mdMaschineScreenRendererTest \
  mdSectionFirmwareTest \
  mdRecordFirmwareTest \
  mdFirmwareImageTest \
  mc68kColdFireDivideTest

HOME="${build_runtime_home}" GEARMULATOR_DATA_ROOT="${build_runtime_data}" \
cmake --build "${build_dir}" --parallel 4 --target \
  midiOutputDispatcherTest \
  mdAutomationMidiTest \
  mdAutomationParameterTest \
  mdProgramChangeFirmwareTest \
  mdAutomationRobustnessTest

if [[ "${require_firmware_tests}" == "1" ]]; then
  HOME="${build_runtime_home}" GEARMULATOR_DATA_ROOT="${build_runtime_data}" \
  cmake --build "${build_dir}" --parallel 4 --target \
    mdAutomationFirmwareTest \
    mdAutomationSoakTest
fi

for test_name in \
  baseLibBinaryStreamTest \
  synthLibAudioTest \
  mdLibTests \
  mdStateTest \
  mdFlashTest \
  mdStandaloneRendererPolicyTest \
  mdPanelRenderingTest \
  juceRmlMouseInputTest \
  mdFrontPanelPresentationTests \
  mdCombinedMidiRouterTest \
  mdMaschineNihiaProtocolTest \
  mdMaschineScreenRendererTest \
  mdAudioQueueTest \
  mdAudioIoLayoutTest \
  mdAudioProbePluginVST3IdentityTest \
  mdVst3ProgramChangeTest \
  mdVst3ProgramChangeOptOutTest \
  mdJucePlugin_VST3ProgramChangeTest \
  mmJucePlugin_VST3ProgramChangeTest \
  mdFirmwareImageTest \
  mc68kColdFireDivideTest \
  midiOutputDispatcherTest \
  mdAutomationMidiTest \
  mdAutomationParameterTest \
  mdAutomationArchitectureTest; do
  HOME="${build_runtime_home}" GEARMULATOR_DATA_ROOT="${build_runtime_data}" \
    ctest --test-dir "${build_dir}" -C Release --output-on-failure \
      --no-tests=error --tests-regex "^${test_name}$"
done

if [[ "${require_firmware_tests}" == "1" ]]; then
  HOME="${build_runtime_home}" GEARMULATOR_DATA_ROOT="${build_runtime_data}" \
    GEARMULATOR_MD_FIRMWARE_BIN="${md_firmware_bin}" \
    GEARMULATOR_REQUIRE_FIRMWARE_TESTS=1 \
    ctest --test-dir "${build_dir}" -C Release --output-on-failure \
      --no-tests=error --tests-regex '^mdUwFirmwareTest$'
  GEARMULATOR_MD_FIRMWARE_BIN="${md_firmware_bin}" \
    GEARMULATOR_MM_FIRMWARE_BIN="${mm_firmware_bin}" \
    ctest --test-dir "${build_dir}" -C Release --output-on-failure \
      --no-tests=error --tests-regex '^(mdSectionFirmwareTest|mmSectionFirmwareTest|mmFourSectionFirmwareTest|mdRecordFirmwareTest|mdPadCursorFirmwareTest|mdPadCursor64FirmwareTest|mmRecordFirmwareTest|mmRecordedStepLedsFirmwareTest|mmRecordedStepPagesFirmwareTest|mdLcdDrumFirmwareTest)$'
  GEARMULATOR_MD_FIRMWARE_BIN="${md_firmware_bin}" \
    GEARMULATOR_MM_FIRMWARE_BIN="${mm_firmware_bin}" \
    ctest --test-dir "${build_dir}" -C Release --output-on-failure \
      --no-tests=error --tests-regex '^mdAudioFirmwareTest$'
  HOME="${build_runtime_home}" GEARMULATOR_DATA_ROOT="${build_runtime_data}" \
    GEARMULATOR_MD_FIRMWARE_BIN="${md_firmware_bin}" \
    GEARMULATOR_MM_FIRMWARE_BIN="${mm_firmware_bin}" \
    ctest --test-dir "${build_dir}" -C Release --output-on-failure \
      --no-tests=error --tests-regex '^mdProjectStateRestoreTest$'
  HOME="${build_runtime_home}" GEARMULATOR_DATA_ROOT="${build_runtime_data}" \
    MD_AUTOMATION_REQUIRE_FIRMWARE=1 \
    ctest --test-dir "${build_dir}" -C Release --output-on-failure \
      --no-tests=error \
      --tests-regex '^(mdAutomationFirmwareTest|mdProgramChangeFirmwareTest|mdAutomationRobustnessTest|mdAutomationSoakTest)$'
else
  HOME="${build_runtime_home}" GEARMULATOR_DATA_ROOT="${build_runtime_data}" \
    ctest --test-dir "${build_dir}" -C Release --output-on-failure \
      --no-tests=error \
      --tests-regex '^(mdUwFirmwareTest|mdAudioFirmwareTest)$'
fi

# Private fixtures are no longer needed after the firmware-backed executables
# complete. Never leave them in the persistent build tree or packaged products.
cleanup_build_runtime_home
# Recreate an empty sandbox for wrapper lifecycle checks. This both proves the
# project-state path works without private firmware and prevents plug-in probing
# from reading or writing the caller's real Gearmulator folders.
mkdir -p "${build_runtime_data}"

plugin_tester="${build_dir}/source/pluginTester/pluginTester_artefacts/Release/pluginTester"
latency_host="${build_dir}/source/pluginTester/latency/latency_host"

validate_binary_architectures() {
  local executable="$1"
  local actual_architectures
  actual_architectures="$(lipo -archs "${executable}")"
  local expected_architecture
  local actual_architecture
  local found
  for expected_architecture in "${release_architecture_array[@]}"; do
    if [[ " ${actual_architectures} " != *" ${expected_architecture} "* ]]; then
      echo "Binary is missing ${expected_architecture}: ${executable} (${actual_architectures})" >&2
      return 1
    fi
  done
  for actual_architecture in ${actual_architectures}; do
    found=0
    for expected_architecture in "${release_architecture_array[@]}"; do
      if [[ "${actual_architecture}" == "${expected_architecture}" ]]; then
        found=1
      fi
    done
    if [[ "${found}" != "1" ]]; then
      echo "Binary has unexpected architecture ${actual_architecture}: ${executable}" >&2
      return 1
    fi
  done
}

for bundle in "${md_vst3}" "${mm_vst3}"; do
  rm -f "${bundle}/Contents/Resources/moduleinfo.json"
done

for bundle in "${md_app}" "${mm_app}" "${md_vst3}" "${mm_vst3}" \
  "${md_au}" "${mm_au}"; do
  if [[ ! -d "${bundle}" ]]; then
    echo "Expected bundle is missing: ${bundle}" >&2
    exit 3
  fi
  codesign --force --deep --sign - "${bundle}"
  codesign --verify --deep --strict "${bundle}"
  executable="${bundle}/Contents/MacOS/$(basename "${bundle}" | sed -E 's/\.(app|vst3|component)$//')"
  validate_binary_architectures "${executable}"
done

validate_binary_architectures "${plugin_tester}"
validate_binary_architectures "${latency_host}"

if [[ ${#release_architecture_array[@]} -eq 1 ]]; then
  qualification_architecture="${release_architecture_array[0]}"
else
  qualification_architecture="$(/usr/bin/uname -m)"
fi
if [[ " ${release_architectures//;/ } " != *" ${qualification_architecture} "* ]]; then
  echo "Native qualification architecture is not in the package: ${qualification_architecture}" >&2
  exit 4
fi

if [[ "${require_firmware_tests}" == "1" ]]; then
  # Three unpaced measurements form a reproducible output-only core-capacity
  # microgate. Paced runs report tail behavior without claiming that a headless
  # selected-host run qualifies the other slice, physical input, or standalone UI.
  python3 "${script_dir}/check_mdmm_core_capacity.py" \
    --host "${latency_host}" \
    --host-architecture "${qualification_architecture}" \
    --md-plugin "${md_vst3}" \
    --mm-plugin "${mm_vst3}" \
    --md-firmware "${md_firmware_bin}" \
    --mm-firmware "${mm_firmware_bin}" \
    --work-root "${build_runtime_home}/core-capacity" \
    --output "${core_capacity_check}" \
    --rate 48000 \
    --block 128 \
    --seconds 20 \
    --warm-start-seconds 12 \
    --capacity-repeats 3 \
    --paced-repeats 3 \
    --capacity-p50-limit 0.90
fi

if [[ ! -x "${plugin_tester}" ]]; then
  echo "Expected VST3 host is missing: ${plugin_tester}" >&2
  exit 4
fi

for wrapper in "${md_vst3}" "${mm_vst3}"; do
  HOME="${build_runtime_home}" GEARMULATOR_DATA_ROOT="${build_runtime_data}" \
    "${plugin_tester}" -blocks 16 -verify-audio-buses -automation-smoke \
      -plugin "${wrapper}"
done

# AUv2 discovery is mediated by macOS's per-user AudioComponent registry; an
# uninstalled .component cannot be instantiated by path without mutating the
# caller's plug-in installation/cache. The loop above exercises the same plugin
# state implementation through VST3. AU bundles are still built, signed, and
# checked structurally here, with installed-host lifecycle covered by the
# bounded human smoke matrix.
plutil -lint "${md_au}/Contents/Info.plist" "${mm_au}/Contents/Info.plist"

cleanup_build_runtime_home
trap - EXIT HUP INT TERM

if find "${md_app}" "${mm_app}" "${mdmm_app}" "${md_vst3}" "${mm_vst3}" \
    "${md_au}" "${mm_au}" -type f \
    \( -iname '*.bin' -o -iname '*.rom' -o -iname '*.nvram' -o \
       -iname '*.syx' -o -iname '*.wav' -o -iname '*.cache' -o \
       -iname '*.mdpd' \) -print -quit | grep -q .; then
  echo "Firmware or private runtime material found in final bundles" >&2
  exit 5
fi

package_dir="${output_dir}/${package_name}"
mkdir -p "${package_dir}"
/usr/bin/ditto "${md_app}" "${package_dir}/Gearmulator MD.app"
/usr/bin/ditto "${mm_app}" "${package_dir}/Gearmulator MM.app"
/usr/bin/ditto "${mdmm_app}" "${package_dir}/Maschine MD-MM.app"
/usr/bin/ditto "${md_vst3}" "${package_dir}/Gearmulator MD.vst3"
/usr/bin/ditto "${mm_vst3}" "${package_dir}/Gearmulator MM.vst3"
/usr/bin/ditto "${md_au}" "${package_dir}/Gearmulator MD.component"
/usr/bin/ditto "${mm_au}" "${package_dir}/Gearmulator MM.component"
/usr/bin/ditto "${source_dir}/LICENSE.md" "${package_dir}/LICENSE.md"
/usr/bin/ditto "${script_dir}/macsetup_Gearmulator-Elektron.command" \
  "${package_dir}/macsetup_Gearmulator-Elektron.command"
/usr/bin/ditto "${script_dir}/INSTALL-macOS.txt" \
  "${package_dir}/INSTALL-macOS.txt"

archive="${output_dir}/${package_name}.zip"
/usr/bin/ditto -c -k --sequesterRsrc --keepParent "${package_dir}" "${archive}"
if [[ "${require_firmware_tests}" == "1" ]]; then
  "${script_dir}/verify_mdmm_package.sh" "${archive}" "${plugin_tester}" \
    "${md_firmware_bin}" "${mm_firmware_bin}"
else
  "${script_dir}/verify_mdmm_package.sh" "${archive}" "${plugin_tester}"
fi

receipt="${output_dir}/${package_name}-receipt.json"
core_capacity_receipt_args=()
if [[ "${require_firmware_tests}" == "1" ]]; then
  core_capacity_receipt_args=(--core-capacity-check "${core_capacity_check}")
fi
python3 "${script_dir}/write_mdmm_receipt.py" \
  --source "${source_dir}" \
  --expected-source-tuple "${source_tuple_before}" \
  --allow-untracked-root "${package_dir}" \
  --allow-untracked-root "${archive}" \
  --allow-untracked-root "${output_dir}/.gearmulator-mdmm-release-root" \
  --firmware-tests-required "${require_firmware_tests}" \
  --build-cache "${build_dir}/CMakeCache.txt" \
  "${expected_architecture_args[@]}" \
  --expected-products-root "${build_dir}/products" \
  ${pgo_provenance_args[@]+"${pgo_provenance_args[@]}"} \
  ${core_capacity_receipt_args[@]+"${core_capacity_receipt_args[@]}"} \
  --output "${receipt}" \
  --archive "${archive}" \
  --artifact "${package_dir}/Gearmulator MD.app" \
  --artifact "${package_dir}/Gearmulator MM.app" \
  --artifact "${package_dir}/Maschine MD-MM.app" \
  --artifact "${package_dir}/Gearmulator MD.vst3" \
  --artifact "${package_dir}/Gearmulator MM.vst3" \
  --artifact "${package_dir}/Gearmulator MD.component" \
  --artifact "${package_dir}/Gearmulator MM.component" \
  --package-file "${package_dir}/macsetup_Gearmulator-Elektron.command" \
  --package-file "${package_dir}/INSTALL-macOS.txt"

echo "MACOS_MDMM_ZIP=${archive}"
echo "MACOS_MDMM_RECEIPT=${receipt}"
env LC_ALL=C LANG=C /usr/bin/shasum -a 256 "${archive}"
