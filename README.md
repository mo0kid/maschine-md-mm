# Maschine MD-MM

Machinedrum and Monomachine emulation with Maschine MK3 integration. This is
an independent fork of
[joelanders/gearmulator-md-mm](https://github.com/joelanders/gearmulator-md-mm)
and The Usual Suspects' Gearmulator project.

This project is not affiliated with Elektron, Native Instruments or The Usual
Suspects. Please report fork-specific issues here, not to the upstream projects.

[Downloads](https://github.com/mo0kid/maschine-md-mm/releases) ·
[Report a bug](https://github.com/mo0kid/maschine-md-mm/issues)

## Features

- **Combined MD–MM app:** run Machinedrum and Monomachine together in one
  standalone window. The separate MD and MM apps and plug-ins remain available.
- **Maschine MK3 integration (macOS):** the two hardware screens show the
  emulated displays, the controls operate the focused instrument, and the
  strip lights mirror its steps and sections. The combined app is required.
- **Key chording / p-locks:** shift-click one or more buttons to hold them
  down until you release the shift key.
- **Secondary functions:** rather than shift-click Function and another button,
  you can just click the secondary function text label.
- **Encoder clicking:** Alt/Option-click a DATA ENTRY encoder to press it, or
  Alt/Option-drag to press and turn. With a trig held, pressing its parameter's
  encoder toggles that parameter lock. This applies to encoders A–H, not LEVEL
  or SOUND SELECTION.
- **Send SysEx File** remains available from the right-click menu in the
  separate MD and MM apps and plug-ins. The combined Maschine app does not
  show right-click menus. Follow the machine's normal receive procedure.
- **Panel look and feel:** adjust encoder-drag and mouse-wheel sensitivity in settings.
  An experimental crisp LCD/panel rendering option is also available.
- **Audio inputs and outputs:** route host audio to the machine's input effects or sampling
  functions. Additional output pairs are available in a multi-output VST3 host;
  the standalone apps use stereo output.

## Install on macOS

The ZIP works without an Apple Developer account. Download it from
[Releases](https://github.com/mo0kid/maschine-md-mm/releases), or build it using
the steps below. Fully extract the archive and run
`macsetup_Maschine-MD-MM.command` from the extracted folder. Then copy
`Maschine MD-MM.app` to `/Applications` (or run it from the extracted folder).
This is the combined app needed for Maschine MK3 screen and control integration.
If you want the separate plug-ins, copy their `.vst3` bundles to
`~/Library/Audio/Plug-Ins/VST3` and their `.component` bundles to
`~/Library/Audio/Plug-Ins/Components`. Restart your DAW or rescan plug-ins.
The setup command prepares the downloaded bundles; it does not install them.
Because the ZIP is signed locally and is not notarized, macOS may ask you to
confirm opening its apps or setup command.

If a signed DMG is available instead, open it and run the installer package
inside. Select the standalone apps and any plug-in formats you use. The
separate Gearmulator MD and MM apps and plug-ins are optional.

Firmware is not distributed with either download. On first launch without
existing firmware, the app shows the MD and MM firmware folders. Copy firmware
images that you are entitled to use into those folders, then relaunch.

## Build a ZIP without an Apple Developer account

Install Xcode's command-line tools, CMake, Python 3, and Git. Clone with
submodules so the pinned JUCE and DSP changes are present:

```sh
git clone --recurse-submodules https://github.com/mo0kid/maschine-md-mm.git
cd maschine-md-mm
```

From a clean Git checkout, build a universal ZIP with no signing certificates,
PACE tools, or AAX SDK. You do not need firmware images to compile it:

```sh
GEARMULATOR_REQUIRE_FIRMWARE_TESTS=0 ./scripts/macos/build_mdmm.sh
```

The script builds both architectures, runs the tests that do not require
firmware, and signs the bundles locally (ad hoc). It does not notarize them.
The ZIP is written to
`artifacts/macos-mdmm-universal/Maschine-MD-MM-macOS-Universal.zip`; the combined
app is also available directly at
`build/macos-mdmm-universal/products/Release/Standalone/Maschine MD-MM.app`.
Install the ZIP as described above, then supply your own firmware on first
launch. This build is useful for development, but it has not passed the
firmware-backed release tests.

For a fully verified ZIP, obtain complete 8 MiB MD and MM firmware images
matching the hashes checked by the script, then supply their absolute paths
outside the repository:

```sh
GEARMULATOR_MD_FIRMWARE_BIN="/absolute/path/to/md.bin" \
GEARMULATOR_MM_FIRMWARE_BIN="/absolute/path/to/mm.bin" \
./scripts/macos/build_mdmm.sh
```

This runs the additional firmware-backed tests and produces the same ZIP plus
a build receipt. Neither ZIP build requires an Apple Developer account. A
Windows release package for the combined app is not yet provided.

## Build a signed DMG (maintainers)

Maintainers can run `scripts/macos/build_installer.sh` to produce a signed
installer package in `artifacts/macos-installer/` and a DMG in the project
root. This requires Xcode, Developer ID Application and Installer certificates,
and a `NOTARIZE_PROFILE` stored in Keychain. To make a signed and notarized DMG
without the optional AAX plug-ins or PACE tools, run:

```sh
SKIP_AAX=1 TEAM_ID="YOUR_APPLE_TEAM_ID" \
NOTARIZE_PROFILE="YOUR_KEYCHAIN_PROFILE" \
./scripts/macos/build_installer.sh
```

To include the separate MD/MM AAX plug-ins, also provide the AAX SDK, PACE
wraptool and account, and both wrap GUIDs:

```sh
TEAM_ID="YOUR_APPLE_TEAM_ID" \
JUCE_GLOBAL_AAX_SDK_PATH="/absolute/path/to/AAX_SDK" \
PACE_ACCOUNT="YOUR_PACE_ACCOUNT" \
MD_PACE_WCGUID="YOUR_MD_WRAP_GUID" \
MM_PACE_WCGUID="YOUR_MM_WRAP_GUID" \
NOTARIZE_PROFILE="YOUR_KEYCHAIN_PROFILE" \
./scripts/macos/build_installer.sh
```

The script does not upload anything. See the [release checklist](doc/mdmm_release.md)
for signing, notarization, and final hardware checks.

## Implementation references

- [TurboMIDI negotiation](doc/turbomidi.md): a worked exchange, firmware observations,
  and Gearmulator sender policy.

## Versioning and attribution

This fork starts its own release series at **0.1.0**; it does not use Gearmulator's
release numbering. The version is defined in the root `CMakeLists.txt`.

Existing plug-in names/IDs, settings and firmware folders, and internal
`gearmulator` build options are retained for compatibility. They are not claims
that this is an official upstream release.

Thanks to the upstream Gearmulator contributors whose work makes this fork
possible. See [the upstream README](README.upstream.md) for the original project
overview. The existing [GPLv3 licence](LICENSE.md), copyright notices and
third-party licences are retained.
