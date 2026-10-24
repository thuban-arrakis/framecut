#!/bin/sh
set -eu

if [ "$(uname -s)" != Linux ] || ! command -v dpkg-shlibdeps >/dev/null 2>&1; then
  echo 'Build on Debian with dpkg-dev installed, matching the target release and architecture.' >&2
  exit 1
fi
framecut_project_root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
framecut_build_dir="${1:-$framecut_project_root/build-debian}"
framecut_release_dir="${2:-$framecut_project_root/release}"
mkdir -p "$framecut_release_dir"
cmake -S "$framecut_project_root" -B "$framecut_build_dir" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DBUILD_TESTING=ON
cmake --build "$framecut_build_dir" --parallel
QT_QPA_PLATFORM=offscreen ctest --test-dir "$framecut_build_dir" --output-on-failure
cpack --config "$framecut_build_dir/CPackConfig.cmake" -G DEB -B "$framecut_release_dir"
echo "Debian package written to: $framecut_release_dir"
echo 'Validate in the target desktop environment before fleet rollout.'
