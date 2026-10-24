# Debian deployment

Framecut targets CPU-only desktop operation: Qt Widgets and a software-painted preview, with FFmpeg processing video on the CPU. The preview uses a local copy no larger than 854×480. Full-length audio removal copies the original streams without re-encoding. Trimming uses CPU H.264 encoding from the original source and therefore takes longer than audio-only removal; it does not require a GPU.

Import can prepare the full preview before playback is available. Long or high-resolution videos can therefore take time to import; preview generation does not modify the source or limit export resolution. Trimmed exports are new high-quality MP4 files, with the first audio track encoded as AAC when sound is retained. See [trim/export behavior](trimming.md).

See the [validation record](validation.md) for the tested Debian builds, media/UI suites, and X11/Xvfb runs. The exact fleet distribution and desktop configuration still need to be confirmed; the actual fleet image and Debian 13 have not been validated.

## Build per distribution and architecture

Build on a clean Debian image matching the target release and CPU architecture. Avoid copying binaries built against newer Qt or FFmpeg libraries into an older distribution. Use the source commit, dependency package versions, and package checksum as the release record.

```sh
sudo apt update
sudo apt install build-essential cmake ninja-build pkg-config dpkg-dev \
  qt6-base-dev qt6-multimedia-dev libavutil-dev libswscale-dev ffmpeg
scripts/package-debian.sh
```

The script runs CMake, builds, executes the tests with Qt's offscreen platform, and invokes CPack. It writes the `.deb` under `release/`. The package uses system Qt and FFmpeg. CPack's `dpkg-shlibdeps` integration derives linked-library requirements from the actual build; the `ffmpeg` executable dependency is explicit. See the [CPack DEB documentation](https://cmake.org/cmake/help/latest/cpack_gen/deb.html#variable:CPACK_DEBIAN_PACKAGE_SHLIBDEPS).

Russian application and standard Qt dialog translations are embedded in the executable, so the runtime does not need a separate language-pack installation. Theme and language are stored in each desktop user's Qt settings. New users start with dark theme and English; changing either option in **Settings** applies immediately and survives an application restart.

Qt's FFmpeg media plugin is supplied by `libqt6multimedia6` in both [Debian 12](https://packages.debian.org/bookworm/amd64/libqt6multimedia6/filelist) and [Debian 13](https://packages.debian.org/trixie/amd64/libqt6multimedia6/filelist). Verify the package contents on the chosen architecture rather than adding an assumed `qt6-multimedia-plugins` dependency.

## Pilot installation

Inspect the generated control metadata and record its checksum:

```sh
dpkg-deb --info release/framecut_0.3.0_amd64.deb
sha256sum release/framecut_0.3.0_amd64.deb
sudo apt install ./release/framecut_0.3.0_amd64.deb
```

Use the actual filename for arm64 or a later version. Install with APT so runtime dependencies are resolved. Launch **Framecut** from the desktop menu or run `framecut` as the logged-in desktop user.

The application needs an interactive graphical session. Offscreen tests exercise code paths; verify display rendering, input latency, scaling, and audio output on the target desktop.

## Desktop acceptance test

Run the pilot in the desktop environment, screen size, and scaling used by the fleet, on a machine without GPU access.

1. Import the sample, then typical production videos including a larger file. Check that the preview appears, seeking works, and the UI stays responsive while creating preview copies.
2. Play with audio removal enabled. Disable it to compare with the original sound. If playback has no sound, check the system's audio output separately; silent export should still work.
3. With the full clip selected, export to a new file and compare video properties with the source. Confirm audio removal using:

   ```sh
   ffprobe -v error -select_streams a -show_entries stream=index -of csv=p=0 /path/to/export.mp4
   ffprobe -v error -select_streams v -show_entries stream=codec_name,width,height,r_frame_rate -of json /path/to/export.mp4
   ```

   The first command should succeed with no output. The second should report the source video's codec, dimensions, and frame rate.

4. Check cancellation, an existing export filename, a read-only destination, invalid media, and insufficient disk space. Verify that originals remain unchanged and partial exports do not appear as completed files.
5. Check native file dialogs, drag-and-drop, keyboard shortcuts, and the file-manager reveal action.
6. Import several clips. Remove selected media using **Remove from project**, its context menu, and **Delete** / **Backspace** with focus in the media list. Check selection after each removal and the empty workspace after the last removal. Confirm the original files remain on disk and unchanged.
7. Switch between dark/light themes and English/Russian while a video is loaded. Check the settings dialog, labels, menus, and inspector at the fleet's screen size; use the inspector's scrollbar at smaller heights. Confirm the media and playback position stay unchanged. Restart and verify the selected theme and language persist for that user.
8. Drag both clip edges inward. Check that the video/audio boxes shrink, the preview seeks to each cut, and playback stays within the selected range. Enter exact source times in **Trim**, then test **Undo trim**, **Redo trim**, **Reset trim**, and **Escape** during a drag. Switch clips and verify each clip's range survives for the current session.
9. Export a non-keyframe trim with sound removed, then another with sound retained. Inspect the output's first/last frames and duration. Expect H.264 MP4 and either no audio or the first source audio track as AAC. Confirm extra tracks and chapters are absent from trimmed outputs and the source is unchanged. Include portrait/rotated, variable-frame-rate, high-frame-rate, and any HDR sources used by the fleet.
10. Record preview frame rate, CPU use, memory use, trim-export time, and interaction latency while working with the largest representative videos. CPU rendering avoids a GPU requirement but does not guarantee a particular playback rate on every machine.

Keep a pilot package for every supported release/architecture combination. A successful local macOS test or offscreen Linux test does not replace this target-desktop check.

## Fleet distribution

After the pilot passes, publish the immutable package using the organization's existing signed APT repository or software-distribution mechanism. Retain the previous package and release record for rollback. Advance through a small initial group before the whole fleet.

Framecut includes no updater, remote administration endpoint, or cloud media service. Use the package manager to install, upgrade, and remove it. Build and packaging scripts do not connect to fleet hosts.
