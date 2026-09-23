#!/bin/bash
# ============================================================================
#  Maschine MD-MM macOS Release Installer Builder
#
#  Builds installer artifacts for both Gearmulator MD and Gearmulator MM:
#    - Standalone -> /Applications/
#    - AU         -> /Library/Audio/Plug-Ins/Components/
#    - VST3       -> /Library/Audio/Plug-Ins/VST3/
#    - AAX        -> /Library/Application Support/Avid/Audio/Plug-Ins/
#
#  The release flow follows the Shine Reverb installer convention:
#    1) Build universal plugin artifacts with CMake
#    2) PACE wrap/sign AAX
#    3) Sign and validate all other bundles
#    4) Build selectable component packages and a product installer
#    5) Optionally notarize/staple the installer and DMG
#
#  Common environment overrides:
#    TEAM_ID=YOUR_APPLE_TEAM_ID
#    VERSION=0.1.0
#    CODESIGN_IDENTITY="Developer ID Application: ... (YOUR_APPLE_TEAM_ID)"
#    INSTALLER_IDENTITY="Developer ID Installer: ... (YOUR_APPLE_TEAM_ID)"
#    SKIP_SIGN=1
#    SKIP_NOTARIZE=1
#    CONTINUE_ON_NOTARIZE_FAILURE=1
#    NOTARIZE_PROFILE=your-keychain-profile
#    NOTARIZE_APPLE_ID=user@example.com
#    NOTARIZE_PASSWORD=xxxx-xxxx-xxxx-xxxx
#    NOTARIZE_DMG=1
#    JUCE_GLOBAL_AAX_SDK_PATH=/absolute/path/to/AAX_SDK
#    WRAPTOOL=/Applications/PACEAntiPiracy/Eden/Fusion/Versions/5/bin/wraptool
#    PACE_ACCOUNT=your-pace-account
#    PACE_SIGNID=YOUR_APPLE_TEAM_ID
#    MD_PACE_WCGUID=your-md-wrap-guid
#    MM_PACE_WCGUID=your-mm-wrap-guid
#    BUILD_CONFIG=Release
#    CMAKE_OSX_ARCHITECTURES="arm64;x86_64"
#    BUILD_JOBS=8
#    SKIP_PLUGIN_BUILD=1
#    SKIP_WRAP=1
#    SKIP_DMG=1
#    DMG_OUTPUT_DIR=/absolute/path/to/deliverables
#    MANUAL_PDF=/absolute/path/to/manual.pdf
#    INSTALLER_BACKGROUND=/absolute/path/to/background.png
# ============================================================================

set -euo pipefail

export COPYFILE_DISABLE=1

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
CMAKE_BUILD_DIR="${CMAKE_BUILD_DIR:-${GEARMULATOR_AAX_BUILD_DIR:-$PROJECT_ROOT/build/macos-mdmm-installer}}"
OUTPUT_DIR="${OUTPUT_DIR:-${GEARMULATOR_AAX_OUTPUT_DIR:-$PROJECT_ROOT/artifacts/macos-installer}}"
DMG_OUTPUT_DIR="${DMG_OUTPUT_DIR:-$PROJECT_ROOT}"

VERSION="${VERSION:-$(sed -n 's/^project(gearmulator VERSION \([^)]*\)).*/\1/p' "$PROJECT_ROOT/CMakeLists.txt")}"
TEAM_ID="${TEAM_ID:-}"
BUILD_CONFIG="${BUILD_CONFIG:-Release}"
CMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-$BUILD_CONFIG}"
CMAKE_OSX_ARCHITECTURES="${CMAKE_OSX_ARCHITECTURES:-arm64;x86_64}"
CMAKE_OSX_SYSROOT="${CMAKE_OSX_SYSROOT:-$(xcrun --sdk macosx --show-sdk-path 2>/dev/null || true)}"
BUILD_JOBS="${BUILD_JOBS:-8}"

SKIP_SIGN="${SKIP_SIGN:-0}"
SKIP_NOTARIZE="${SKIP_NOTARIZE:-${GEARMULATOR_AAX_SKIP_NOTARIZATION:-0}}"
CONTINUE_ON_NOTARIZE_FAILURE="${CONTINUE_ON_NOTARIZE_FAILURE:-0}"
NOTARIZE_PROFILE="${NOTARIZE_PROFILE:-${GEARMULATOR_AAX_NOTARY_PROFILE:-}}"
NOTARIZE_DMG="${NOTARIZE_DMG:-1}"

