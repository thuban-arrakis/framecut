#!/bin/sh
set -eu

if [ "$(uname -s)" != Darwin ] || [ "$#" -lt 1 ]; then
  echo 'Usage on macOS: scripts/run-temporary-macos.sh /path/to/Framecut.app [app arguments]' >&2
  exit 1
fi
framecut_app="$1"
shift
framecut_executable="$framecut_app/Contents/MacOS/Framecut"
if [ ! -x "$framecut_executable" ]; then
  echo 'Built Framecut.app not found.' >&2
  exit 1
fi

framecut_runtime="$(mktemp -d /tmp/framecut-run.XXXXXX)"
mkdir -p "$framecut_runtime/home" "$framecut_runtime/cache" "$framecut_runtime/exports" "$framecut_runtime/tmp"
echo "Temporary app data: $framecut_runtime"
exec env \
  CFFIXED_USER_HOME="$framecut_runtime/home" \
  TMPDIR="$framecut_runtime/tmp" \
  XDG_CACHE_HOME="$framecut_runtime/cache" \
  FRAMECUT_SETTINGS_FILE="$framecut_runtime/settings.ini" \
  FRAMECUT_CACHE_DIR="$framecut_runtime/cache" \
  FRAMECUT_EXPORT_DIR="$framecut_runtime/exports" \
  "$framecut_executable" "$@"
