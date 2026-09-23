#!/usr/bin/env bash
set -euo pipefail

usage() { echo "Usage: deploy/scripts/build_hmi_macos_release.sh --version <version> [--allow-dirty]"; }
version=""
allow_dirty=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --version) version="${2:-}"; shift 2 ;;
    --allow-dirty) allow_dirty=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "알 수 없는 인자: $1" >&2; exit 2 ;;
  esac
done
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+([.-][A-Za-z0-9]+)*$ ]] || { usage >&2; exit 2; }
[[ "$(uname -s)" == Darwin ]] || { echo "이 스크립트는 macOS runner 전용입니다." >&2; exit 1; }

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
hmi_root="$repo_root/hmi"
hmi_version="$(sed -n 's/^project(inspection_hmi VERSION \([^ ]*\).*/\1/p' "$hmi_root/CMakeLists.txt")"
[[ "$version" == "$hmi_version" ]] || { echo "HMI project version($hmi_version)과 요청 버전($version)이 다릅니다." >&2; exit 1; }
if [[ $allow_dirty -eq 0 ]] && [[ -n "$(git -C "$repo_root" status --porcelain --untracked-files=all)" ]]; then
  echo "Git 작업 트리가 깨끗하지 않습니다." >&2; exit 1
fi
command -v macdeployqt >/dev/null || { echo "macdeployqt가 필요합니다." >&2; exit 1; }

dist_root="$repo_root/dist"
release_name="inspection-hmi-$version-macos-$(uname -m)"
stage_root="$dist_root/$release_name"
app_root="$stage_root/inspection-hmi"
archive="$dist_root/$release_name.tar.zst"
build_root="$(mktemp -d "${TMPDIR:-/tmp}/shalom-hmi-build.XXXXXX")"
trap 'rm -rf "$build_root"' EXIT

rm -rf "$stage_root" "$archive"
cmake -S "$hmi_root" -B "$build_root" -G Ninja -DCMAKE_BUILD_TYPE=Release -DHMI_BUILD_TESTS=OFF
cmake --build "$build_root" --parallel
cmake --install "$build_root" --prefix "$app_root"
find "$app_root/include" -depth -delete 2>/dev/null || true
macdeployqt "$app_root/inspection_hmi.app" -always-overwrite -no-translations
printf '%s\n' 'HMI 실행: inspection_hmi.app' 'HMI 번들에는 고객 SDK를 포함하지 않습니다.' > "$app_root/README.txt"
(cd "$stage_root" && find . -type f ! -name checksums.txt -print0 | sort -z | xargs -0 shasum -a 256 > checksums.txt)
tar --zstd -cf "$archive" -C "$dist_root" "$release_name"
printf '%s\n' "Built: $archive"
