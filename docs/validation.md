# Validation record

Validated on 24 October 2026 for Framecut 0.3.0.

## macOS

| Component | Tested version |
| --- | --- |
| Operating system | macOS 27.0.1, build 26A434 |
| CPU architecture | arm64 |
| Compiler | AppleClang 21.0.0.21000334 |
| Qt | 6.11.3 |
| Bundled FFmpeg / ffprobe tools | 7.1.1 |

Both CTest suites passed: media engine (QtTest total 17 passed) and UI smoke (QtTest total 11 passed), with zero failures or skips in the final test log. UI checks use Qt's offscreen platform and software media settings. Logs and thirteen screenshots are recorded under `qa/macos/`.

The UI suite verifies playback, seeking, audio removal, and a silent export inspected with ffprobe. The 0.2 checks also verify live light/dark and English/Russian changes without losing the loaded media or playback position, preference persistence in a fresh process, translated errors, and the minimum 1100×720 layout. Removal checks cover current and non-current clips, busy-state protection, the visible button, Delete while the library has focus, the empty library, unchanged original-file hashes, and keeping another imported clip's source when it refers to a generated preview.

The 0.3 UI checks drag both trim handles inward and outward, cancel a drag with Escape, undo/redo a whole gesture, enter exact times, switch between clips with independent ranges, stop playback at the selected end, and restart from the selected start after previewing the final frame. A trimmed export through the save dialog is checked for selected duration, full source dimensions, and retained audio. Russian/light and English/dark trim controls were visually reviewed at normal and minimum window sizes.

The media suite uses a long-GOP source with a distinct shade in every frame to check every decoded frame after a cut between keyframes. It checks trim duration, retained/removed audio, rotation, invalid ranges, cancellation, source-file protection, and a selected segment after a shorter audio track has ended. Existing full-length stream, chapter, attachment, and secondary-video preservation tests remain in place.

The packaged app passed a dependency audit covering 120 Mach-O files, with no unresolved non-system dependencies, duplicate library identities, or missing arm64 code. Its ad-hoc signature passed `codesign --verify --deep --strict`. Both bundled media executables ran successfully, and the packaged application passed a native Cocoa startup check using `--version`.

Package artifacts:

- `Framecut.app`: approximately 168 MiB.
- `Framecut-0.3.0-macos-arm64.zip`: approximately 71 MiB, with an adjacent SHA-256 checksum record.

## Debian

| Component | Tested version/configuration |
| --- | --- |
| Operating system | Debian 12 container |
| CPU architecture | arm64 |
| Qt | 6.4.2 |
| FFmpeg / ffprobe | 5.1.9-0+deb12u1 |
| Graphics and audio | No `/dev/dri` GPU device and no audio device |

Both CTest suites passed after a clean rebuild: media engine (QtTest total 17 passed) and UI smoke (QtTest total 11 passed). A separate X11/Xvfb run passed all 11 UI entries, including software preview frames, playback, seeking, audio removal, trim handles and restoration, per-clip trim ranges, bounded preview playback, a trimmed export retaining audio, removal, theme/language changes, and preference persistence. Both runs had zero failures or skips.

CPack generated `framecut_0.3.0_arm64.deb` (616,910 bytes). Test logs are recorded under `qa/debian/`, with thirteen screenshots per platform in its `offscreen/` and `xcb/` subdirectories. The final X11 Russian/light and English/dark trim screenshots were visually reviewed, including the minimum window size. Build commands and the desktop acceptance procedure are in [Debian deployment](debian-deployment.md).

The package successfully upgraded 0.2.0 in the isolated Debian container. The installed executable reported `Framecut 0.3.0` through X11/Xvfb and its SHA-256 matched the tested build. The release directory includes a SHA-256 checksum record for the package.

## Scope of validation

Offscreen and Xvfb tests verify application behavior without a GPU-backed window; they do not establish performance or audio output in the fleet's desktop environment. The fleet's release remains unknown. The final pilot must use the actual Debian release, architecture, desktop environment, system configuration, and representative videos. Debian 13 and amd64 were not tested in this run.

The local macOS package is ad-hoc signed, not Developer ID signed or notarized. A clean second-Mac deployment and physical audio-device reconnection are not covered by the automated tests.