PACE_ACCOUNT="${PACE_ACCOUNT:-${GEARMULATOR_AAX_PACE_ACCOUNT:-}}"
PACE_SIGNID="${PACE_SIGNID:-$TEAM_ID}"
MD_PACE_WCGUID="${MD_PACE_WCGUID:-${GEARMULATOR_AAX_MD_WRAP_GUID:-}}"
MM_PACE_WCGUID="${MM_PACE_WCGUID:-${GEARMULATOR_AAX_MM_WRAP_GUID:-}}"
WRAPTOOL="${WRAPTOOL:-${GEARMULATOR_AAX_WRAPTOOL:-/Applications/PACEAntiPiracy/Eden/Fusion/Versions/5/bin/wraptool}}"
AAX_SDK_PATH="${JUCE_GLOBAL_AAX_SDK_PATH:-}"

PRODUCT_DISPLAY_NAME="${PRODUCT_DISPLAY_NAME:-Maschine MD-MM}"
PRODUCT_IDENTIFIER="${PRODUCT_IDENTIFIER:-com.djw.gearmulator.mdmm}"
INSTALLER_PRODUCT_NAME="${INSTALLER_PRODUCT_NAME:-Maschine MD-MM}"
FINAL_PKG_NAME="${FINAL_PKG_NAME:-${INSTALLER_PRODUCT_NAME} Installer ${VERSION}.pkg}"
DMG_NAME="${DMG_NAME:-${INSTALLER_PRODUCT_NAME} ${VERSION}.dmg}"
DMG_VOLUME_NAME="${DMG_VOLUME_NAME:-Maschine MD-MM}"
FINAL_PKG="$OUTPUT_DIR/$FINAL_PKG_NAME"
FINAL_DMG="$DMG_OUTPUT_DIR/$DMG_NAME"

PRODUCTS_ROOT="$CMAKE_BUILD_DIR/products"
BUILD_DIR="$PRODUCTS_ROOT/$BUILD_CONFIG"

is_truthy() {
  case "${1:-}" in
  1 | true | TRUE | yes | YES | on | ON) return 0 ;;
  *) return 1 ;;
  esac
}

die() {
  echo "Error: $*" >&2
  exit 1
}

require_command() {
  command -v "$1" >/dev/null 2>&1 || die "Required command not found: $1"
}

resolve_aax_sdk_path() {
  if [ -n "$AAX_SDK_PATH" ]; then
    [ -d "$AAX_SDK_PATH/Interfaces/ACF" ] ||
      die "Invalid JUCE_GLOBAL_AAX_SDK_PATH: $AAX_SDK_PATH (expected Interfaces/ACF)"
    return
  fi

  # Reuse a valid setting from an existing Gearmulator build when available.
  local cache_file cached_path
  for cache_file in \
    "$CMAKE_BUILD_DIR/CMakeCache.txt" \
    "$PROJECT_ROOT/build/CMakeCache.txt"; do
    [ -f "$cache_file" ] || continue
    cached_path="$(sed -n 's/^JUCE_GLOBAL_AAX_SDK_PATH:[^=]*=//p' "$cache_file" | tail -1)"
    if [ -n "$cached_path" ] && [ -d "$cached_path/Interfaces/ACF" ]; then
      AAX_SDK_PATH="$cached_path"
      return
    fi
  done

  # Match the conventional JUCE/AAX SDK layouts used by the other projects.
  local candidate
  for candidate in \
    "${HOME:-}/JUCE/modules/juce_audio_plugin_client/AAX/SDK" \
    "${HOME:-}/Developer/JUCE/modules/juce_audio_plugin_client/AAX/SDK" \
    "${HOME:-}/AAX_SDK" \
    "$PROJECT_ROOT/source/JUCE/modules/juce_audio_plugin_client/AAX/SDK"; do
    if [ -d "$candidate/Interfaces/ACF" ]; then
      AAX_SDK_PATH="$candidate"
      return
    fi
  done

  die "AAX SDK not found. Set JUCE_GLOBAL_AAX_SDK_PATH to the directory containing Interfaces/ACF"
}

