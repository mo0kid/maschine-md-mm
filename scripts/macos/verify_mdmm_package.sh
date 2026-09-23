#!/bin/bash

set -euo pipefail

archive="${1:?usage: verify_mdmm_package.sh ARCHIVE PLUGIN_TESTER [MD_FIRMWARE_BIN MM_FIRMWARE_BIN]}"
plugin_tester="${2:?usage: verify_mdmm_package.sh ARCHIVE PLUGIN_TESTER [MD_FIRMWARE_BIN MM_FIRMWARE_BIN]}"
md_firmware_bin="${3:-}"
mm_firmware_bin="${4:-}"
readonly md_firmware_bin_sha256="68542e30917b9918ccaee2b2237df62c8a00479938680b85aca93ce4fbca44c8"
readonly mm_firmware_bin_sha256="369849175602e20a9dd2b6e0ad8ac404b76f82718b14afbf1cbc01b7acabec7e"
readonly smoke_blocks=256

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

run_packaged_firmware_smoke() {
  local label="$1"
  local data_folder="$2"
  local firmware_bin="$3"
  local expected_sha256="$4"
  local plugin="$5"
  local smoke_home="$6"
  local rom_dir="${smoke_home}/Documents/Gearmulator Preview/${data_folder}/roms"
  local staged_firmware="${rom_dir}/validated-${label}.bin"
  local smoke_log="${smoke_home}/plugin-smoke.log"

  mkdir -p "${rom_dir}"
  /usr/bin/ditto "${firmware_bin}" "${staged_firmware}"
  validate_firmware_bin "${label}" "${staged_firmware}" "${expected_sha256}"

  # The stock host proves exact-module loading, device creation, and callback
  # execution. It cannot inject MIDI, measure output, or query private DSP state.
  if ! HOME="${smoke_home}" GEARMULATOR_DATA_ROOT="${smoke_home}/Documents" \
      "${plugin_tester}" -verify-audio-buses -blocks "${smoke_blocks}" \
      -plugin "${plugin}" \
      >"${smoke_log}" 2>&1; then
    /bin/cat "${smoke_log}" >&2
    echo "${label} extracted-package firmware smoke failed" >&2
    return 1
  fi
  /bin/cat "${smoke_log}"
  if /usr/bin/grep -Eiq \
      'Failed to create device|Device Initialization failed|firmware rom .* required|DSP execution fault' \
      "${smoke_log}"; then
    echo "${label} extracted package did not complete a clean firmware-backed device start" >&2
    return 1
  fi
  if ! /usr/bin/grep -Fq "Progress: 100% (${smoke_blocks}/${smoke_blocks} blocks)" \
      "${smoke_log}"; then
    echo "${label} extracted package did not process all requested audio blocks" >&2
    return 1
  fi
}

if [[ ! -f "${archive}" ]]; then
  echo "Package archive does not exist: ${archive}" >&2
  exit 2
fi
if [[ ! -x "${plugin_tester}" ]]; then
  echo "Plugin tester does not exist or is not executable: ${plugin_tester}" >&2
  exit 2
fi
if [[ -n "${md_firmware_bin}" || -n "${mm_firmware_bin}" ]]; then
  if [[ -z "${md_firmware_bin}" || -z "${mm_firmware_bin}" ]]; then
    echo "Both MD and MM complete firmware images are required for firmware-backed package verification" >&2
    exit 2
  fi
  validate_firmware_bin "MD" "${md_firmware_bin}" "${md_firmware_bin_sha256}"
  validate_firmware_bin "MM" "${mm_firmware_bin}" "${mm_firmware_bin_sha256}"
fi

verification_root="$(mktemp -d "${TMPDIR:-/tmp}/gearmulator-mdmm-package.XXXXXX")"
trap 'rm -rf "${verification_root}"' EXIT

