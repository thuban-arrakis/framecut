# macOS packaging

Build `Framecut.app` first with CMake. Then pass the built app and the matching Qt installation prefix:

```sh
scripts/package-macos.sh out/macos-current/Framecut.app /path/to/Qt/6.11.3/macos
```

Use the same CPU architecture for the application and the FFmpeg executables. The script finds `ffmpeg` and `ffprobe` on `PATH`; `FRAMECUT_FFMPEG_BIN` and `FRAMECUT_FFPROBE_BIN` can point to specific local binaries. No download or system installation is performed by the packaging script.

Each invocation creates a fresh directory under `release/`. It retains the staged `.app`, a zip archive, a SHA-256 record, and the tools' version/build information. Existing staged apps are not overwritten.

Russian application and standard Qt dialog translations are compiled into the app's resources; packaging does not require a separate translation download. Native macOS file dialogs follow the operating system's language. The app saves theme and interface language as per-user preferences outside the bundle, so replacing the app does not reset those choices.

The packaging steps are:

1. Copy the built app and place FFmpeg and ffprobe in `Contents/MacOS`.
2. Move the app and media tools' non-Qt library dependencies into private bundle directories so different FFmpeg builds cannot collide.
3. Run the matching Qt installation's `macdeployqt` to deploy Qt frameworks and plugins. Recursively copy any remaining non-system Mach-O dependencies, rewrite load paths relative to the bundled files, and remove absolute development rpaths.
4. Audit all bundled Mach-O references, unique library identities, and the target CPU architecture. Unresolved or external non-system dependencies stop packaging.
5. Apply and verify an ad-hoc signature, run the bundled media tools, and create the zip and checksum.

Qt's deployment documentation describes `macdeployqt` and its [framework/plugin behavior](https://doc.qt.io/qt-6.11/macos-deployment.html). Additional non-Qt libraries require their own deployment handling, which is why the project includes `packaging/bundle_macos.py`.

The resulting local build is ad-hoc signed, not Developer ID signed or notarized. Production distribution to other Macs requires the organization's normal signing, notarization, and dependency-license process. The packaging script does not inspect or use signing credentials.

Before distributing, test the packaged app on a compatible clean Mac without the development Qt or Homebrew installation. Check import, playback, cancellation, and export using the bundled media tools. Drag both timeline edges, enter exact times in **Trim**, restore footage by dragging outward, and test undo/redo/reset. Check selected-range playback and MP4 export with and without sound. Confirm the bundled FFmpeg has `libx264` and AAC encoders; trimmed export uses these CPU encoders. Remove imported clips and confirm the originals remain unchanged. Switch dark/light themes and English/Russian in **Settings**, then restart the packaged app to check preference persistence. Trim edits are session-only. The dependency audit checks load paths; this clean-machine test verifies the complete runtime behavior.

## Local installation and upgrades

`scripts/install-local-macos.py` installs a packaged app together with the source, translations, release archives, and QA records. Pass `--app`, `--source`, `--destination-app`, and `--destination-source` as explicit paths. An existing destination is protected unless `--upgrade` is supplied.

An upgrade stages and verifies both copies before replacement. The previous app and project are retained in timestamped hidden backup directories beside their original locations, and their exact paths are printed. If final installation verification fails, the helper restores the previous destinations. It does not stop a running editor; quit and reopen Framecut to load an update. `--launch` optionally opens the installed app after verification.

The installer refuses a source destination containing Git metadata, including linked worktrees. Update an existing checkout through Git, or use a separate destination for an exported source snapshot. This keeps repository history and local changes in place.