bundle_executable() {
  local bundle="$1"
  local plist="$bundle/Contents/Info.plist"
  [ -f "$plist" ] || return 1
  local name
  name=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$plist" 2>/dev/null) || return 1
  printf '%s/Contents/MacOS/%s\n' "$bundle" "$name"
}

validate_bundle() {
  local bundle="$1"
  [ -d "$bundle" ] || die "Plugin bundle is missing: $bundle"
  plutil -lint "$bundle/Contents/Info.plist" >/dev/null || die "Malformed Info.plist: $bundle"
  local executable
  executable="$(bundle_executable "$bundle")" || die "CFBundleExecutable is missing: $bundle"
  [ -f "$executable" ] || die "Plugin executable is missing: $executable"

  if ! is_truthy "${SKIP_ARCH_CHECK:-0}"; then
    local actual_architectures
    actual_architectures="$(lipo -archs "$executable" 2>/dev/null || true)"
    [ -n "$actual_architectures" ] || die "Could not read architectures: $executable"
    local expected
    IFS=';' read -r -a expected_architectures <<< "$CMAKE_OSX_ARCHITECTURES"
    for expected in "${expected_architectures[@]}"; do
      [[ " $actual_architectures " == *" $expected "* ]] ||
        die "$bundle is missing required architecture $expected (found: $actual_architectures)"
    done
  fi
}

resolve_signing_identities() {
  [ -n "$TEAM_ID" ] || die "Set TEAM_ID for a signed release, or SKIP_SIGN=1 for an unsigned build."
  CODESIGN_IDENTITY="${CODESIGN_IDENTITY:-${GEARMULATOR_AAX_SIGN_ID:-}}"
  INSTALLER_IDENTITY="${INSTALLER_IDENTITY:-${GEARMULATOR_AAX_INSTALLER_SIGN_ID:-}}"
  if [ -z "$CODESIGN_IDENTITY" ]; then
    CODESIGN_IDENTITY=$(security find-identity -v -p codesigning |
      grep 'Developer ID Application' | grep "$TEAM_ID" | head -1 |
      sed -E 's/.*"([^"]+)".*/\1/' || true)
  fi
  if [ -z "$INSTALLER_IDENTITY" ]; then
    INSTALLER_IDENTITY=$(security find-identity -v |
      grep 'Developer ID Installer' | grep "$TEAM_ID" | head -1 |
      sed -E 's/.*"([^"]+)".*/\1/' || true)
  fi
  [ -n "$CODESIGN_IDENTITY" ] && [ -n "$INSTALLER_IDENTITY" ] ||
    die "Developer ID Application and Installer certificates for team $TEAM_ID were not found. Use SKIP_SIGN=1 for a local unsigned build."
}

