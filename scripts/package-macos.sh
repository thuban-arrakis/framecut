#!/bin/bash
set -euo pipefail

if [[ "$(uname -s)" != Darwin ]]; then
  echo 'This packaging script requires macOS.' >&2
  exit 1
fi
if [[ $# -lt 2 || $# -gt 3 ]]; then
  echo 'Usage: scripts/package-macos.sh /path/to/Framecut.app /path/to/Qt/6.x/macos [release-directory]' >&2
  exit 1
fi

framecut_project_root="$(cd "$(dirname "$0")/.." && pwd)"
framecut_source_app="$1"
framecut_qt_prefix="$2"
framecut_release_root="${3:-$framecut_project_root/release}"
framecut_ffmpeg="${FRAMECUT_FFMPEG_BIN:-$(command -v ffmpeg || true)}"
framecut_ffprobe="${FRAMECUT_FFPROBE_BIN:-$(command -v ffprobe || true)}"
framecut_arch="$(uname -m)"

[[ -d "$framecut_source_app/Contents/MacOS" ]] || { echo 'Built .app not found.' >&2; exit 1; }
[[ -x "$framecut_qt_prefix/bin/macdeployqt" ]] || { echo 'macdeployqt not found at Qt prefix.' >&2; exit 1; }
[[ -x "$framecut_ffmpeg" && -x "$framecut_ffprobe" ]] || { echo 'ffmpeg and ffprobe must be installed.' >&2; exit 1; }
mkdir -p "$framecut_release_root"
framecut_release_root="$(cd "$framecut_release_root" && pwd)"
framecut_stage="$(mktemp -d "$framecut_release_root/Framecut-macos-$framecut_arch.XXXXXX")"
framecut_app="$framecut_stage/Framecut.app"
/usr/bin/ditto "$framecut_source_app" "$framecut_app"
/bin/cp -L "$framecut_ffmpeg" "$framecut_app/Contents/MacOS/ffmpeg"
/bin/cp -L "$framecut_ffprobe" "$framecut_app/Contents/MacOS/ffprobe"
/bin/chmod u+x "$framecut_app/Contents/MacOS/ffmpeg" "$framecut_app/Contents/MacOS/ffprobe"

# Namespace the Homebrew media libraries before Qt's deployment tool can flatten
# same-named FFmpeg libraries from different builds into one Frameworks directory.
python3 "$framecut_project_root/packaging/bundle_macos.py" "$framecut_app" --qt-prefix "$framecut_qt_prefix" --prepare
"$framecut_qt_prefix/bin/macdeployqt" "$framecut_app" \
  "-executable=$framecut_app/Contents/MacOS/ffmpeg" \
  "-executable=$framecut_app/Contents/MacOS/ffprobe" -verbose=1
python3 "$framecut_project_root/packaging/bundle_macos.py" "$framecut_app" --qt-prefix "$framecut_qt_prefix"
python3 "$framecut_project_root/packaging/bundle_macos.py" "$framecut_app" --audit-only --arch "$framecut_arch"
/bin/cp "$framecut_project_root/docs/third-party-notices.md" "$framecut_app/Contents/Resources/ThirdPartyNotices.md"

# Ad-hoc signatures use no identity/keychain credentials. They are for local builds.
/usr/bin/codesign --force --deep --sign - "$framecut_app"
/usr/bin/codesign --verify --deep --strict "$framecut_app"
"$framecut_app/Contents/MacOS/ffmpeg" -version > "$framecut_stage/ffmpeg-version.txt"
"$framecut_app/Contents/MacOS/ffprobe" -version > "$framecut_stage/ffprobe-version.txt"
framecut_version="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$framecut_app/Contents/Info.plist")"
framecut_archive="$framecut_stage/Framecut-$framecut_version-macos-$framecut_arch.zip"
/usr/bin/ditto -c -k --sequesterRsrc --keepParent "$framecut_app" "$framecut_archive"
(cd "$framecut_stage" && /usr/bin/shasum -a 256 "$(basename "$framecut_archive")") > "$framecut_archive.sha256"
echo "Packaged app: $framecut_app"
echo "Archive: $framecut_archive"
echo 'This local build is ad-hoc signed; it is not Developer ID signed or notarized.'
