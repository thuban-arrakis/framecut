# Framecut

Framecut is a native C++ desktop video editor built with Qt 6 Widgets and FFmpeg. Its CapCut-inspired workspace supports trimming the beginning and end of a video and removing sound. Version 0.3 adds draggable timeline edges, exact trim times, and trim undo/redo; light/dark themes and English/Russian interfaces remain available.

The interface and video preview use CPU rendering. Preview copies are limited to 480p to reduce CPU and memory use. Import may need to prepare a full local preview first, which can take time for long, high-resolution, or unusual videos. Export reads the untouched original file, so the smaller preview does not limit the exported resolution. Audio-only removal copies the original streams; trimming encodes a new high-quality MP4 on the CPU.

## Use

1. Import or drop a video, or load the included sample.
2. To trim, drag the left or right edge of the timeline clip inward. The clip box and audio strip shrink together. Drag outward to recover footage, or enter exact **Start** and **End** source times in the **Trim** tab.
3. Play and scrub the selected range. Use **Reset trim** to restore the full clip, or **Edit → Undo trim / Redo trim** to revisit edits. Press **Escape** during a handle drag to cancel that drag.
4. Choose whether to **Remove audio**. With it switched off, a trimmed export retains the first source audio track.
5. Choose **Export** and select a new filename. A trimmed video is saved as MP4.

Trim times and undo history are remembered separately for each imported clip while the app stays open. The source ruler stays fixed as the selected box shrinks; the shaded outside area represents footage excluded from the export. Playback and export use the selected range, without adding a blank lead-in. See [trimming behavior and implementation](docs/trimming.md).

To remove imported media, select it and choose **Remove from project** below the import button, use its right-click menu, or press **Delete** / **Backspace** while the media list has focus. Removal clears the item from the current workspace and leaves the original file on disk. Removing the selected video opens a neighboring item, or returns to the empty workspace if it was the last one. Removal is unavailable during import or export.

Open **Settings** in the top bar to choose **Dark** or **Light** and **English** or **Русский**. Changes apply immediately, preserve the loaded media and playback position, and are saved for the next launch. The initial defaults are dark and English. These preferences are per user; they do not save the editing session.

When the full clip is selected, removing audio preserves the original container, all non-audio streams, and chapters without re-encoding. When a shorter range is selected, export creates H.264 MP4 at the source's displayed dimensions, with AAC audio if enabled; extra video/audio tracks, subtitles, attachments, and chapters are excluded from this trimmed output. Precise trims require re-encoding and are not lossless. Existing files and the original video are protected; choose an unused export filename. The application processes media locally and needs no account or internet connection.

This version handles one selected video at a time. It does not yet split a clip into several pieces, remove an interior section, combine clips, or save editing projects. Closing the app discards the imported library and trim edits; exported files and appearance/language preferences remain.

## Build on macOS

Install a C++17 compiler, CMake 3.22+, Ninja, Qt with Widgets/Multimedia/Concurrent/Test, FFmpeg, and the libswscale/libavutil development files. The current Mac toolchain uses Qt 6.11.3; choose a Qt build compatible with the installed macOS SDK. Debian source compatibility starts at Qt 6.4. A Homebrew FFmpeg installation supplies both the tools and these libraries on macOS.

```sh
cmake -S . -B out/macos-current -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/path/to/Qt/6.11.3/macos
cmake --build out/macos-current --parallel
QT_QPA_PLATFORM=offscreen ctest --test-dir out/macos-current --output-on-failure
open out/macos-current/Framecut.app
```

Make a self-contained local app and zip:

```sh
scripts/package-macos.sh out/macos-current/Framecut.app /path/to/Qt/6.11.3/macos
```

The script stages a fresh copy in `release/`, bundles Qt and the FFmpeg tools, rewrites and checks non-system library paths, then applies an ad-hoc signature. This local package is not Developer ID signed or notarized. See [macOS packaging](docs/macos-packaging.md).

## Build on Debian

Use the distribution and architecture that will run the application. Debian 12 and Debian 13 are source targets; see the [validation record](docs/validation.md) for the releases and architectures actually tested. Test the actual fleet image and desktop environment before rollout.

```sh
sudo apt update
sudo apt install build-essential cmake ninja-build pkg-config dpkg-dev \
  qt6-base-dev qt6-multimedia-dev libavutil-dev libswscale-dev ffmpeg
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
./build/framecut
```

Build a `.deb` using the same source checkout:

```sh
scripts/package-debian.sh
```

Runtime library dependencies are calculated by `dpkg-shlibdeps`; FFmpeg is an explicit package dependency. No desktop fleet is changed by the build scripts. See [Debian deployment and desktop validation](docs/debian-deployment.md).

## Source layout

- `src/`: Qt interface, software preview, and media pipeline.
- `tests/`: media integration and UI checks.
- `assets/`: original app icon and an eight-second sample with audio.
- `translations/`: editable Russian `.ts` catalog and embedded application/Qt `.qm` catalogs; see [translation maintenance](translations/README.md).
- `packaging/`: desktop entry and macOS dependency bundling helper.
- `scripts/`: native packaging commands.

The sample and icon were created for this project. The workspace takes inspiration from [CapCut's desktop layout](https://www.capcut.com/resource/capcut-for-laptop); Framecut is independently named and is not affiliated with CapCut. Dependency redistribution information is in [third-party notices](docs/third-party-notices.md).

See [validation results](docs/validation.md) for the tested toolchains and the remaining desktop pilot check.