run_cmake_plugin_build() {
  resolve_aax_sdk_path
  echo "AAX SDK:                    $AAX_SDK_PATH"

  echo "Configuring CMake..."
  cmake -S "$PROJECT_ROOT" -B "$CMAKE_BUILD_DIR" -G Xcode \
    -DCMAKE_BUILD_TYPE="$CMAKE_BUILD_TYPE" \
    -DCMAKE_OSX_ARCHITECTURES="$CMAKE_OSX_ARCHITECTURES" \
    -DCMAKE_OSX_SYSROOT="$CMAKE_OSX_SYSROOT" \
    -DGEARMULATOR_MDMM_APPLE_THINLTO=ON \
    -DGEARMULATOR_MDMM_APPLE_OPTIMIZE_DSP=ON \
    -DJUCE_GLOBAL_AAX_SDK_PATH="$AAX_SDK_PATH" \
    -DGEARMULATOR_JUCE_PRODUCTS_ROOT="$PRODUCTS_ROOT" \
    -DBUILD_TESTING=OFF \
    -Dgearmulator_BUILD_JUCEPLUGIN=ON \
    -Dgearmulator_BUILD_JUCEPLUGIN_AAX=ON \
    -Dgearmulator_BUILD_JUCEPLUGIN_Standalone=ON \
    -Dgearmulator_BUILD_JUCEPLUGIN_VST2=OFF \
    -Dgearmulator_BUILD_JUCEPLUGIN_VST3=ON \
    -Dgearmulator_BUILD_JUCEPLUGIN_CLAP=OFF \
    -Dgearmulator_BUILD_JUCEPLUGIN_LV2=OFF \
    -Dgearmulator_BUILD_JUCEPLUGIN_AU=ON \
    -Dgearmulator_BUILD_FX_PLUGIN=OFF \
    -Dgearmulator_SYNTH_ELEKTRON=ON \
    -Dgearmulator_SYNTH_OSIRUS=OFF \
    -Dgearmulator_SYNTH_OSTIRUS=OFF \
    -Dgearmulator_SYNTH_VAVRA=OFF \
    -Dgearmulator_SYNTH_XENIA=OFF \
    -Dgearmulator_SYNTH_NODALRED2X=OFF \
    -Dgearmulator_SYNTH_JE8086=OFF

  rm -rf "$BUILD_DIR/AAX/Gearmulator MD.wrapped.aaxplugin" \
    "$BUILD_DIR/AAX/Gearmulator MM.wrapped.aaxplugin"

  echo "Building plugin artifacts..."
  cmake --build "$CMAKE_BUILD_DIR" --config "$BUILD_CONFIG" \
    --parallel "$BUILD_JOBS" --target \
    mdJucePlugin_AAX mmJucePlugin_AAX \
    mdJucePlugin_AU mmJucePlugin_AU \
    mdJucePlugin_VST3 mmJucePlugin_VST3 \
    mdJucePlugin_Standalone mmJucePlugin_Standalone mdmmJucePlugin_Standalone
}

for command_name in cmake ditto hdiutil lipo pkgbuild pkgutil plutil productbuild security; do
  require_command "$command_name"
done
[ -n "$VERSION" ] || die "Could not read the project version"

echo "=== $PRODUCT_DISPLAY_NAME Release Builder ==="
echo "Project root: $PROJECT_ROOT"
echo "Build dir:    $BUILD_DIR"
echo "Output dir:   $OUTPUT_DIR"
echo "DMG output:   $DMG_OUTPUT_DIR"
echo "Version:      $VERSION"
echo "Architectures: $CMAKE_OSX_ARCHITECTURES"
echo ""

mkdir -p "$OUTPUT_DIR" "$DMG_OUTPUT_DIR"
rm -f "$FINAL_PKG" "$FINAL_DMG"

if ! is_truthy "$SKIP_SIGN"; then
  resolve_signing_identities
  echo "Code signing identity:      $CODESIGN_IDENTITY"
  echo "Installer signing identity: $INSTALLER_IDENTITY"
fi

if is_truthy "${SKIP_PLUGIN_BUILD:-0}"; then
  echo "Skipping plugin build (SKIP_PLUGIN_BUILD=1)."
else
  run_cmake_plugin_build
fi

MD_APP="$BUILD_DIR/Standalone/Gearmulator MD.app"
MM_APP="$BUILD_DIR/Standalone/Gearmulator MM.app"
MDMM_APP="$BUILD_DIR/Standalone/Maschine MD-MM.app"
MD_AU="$BUILD_DIR/AU/Gearmulator MD.component"
MM_AU="$BUILD_DIR/AU/Gearmulator MM.component"
MD_VST3="$BUILD_DIR/VST3/Gearmulator MD.vst3"
MM_VST3="$BUILD_DIR/VST3/Gearmulator MM.vst3"
MD_AAX="$BUILD_DIR/AAX/Gearmulator MD.aaxplugin"
MM_AAX="$BUILD_DIR/AAX/Gearmulator MM.aaxplugin"

for bundle in "$MD_APP" "$MM_APP" "$MDMM_APP" "$MD_AU" "$MM_AU" "$MD_VST3" "$MM_VST3" "$MD_AAX" "$MM_AAX"; do
  validate_bundle "$bundle"
done

WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/gearmulator-mdmm-installer.XXXXXX")"
DMG_DEVICE=""
cleanup() {
  if [ -n "$DMG_DEVICE" ]; then
    hdiutil detach "$DMG_DEVICE" >/dev/null 2>&1 ||
      hdiutil detach -force "$DMG_DEVICE" >/dev/null 2>&1 || true
  fi
  rm -rf -- "$WORK_DIR"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

JIT_ENTITLEMENTS="$WORK_DIR/jit.entitlements"
cat > "$JIT_ENTITLEMENTS" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "https://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>com.apple.security.cs.allow-jit</key><true/>
</dict></plist>
EOF

wrap_aax() {
  local input="$1"
  local output="$2"
  local wcguid="$3"
  if is_truthy "${SKIP_WRAP:-0}"; then
    echo "Skipping PACE wrapping for $(basename "$input") (SKIP_WRAP=1)."
    ditto --noextattr --norsrc "$input" "$output"
    return
  fi
  [ -x "$WRAPTOOL" ] || die "PACE wraptool was not found or is not executable: $WRAPTOOL"
  [ -n "$PACE_ACCOUNT" ] && [ -n "$PACE_SIGNID" ] && [ -n "$wcguid" ] ||
    die "Set PACE_ACCOUNT, PACE_SIGNID and the product's PACE wrap GUID, or use SKIP_WRAP=1."
  local clean_input="$WORK_DIR/input-$(basename "$input")"
  ditto --noextattr --norsrc "$input" "$clean_input"
  "$WRAPTOOL" sign --verbose \
    --account "$PACE_ACCOUNT" \
    --wcguid "$wcguid" \
    --signid "$PACE_SIGNID" \
    --dsigharden \
    --autoinstall on \
    --in "$clean_input" \
    --out "$output"
  "$WRAPTOOL" verify --verbose --in "$output"
}

STAGING="$WORK_DIR/staging"
PKG_OUTPUT="$WORK_DIR/packages"
RESOURCES="$WORK_DIR/resources"
mkdir -p "$STAGING/apps/Applications" \
  "$STAGING/au/Library/Audio/Plug-Ins/Components" \
  "$STAGING/vst3/Library/Audio/Plug-Ins/VST3" \
  "$STAGING/aax/Library/Application Support/Avid/Audio/Plug-Ins" \
  "$PKG_OUTPUT" "$RESOURCES"

ditto --noextattr --norsrc "$MD_APP" "$STAGING/apps/Applications/Gearmulator MD.app"
ditto --noextattr --norsrc "$MM_APP" "$STAGING/apps/Applications/Gearmulator MM.app"
ditto --noextattr --norsrc "$MDMM_APP" "$STAGING/apps/Applications/Maschine MD-MM.app"
ditto --noextattr --norsrc "$MD_AU" "$STAGING/au/Library/Audio/Plug-Ins/Components/Gearmulator MD.component"
ditto --noextattr --norsrc "$MM_AU" "$STAGING/au/Library/Audio/Plug-Ins/Components/Gearmulator MM.component"
ditto --noextattr --norsrc "$MD_VST3" "$STAGING/vst3/Library/Audio/Plug-Ins/VST3/Gearmulator MD.vst3"
ditto --noextattr --norsrc "$MM_VST3" "$STAGING/vst3/Library/Audio/Plug-Ins/VST3/Gearmulator MM.vst3"
wrap_aax "$MD_AAX" "$STAGING/aax/Library/Application Support/Avid/Audio/Plug-Ins/Gearmulator MD.aaxplugin" "$MD_PACE_WCGUID"
wrap_aax "$MM_AAX" "$STAGING/aax/Library/Application Support/Avid/Audio/Plug-Ins/Gearmulator MM.aaxplugin" "$MM_PACE_WCGUID"

if ! is_truthy "$SKIP_SIGN"; then
  for bundle in \
    "$STAGING/apps/Applications/Gearmulator MD.app" \
    "$STAGING/apps/Applications/Gearmulator MM.app" \
    "$STAGING/apps/Applications/Maschine MD-MM.app" \
    "$STAGING/au/Library/Audio/Plug-Ins/Components/Gearmulator MD.component" \
    "$STAGING/au/Library/Audio/Plug-Ins/Components/Gearmulator MM.component" \
    "$STAGING/vst3/Library/Audio/Plug-Ins/VST3/Gearmulator MD.vst3" \
    "$STAGING/vst3/Library/Audio/Plug-Ins/VST3/Gearmulator MM.vst3"; do
    codesign --force --deep --timestamp --options runtime \
      --entitlements "$JIT_ENTITLEMENTS" --sign "$CODESIGN_IDENTITY" "$bundle"
    codesign --verify --deep --strict --verbose=2 "$bundle"
  done
fi

for bundle in \
  "$STAGING/aax/Library/Application Support/Avid/Audio/Plug-Ins/Gearmulator MD.aaxplugin" \
  "$STAGING/aax/Library/Application Support/Avid/Audio/Plug-Ins/Gearmulator MM.aaxplugin"; do
  validate_bundle "$bundle"
  if ! is_truthy "$SKIP_SIGN" && ! is_truthy "${SKIP_WRAP:-0}"; then
    codesign --verify --deep --strict --verbose=2 "$bundle"
  fi
done

make_preinstall() {
  local directory="$1"
  local install_directory="$2"
  shift 2
  mkdir -p "$directory"
  {
    echo '#!/bin/bash'
    echo 'set -euo pipefail'
    local bundle_name
    for bundle_name in "$@"; do
      printf 'rm -rf %q\n' "$install_directory/$bundle_name"
      printf 'for user_home in /Users/*; do [ -d "$user_home" ] || continue; rm -rf "$user_home%s/%s"; done\n' "$install_directory" "$bundle_name"
      printf 'rm -rf %q\n' "/var/root$install_directory/$bundle_name"
    done
  } > "$directory/preinstall"
  chmod 755 "$directory/preinstall"
}

make_preinstall "$WORK_DIR/scripts/apps" "/Applications" \
  "Gearmulator MD.app" "Gearmulator MM.app" "Maschine MD-MM.app"
make_preinstall "$WORK_DIR/scripts/au" "/Library/Audio/Plug-Ins/Components" \
  "Gearmulator MD.component" "Gearmulator MM.component"
make_preinstall "$WORK_DIR/scripts/vst3" "/Library/Audio/Plug-Ins/VST3" \
  "Gearmulator MD.vst3" "Gearmulator MM.vst3"
make_preinstall "$WORK_DIR/scripts/aax" "/Library/Application Support/Avid/Audio/Plug-Ins" \
  "Gearmulator MD.aaxplugin" "Gearmulator MM.aaxplugin"

cat > "$WORK_DIR/scripts/au/postinstall" <<'EOF'
#!/bin/bash
killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true
EOF
chmod 755 "$WORK_DIR/scripts/au/postinstall"

find "$STAGING" -name '._*' -type f -delete
/usr/bin/xattr -cr "$STAGING" 2>/dev/null || true

if find "$STAGING" -type f \
    \( -iname '*.bin' -o -iname '*.rom' -o -iname '*.nvram' -o \
       -iname '*.syx' -o -iname '*.wav' -o -iname '*.cache' -o \
       -iname '*.mdpd' \) -print -quit | grep -q .; then
  die "Firmware or private runtime material was found in the installer payload"
fi

run_pkgbuild() {
  local key="$1"
  local args=(--root "$STAGING/$key"
    --identifier "$PRODUCT_IDENTIFIER.$key"
    --version "$VERSION"
    --install-location "/"
    --scripts "$WORK_DIR/scripts/$key")
  if ! is_truthy "$SKIP_SIGN"; then
    args+=(--sign "$INSTALLER_IDENTITY" --timestamp)
  fi
  pkgbuild "${args[@]}" "$PKG_OUTPUT/$key.pkg"
}

for component in apps au vst3 aax; do
  run_pkgbuild "$component"
done

BACKGROUND_XML=""
if [ -n "${INSTALLER_BACKGROUND:-}" ]; then
  [ -f "$INSTALLER_BACKGROUND" ] || die "Installer background not found: $INSTALLER_BACKGROUND"
  ditto --noextattr --norsrc "$INSTALLER_BACKGROUND" "$RESOURCES/background.png"
  BACKGROUND_XML=$'    <background file="background.png" alignment="bottomleft" scaling="proportional"/>\n    <background-darkAqua file="background.png" alignment="bottomleft" scaling="proportional"/>'
fi

cat > "$RESOURCES/welcome.html" <<EOF
<!DOCTYPE html><html><head><meta charset="utf-8"></head>
<body style="font-family: -apple-system, Helvetica Neue, sans-serif; padding: 20px;">
<h1>$PRODUCT_DISPLAY_NAME</h1>
<p>Welcome to the $PRODUCT_DISPLAY_NAME installer.</p>
<p>Choose the standalone applications and plug-in formats you want to install.</p>
<p><strong>Firmware is not included.</strong> Each product will ask for a firmware image that you are entitled to use.</p>
<p style="color: #666; font-size: 12px;">Version $VERSION</p>
</body></html>
EOF

HOST_ARCHITECTURES="${CMAKE_OSX_ARCHITECTURES//;/,}"
cat > "$WORK_DIR/distribution.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>$PRODUCT_DISPLAY_NAME</title>
    <organization>com.djw</organization>
    <welcome file="welcome.html"/>
$BACKGROUND_XML
    <options customize="allow" require-scripts="false" hostArchitectures="$HOST_ARCHITECTURES"/>
    <choices-outline>
        <line choice="apps"/><line choice="au"/><line choice="vst3"/><line choice="aax"/>
    </choices-outline>
    <choice id="apps" title="Standalone Applications"><pkg-ref id="$PRODUCT_IDENTIFIER.apps"/></choice>
    <choice id="au" title="Audio Units"><pkg-ref id="$PRODUCT_IDENTIFIER.au"/></choice>
    <choice id="vst3" title="VST3"><pkg-ref id="$PRODUCT_IDENTIFIER.vst3"/></choice>
    <choice id="aax" title="AAX (Pro Tools)"><pkg-ref id="$PRODUCT_IDENTIFIER.aax"/></choice>
    <pkg-ref id="$PRODUCT_IDENTIFIER.apps" version="$VERSION">apps.pkg</pkg-ref>
    <pkg-ref id="$PRODUCT_IDENTIFIER.au" version="$VERSION">au.pkg</pkg-ref>
    <pkg-ref id="$PRODUCT_IDENTIFIER.vst3" version="$VERSION">vst3.pkg</pkg-ref>
    <pkg-ref id="$PRODUCT_IDENTIFIER.aax" version="$VERSION">aax.pkg</pkg-ref>
</installer-gui-script>
EOF

PRODUCTBUILD_ARGS=(--distribution "$WORK_DIR/distribution.xml" --resources "$RESOURCES" --package-path "$PKG_OUTPUT")
if ! is_truthy "$SKIP_SIGN"; then
  PRODUCTBUILD_ARGS+=(--sign "$INSTALLER_IDENTITY" --timestamp)
fi
productbuild "${PRODUCTBUILD_ARGS[@]}" "$FINAL_PKG"
if ! is_truthy "$SKIP_SIGN"; then
  pkgutil --check-signature "$FINAL_PKG"
fi

EFFECTIVE_NOTARIZE_PROFILE="${NOTARIZE_PROFILE:-${NOTARY_PROFILE:-}}"
EFFECTIVE_NOTARIZE_APPLE_ID="${NOTARIZE_APPLE_ID:-${APPLE_ID:-${AC_USERNAME:-}}}"
EFFECTIVE_NOTARIZE_PASSWORD="${NOTARIZE_PASSWORD:-${APP_SPECIFIC_PASSWORD:-${APPLE_APP_PASSWORD:-${AC_PASSWORD:-}}}}"
EFFECTIVE_NOTARIZE_TEAM_ID="${NOTARIZE_TEAM_ID:-${AC_TEAM_ID:-$TEAM_ID}}"
NOTARY_ARGS=()
if [ -n "$EFFECTIVE_NOTARIZE_PROFILE" ]; then
  NOTARY_ARGS=(--keychain-profile "$EFFECTIVE_NOTARIZE_PROFILE")
elif [ -n "$EFFECTIVE_NOTARIZE_APPLE_ID" ] && [ -n "$EFFECTIVE_NOTARIZE_PASSWORD" ]; then
  NOTARY_ARGS=(--apple-id "$EFFECTIVE_NOTARIZE_APPLE_ID" --password "$EFFECTIVE_NOTARIZE_PASSWORD" --team-id "$EFFECTIVE_NOTARIZE_TEAM_ID")
fi

notarize_and_staple() {
  local artifact="$1"
  echo "Submitting $(basename "$artifact") to Apple notary service..."
  xcrun notarytool submit "$artifact" "${NOTARY_ARGS[@]}" --wait
  xcrun stapler staple "$artifact"
  xcrun stapler validate "$artifact"
}

if is_truthy "$SKIP_NOTARIZE"; then
  echo "Notarization skipped (SKIP_NOTARIZE=1)."
elif is_truthy "$SKIP_SIGN"; then
  echo "Notarization skipped because signing is disabled."
elif [ ${#NOTARY_ARGS[@]} -eq 0 ]; then
  echo "Notarization skipped because no usable credentials were provided."
elif ! notarize_and_staple "$FINAL_PKG"; then
  if ! is_truthy "$CONTINUE_ON_NOTARIZE_FAILURE"; then
    die "Package notarization failed"
  fi
  echo "Continuing without notarization (CONTINUE_ON_NOTARIZE_FAILURE=1)."
  SKIP_NOTARIZE=1
fi

if ! is_truthy "${SKIP_DMG:-0}"; then
  DMG_STAGING="$WORK_DIR/dmg"
  DMG_MOUNT="$WORK_DIR/dmg-mount"
  DMG_RW_IMAGE="$WORK_DIR/dmg-rw.sparseimage"
  mkdir -p "$DMG_STAGING"
  ditto --noextattr --norsrc "$FINAL_PKG" "$DMG_STAGING/$FINAL_PKG_NAME"
  ditto --noextattr --norsrc "$PROJECT_ROOT/LICENSE.md" "$DMG_STAGING/LICENSE.md"
  if [ -n "${MANUAL_PDF:-}" ]; then
    [ -f "$MANUAL_PDF" ] || die "Manual PDF not found: $MANUAL_PDF"
    ditto --noextattr --norsrc "$MANUAL_PDF" "$DMG_STAGING/$(basename "$MANUAL_PDF")"
  fi

  # hdiutil's automatic -srcfolder sizing can underestimate notarized packages
  # with tickets and extended metadata. Build a generously sized writable image
  # at a private mount point, then compress it into the release DMG.
  DMG_STAGING_KIB="$(du -sk "$DMG_STAGING" | awk '{print $1}')"
  DMG_SIZE_MIB=$(((DMG_STAGING_KIB * 5 / 4 + 1023) / 1024 + 32))
  echo "Building ${DMG_SIZE_MIB} MiB DMG image..."
  hdiutil create -size "${DMG_SIZE_MIB}m" -fs HFS+ \
    -volname "$DMG_VOLUME_NAME" -type SPARSE -ov "$DMG_RW_IMAGE" >/dev/null
  mkdir -p "$DMG_MOUNT"
  DMG_DEVICE="$(hdiutil attach -nobrowse -noverify -mountpoint "$DMG_MOUNT" \
    "$DMG_RW_IMAGE" | awk '/^\/dev\// { print $1; exit }')"
  [ -n "$DMG_DEVICE" ] || die "Could not attach temporary DMG image"
  ditto --noextattr --norsrc "$DMG_STAGING" "$DMG_MOUNT"
  sync
  hdiutil detach "$DMG_DEVICE" >/dev/null
  DMG_DEVICE=""
  hdiutil convert "$DMG_RW_IMAGE" -format UDZO -ov -o "$FINAL_DMG" >/dev/null
  [ -f "$FINAL_DMG" ] || die "DMG creation did not produce: $FINAL_DMG"
  if ! is_truthy "$SKIP_SIGN"; then
    codesign --force --timestamp --sign "$CODESIGN_IDENTITY" "$FINAL_DMG"
  fi
  if ! is_truthy "$SKIP_SIGN" && ! is_truthy "$SKIP_NOTARIZE" && is_truthy "$NOTARIZE_DMG" && [ ${#NOTARY_ARGS[@]} -gt 0 ]; then
    notarize_and_staple "$FINAL_DMG"
  fi
fi

echo ""
echo "=== Done ==="
echo "Installer: $FINAL_PKG"
echo "MDMM_INSTALLER=$FINAL_PKG"
if ! is_truthy "${SKIP_DMG:-0}"; then
  echo "DMG:       $FINAL_DMG"
  echo "MDMM_DMG=$FINAL_DMG"
fi