/usr/bin/ditto -x -k "${archive}" "${verification_root}"
shopt -s nullglob
package_dirs=("${verification_root}"/Maschine-MD-MM-macOS-*)
shopt -u nullglob
if [[ ${#package_dirs[@]} -ne 1 || ! -d "${package_dirs[0]}" ]]; then
  echo "Expected exactly one extracted package directory, found ${#package_dirs[@]}" >&2
  exit 3
fi
package_dir="${package_dirs[0]}"
setup_command="${package_dir}/macsetup_Maschine-MD-MM.command"
install_guide="${package_dir}/INSTALL-macOS.txt"

if find "${package_dir}" -type f \
    \( -iname '*.bin' -o -iname '*.rom' -o -iname '*.nvram' -o \
       -iname '*.syx' -o -iname '*.wav' -o -iname '*.cache' -o \
       -iname '*.mdpd' \) -print -quit | grep -q .; then
  echo "Firmware or private runtime material found in extracted package" >&2
  exit 3
fi

if [[ ! -x "${setup_command}" ]]; then
  echo "Setup command is missing or is not executable: ${setup_command}" >&2
  exit 3
fi
if [[ ! -f "${install_guide}" ]]; then
  echo "Installation guide is missing: ${install_guide}" >&2
  exit 3
fi

bundles=(
  "${package_dir}/Maschine MD-MM.app"
  "${package_dir}/Gearmulator MD.app"
  "${package_dir}/Gearmulator MM.app"
  "${package_dir}/Gearmulator MD.vst3"
  "${package_dir}/Gearmulator MM.vst3"
  "${package_dir}/Gearmulator MD.component"
  "${package_dir}/Gearmulator MM.component"
)
for bundle in "${bundles[@]}"; do
  if [[ ! -d "${bundle}" ]]; then
    echo "Expected bundle is missing from extracted package: ${bundle}" >&2
    exit 3
  fi
  executable="${bundle}/Contents/MacOS/$(basename "${bundle}" | sed -E 's/\.(app|vst3|component)$//')"
  if [[ ! -f "${executable}" ]]; then
    echo "Expected bundle executable is missing: ${executable}" >&2
    exit 3
  fi
  if /usr/bin/nm -u "${executable}" | /usr/bin/grep -Fq \
      'pmr20get_default_resource'; then
    echo "Bundle imports the macOS-14-only std::pmr runtime: ${bundle}" >&2
    exit 4
  fi
  /usr/bin/codesign --verify --deep --strict "${bundle}"
done

quarantined_paths=("${setup_command}")
for bundle in "${bundles[@]}"; do
  executable="${bundle}/Contents/MacOS/$(basename "${bundle}" | sed -E 's/\.(app|vst3|component)$//')"
  quarantined_paths+=("${bundle}" "${executable}")
done
for path in "${quarantined_paths[@]}"; do
  /usr/bin/xattr -w com.apple.quarantine \
    "0081;00000000;GearmulatorPackageTest;" "${path}"
done

"${setup_command}"

for path in "${quarantined_paths[@]}"; do
  if /usr/bin/xattr -p com.apple.quarantine "${path}" >/dev/null 2>&1; then
    echo "Setup command left quarantine metadata on: ${path}" >&2
    exit 4
  fi
done
for bundle in "${bundles[@]}"; do
  /usr/bin/codesign --verify --deep --strict "${bundle}"
done
/usr/bin/plutil -lint \
  "${package_dir}/Gearmulator MD.component/Contents/Info.plist" \
  "${package_dir}/Gearmulator MM.component/Contents/Info.plist"

md_smoke_home="${verification_root}/smoke-home-md"
mm_smoke_home="${verification_root}/smoke-home-mm"
if [[ -n "${md_firmware_bin}" ]]; then
  run_packaged_firmware_smoke "MD" "Machinedrum" "${md_firmware_bin}" \
    "${md_firmware_bin_sha256}" "${package_dir}/Gearmulator MD.vst3" "${md_smoke_home}"
  run_packaged_firmware_smoke "MM" "Monomachine" "${mm_firmware_bin}" \
    "${mm_firmware_bin_sha256}" "${package_dir}/Gearmulator MM.vst3" "${mm_smoke_home}"
else
  mkdir -p "${md_smoke_home}/Documents" "${mm_smoke_home}/Documents"
  HOME="${md_smoke_home}" GEARMULATOR_DATA_ROOT="${md_smoke_home}/Documents" \
    "${plugin_tester}" -verify-audio-buses -blocks 16 \
    -plugin "${package_dir}/Gearmulator MD.vst3"
  HOME="${mm_smoke_home}" GEARMULATOR_DATA_ROOT="${mm_smoke_home}/Documents" \
    "${plugin_tester}" -verify-audio-buses -blocks 16 \
    -plugin "${package_dir}/Gearmulator MM.vst3"
fi

echo "Verified extracted MD/MM package: ${archive}"
