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

## macOS build and release

The combined standalone app is **Maschine MD-MM.app**, built by the
`mdmmJucePlugin_Standalone` CMake target.
Use `scripts/macos/build_mdmm.sh` for the verified universal build; it expects
your own MD and MM firmware images through `GEARMULATOR_MD_FIRMWARE_BIN` and
`GEARMULATOR_MM_FIRMWARE_BIN`. Firmware is not included in this repository or
in the installer.

`scripts/macos/build_installer.sh` makes the signed installer and puts the
DMG in the project root. It additionally requires the AAX SDK, PACE tools,
Developer ID certificates and Apple notarization credentials for a full
release. See [the release checklist](doc/mdmm_release.md) before uploading
assets to GitHub.

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
