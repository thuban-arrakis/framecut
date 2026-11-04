# Validation record

Historical 0.3.0 checks below were run on 24 October 2026. The 0.3.1 Debian validation is recorded separately.

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

## Post-0.3.0 robustness review

On 24 October 2026, the local macOS review build passed three CTest suites: media engine (20 QtTest entries), controller lifecycle (5), and UI smoke (17), with no failures or skips. The same suites passed a separate Debug build with AddressSanitizer and UndefinedBehaviorSanitizer; leak detection was disabled. These counts include QtTest setup and cleanup entries. That review did not include a Debian rebuild; the later 0.3.1 entry below records the Debian run.

New regression checks cover single-frame imports, previews whose source audio outlasts the video, same-size source rewrites with restored modification times, controller destruction during active inspection, progress delivery on the GUI thread, and destroyed dialog parents. UI checks cover retaining library items when switching clips, limited playhead repaint regions, interrupted trim drags, and complete pixel conversion at odd widths. The odd-width planar YUV check exposed an uninitialized final column, which is now corrected without changing the conversion path for even-width previews.

Timeline repaint area is below one tenth of the whole widget in the 900×180 regression fixture. Unchanged library thumbnails are reused across selection changes. These checks verify reduced work; they are not an overall application CPU benchmark.

Builds, test homes, caches, and the interactive review app were kept under `/tmp`. Startup with an unwritable cache directory exited cleanly with a diagnostic. The temporary launcher passed a version smoke check. Packaging shell syntax was checked, and package rules now include the Qt translation license beside the dependency notices; fresh distribution packages were not produced during this review.

Local test logs are saved under `qa/macos-review/`, outside the tracked source files.

## 0.3.1 VFR trim/export fix — 4 November 2026

| Component | Tested version/configuration |
| --- | --- |
| Operating system | Debian 13 (trixie) |
| CPU architecture | amd64 |
| Compiler | GCC 14.2.0 |
| Qt | 6.8.2 |
| FFmpeg / ffprobe | 7.1.5-0+deb13u1 |
| Build isolation | Existing distribution-matched container, no network, two CPUs, 4 GiB memory limit |

All three CTest suites passed: media engine (31 QtTest entries), controller
lifecycle (5), and UI smoke (26), with zero failures or skips. Counts include
setup and cleanup. The serial suite run took 33.53 seconds; Qt used its offscreen
platform and software media settings.

Generated VFR fixtures cover fractional starts and ends, an end inside the final
frame hold, a 43 ms final hold, retained and removed audio, nonzero container
origins, video delayed relative to audio, missing stream timing, and Matroska's
millisecond clock. Fault injection confirms that otherwise decodable outputs
with a missing interior frame or a shortened final hold are rejected without
publishing an output. Existing CFR, rotation, cancellation, overwrite, original
file integrity, auxiliary-stream and UI export checks remain passing.

The reported recording was verified on its original host using a small harness
linked to the same built media library. Its selected range exported all 361
frames with identical relative timestamps, a 6.074933-second presentation end,
and the original 43 ms final hold. A cut inside that hold ended at 6.048 seconds
with a 16.066667 ms hold. Both outputs decoded fully; the original hash remained
unchanged. Private media stayed on the host and temporary exports were removed;
only synthetic fixtures are included in this repository.

The build also includes the already deployed preview-buffer padding and Linux
icon-installation fixes. The resulting package is `framecut_0.3.1_amd64.deb`
(3,663,856 bytes), SHA-256
`f0b8a960d4a84419fa1b0c930d1ef893f1b6548e55d25e569867d630a0c4cf0c`.
The tested executable SHA-256 is
`f70862b284de586253139751ae332987a757ab1c598e3478cb3c42bfeb819002`.

## Scope of validation

Offscreen and Xvfb tests verify application behavior without a GPU-backed window; they do not establish interaction performance or audio output in every desktop session. The 0.3.1 Debian 13 amd64 checks below establish compatibility with that host's runtime. Manual display, input-latency, scaling and audio acceptance remains specific to the desktop environment.

The local macOS package is ad-hoc signed, not Developer ID signed or notarized. A clean second-Mac deployment and physical audio-device reconnection are not covered by the automated tests.
