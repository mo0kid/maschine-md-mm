# Maschine MD-MM

Machinedrum and Monomachine emulation with Maschine MK3 integration. This is
an independent fork of
[joelanders/gearmulator-md-mm](https://github.com/joelanders/gearmulator-md-mm)
and The Usual Suspects' Gearmulator project.

[Support this project on Ko-fi](https://ko-fi.com/djw_audio).

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
- **MIDI keyboard focus:** click either instrument panel to focus it. When
  Monomachine is focused, notes on any MIDI input channel play its selected track,
  including when Maschine is disconnected. Held notes release on their original tracks.
- **Key chording / p-locks:** shift-click one or more buttons to hold them
  down until you release the shift key.
- **Secondary functions:** rather than shift-click Function and another button,
  you can just click the secondary function text label.
- **Encoder clicking:** Alt/Option-click a DATA ENTRY encoder to press it, or
  Alt/Option-drag to press and turn. With a trig held, pressing its parameter's
  encoder toggles that parameter lock. This applies to encoders A–H, not LEVEL
  or SOUND SELECTION.
- **SysEx load and save:** use the macOS **File** menu in the combined app to
  load files into either machine or save dumps sent by its firmware. The
  separate MD and MM apps and plug-ins retain their right-click send menu.
- **Panel look and feel:** adjust encoder-drag and mouse-wheel sensitivity in settings.
  An experimental crisp LCD/panel rendering option is also available.
- **Audio inputs and outputs:** route host audio to the machine's input effects or sampling
  functions. Additional output pairs are available in a multi-output VST3 host;
  the standalone apps use stereo output.

## Using a Maschine MK3

Connect the MK3 and open the **Maschine MD-MM** standalone app on macOS. The
left hardware screen shows Machinedrum (MD); the right shows Monomachine (MM).
Press display button **1** or **5** to choose which instrument the shared pads,
knobs and buttons control. Both instruments keep running when you change focus.
The display buttons above each screen also provide that instrument's page and
mode controls, regardless of focus.

The integration does not modify or flash the Maschine's firmware, so using the
app cannot brick the MK3 through a firmware update.

Start by choosing an instrument, selecting a track with **PAD MODE + pad**, and
using the pads as trigs/steps. Hold a group button **A–H** and press a pad to
choose a pattern in that bank. Turn the eight knobs below the screens to edit
DATA ENTRY parameters A–H. The MK3's **SHIFT** button acts as the instrument's
FUNCTION key for its own secondary operations.

The table lists the MK3 controls and combinations implemented by this app.
Unless a row says otherwise, they act on the focused instrument. Button numbers
refer to the eight buttons above the screens, counted from left to right.

| MK3 control or shortcut | Action in Maschine MD-MM |
| --- | --- |
| Display button 1 / 5 | Focus MD / MM. |
| Display button 2 / 6 | Advance the MD synthesis/effects/routing page / MM data page. |
| Display button 3 / 7 | Advance that instrument's `1:4`–`4:4` scale/page selector. |
| Display button 4 / 8 | Toggle MD Classic/Extended mode / MM trig-select mode. |
| Eight knobs below the screens | Turn DATA ENTRY encoders A–H. |
| NOTE REPEAT + touch a knob | Press that DATA ENTRY encoder while touched; release to let go. With a trig held, this toggles its parameter lock. |
| 5D encoder turn | Turn MD SOUND SELECTION (also used on its tempo screen) or MM LEVEL. |
| 5D encoder press / directional buttons | ENTER / navigate up, right, down and left. |
| Dedicated `<` / `>` buttons | Browse edit pages backward / forward. |
| PAD MODE + pad | Select MD track 1–16 or MM track 1–6. |
| MUTE + pad | Toggle mute for MD track 1–16 or MM track 1–6. |
| Pad 1–16 | Press the corresponding trig/step; when a pattern bank is held, select that pattern slot. |
| Group A–H + pad | Select a pattern in bank A–H. A–D and E–H also switch the instrument's bank group as needed. |
| SHIFT | Hold the instrument's FUNCTION key for its firmware shortcuts. |
| SHIFT + PLAY / STOP | Start / stop **both** instruments together. |
| PLAY + RECORD | Send the instrument's real-time recording gesture. |
| ERASE / DUPLICATE | Send FUNCTION + PLAY (clear) / FUNCTION + STOP (paste) to the focused instrument. |
| PLAY / RECORD / STOP | Use the focused instrument's transport controls. |
| MIXER + knob 1 / knob 8 | Adjust the selected track's LEVEL/DATA / the app's master output level for the focused instrument. |
| SAMPLING + turn 5D encoder | Cycle the MK3 lighting effects. |
| SHIFT + SAMPLING | Toggle decorative lighting on the MK3. |
| TEMPO | Open the instrument's tempo control. |
| STEP | Send SCALE on either instrument. |
| SCENE | Send SONG ENABLE on MM. |
| PATTERN or ARRANGER | Send PATTERN-SONG on MD. |
| PLUG-IN or CHANNEL | Send SYNTHESIS-EFFECTS-ROUTING on MD. |
| BROWSER | Send KIT on MD, or KIT/SONG SETUP on MM. |
| SELECT or PITCH / MOD | Send ENTER or YES / EXIT or NO. |

### Loading and saving SysEx dumps

In the combined macOS app, open the **File** menu in the system menu bar.
**File > Factory Reset** lets you restore either machine to its factory data after
confirmation. Save any patterns, kits, settings or sample data you want to keep first.
Choose **Load SysEx File to Machinedrum...** or **Load SysEx File to
Monomachine...** to select a `.syx` file. Follow the prompt to put the emulated
machine in its receive mode. The same menu offers resume and cancel actions
during a transfer.

To save, choose **Save Machinedrum SysEx Dump...** or **Save Monomachine SysEx
Dump...** and select a destination. On the emulated machine, use its SysEx SEND
screen to send the data you want to export. Then choose **Finish Saving SysEx
Dump** from the menu. The app validates the captured data before writing the
`.syx` file. Choose **Cancel Saving SysEx Dump** to discard a capture.

## Install on macOS

Download the DMG from
[Releases](https://github.com/mo0kid/maschine-md-mm/releases). Open it and run
the installer package inside. Select **Maschine MD-MM** for the combined app
with MK3 screen and control integration. The separate Gearmulator MD and MM
apps and plug-ins are optional; select any plug-in formats you use. Restart
your DAW or rescan plug-ins after installing them.

Firmware is not included. On first launch without existing firmware, the app
shows the MD and MM firmware folders. Copy firmware images that you are
entitled to use into those folders, then relaunch.

## Build a development ZIP

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
To use the ZIP, fully extract it and run `macsetup_Maschine-MD-MM.command` from
the extracted folder. Copy `Maschine MD-MM.app` to `/Applications` (or run it
from that folder). If you want the separate plug-ins, copy their `.vst3` bundles
to `~/Library/Audio/Plug-Ins/VST3` and their `.component` bundles to
`~/Library/Audio/Plug-Ins/Components`, then restart your DAW or rescan plug-ins.
The setup command prepares these bundles; it does not install them. Because
this ZIP is signed locally and is not notarized, macOS may ask you to confirm
opening its apps or setup command. Supply your own firmware on first launch.
This build is useful for development, but it has not passed the
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
a build receipt. A Windows release package for the combined app is not yet
provided.

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
