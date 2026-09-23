# Maschine MD-MM GitHub release checklist

The fork has its own version in the root `CMakeLists.txt` (initially 0.1.0).
Release titles and deliverables use Maschine MD-MM, not upstream Gearmulator
release numbers. Retain upstream attribution, copyright and licence files.
Legacy plug-in IDs, installer receipt IDs and user-data paths remain unchanged
for compatibility. The combined app also retains its original bundle ID and
standalone settings filename so the rename preserves permissions and settings.

The combined macOS app runs both Elektron emulations and supports the Maschine
MK3 displays, controls and lights. The separate MD and MM products remain in
the installer. No firmware or user data belongs in a GitHub release.

1. Confirm the working tree and the JUCE and DSP submodules are clean and
   their commits are available from the URLs in `.gitmodules`. The verified
   build script refuses a dirty source tree.
2. Run `scripts/macos/build_mdmm.sh` with
   `GEARMULATOR_MD_FIRMWARE_BIN` and `GEARMULATOR_MM_FIRMWARE_BIN` pointing to
   personally obtained 8 MiB images. This runs the release test suite and
   produces the signed universal app and plug-in candidates.
3. Check MD and MM playback, recording, pad lights, strip lights, both LCDs,
   and the 1/5 focus buttons on a connected Maschine MK3. Verify the pad
   response during MM playback while record is enabled.
4. Run `scripts/macos/build_installer.sh` with the AAX SDK, PACE tools,
   signing certificates and notary credentials configured. The DMG appears
   in the project root; the signed package appears under
   `artifacts/macos-installer/`. Do not add either binary to Git.
5. Verify the final DMG mounts and contains the notarized package. Install
   into a clean macOS account, then check standalone, AU, VST3 and AAX where
   available. Confirm no firmware or private project state was packaged.
6. Upload the verified DMG as a GitHub release asset only after the final
   hardware check. Record the release commit, version and SHA-256 checksum.

The build and installer scripts do not upload anything to GitHub.

## Signing credentials

Keep private signing keys in macOS Keychain. The installer accepts the Apple
team, signing identities, PACE account and wrap GUIDs through environment
variables; no personal values are supplied by the public script. Use a
Keychain profile for notarization (`NOTARIZE_PROFILE`). Never commit exported
keys, certificate bundles, passwords or a local `.env` file. Signed apps
contain the public signing certificate, not the private key.
