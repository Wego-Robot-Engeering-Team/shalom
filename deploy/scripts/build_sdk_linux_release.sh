#!/usr/bin/env bash
set -euo pipefail

usage() {
  echo "Usage: deploy/scripts/build_sdk_linux_release.sh --version <version> [--allow-dirty]"
}

version=""
allow_dirty=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --version) version="${2:-}"; shift 2 ;;
    --allow-dirty) allow_dirty=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "알 수 없는 인자: $1" >&2; usage >&2; exit 2 ;;
  esac
done
if [[ -z "$version" || ! "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+([.-][A-Za-z0-9]+)*$ ]]; then
  echo "--version <major.minor.patch>가 필요합니다." >&2
  exit 2
fi
if [[ "$(uname -s)" != "Linux" || "$(uname -m)" != "x86_64" ]]; then
  echo "이 스크립트는 Linux x86_64 release runner에서만 실행합니다." >&2
  exit 1
fi

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
sdk_root="$repo_root/hmi/sdk"
sdk_version="$(tr -d '[:space:]' < "$sdk_root/VERSION")"
if [[ "$sdk_version" != "$version" ]]; then
  echo "SDK VERSION($sdk_version)과 요청 버전($version)이 다릅니다." >&2
  exit 1
fi
if [[ $allow_dirty -eq 0 ]] && [[ -n "$(git -C "$repo_root" status --porcelain --untracked-files=all)" ]]; then
  echo "Git 작업 트리가 깨끗하지 않습니다. 릴리스에는 --allow-dirty를 사용하지 마십시오." >&2
  exit 1
fi

dist_root="$repo_root/dist"
release_name="shalom-sdk-$version-linux-x86_64"
stage_root="$dist_root/$release_name"
archive="$dist_root/$release_name.tar.zst"
build_root="$(mktemp -d "${TMPDIR:-/tmp}/shalom-sdk-build.XXXXXX")"
trap 'rm -rf "$build_root"' EXIT

rm -rf "$stage_root" "$archive"
mkdir -p "$stage_root/shalom-sdk/samples/cpp" "$stage_root/shalom-sdk/samples/python"

cmake -S "$sdk_root/Linux/cpp" -B "$build_root" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$stage_root/shalom-sdk"
cmake --build "$build_root" --parallel
cmake --install "$build_root"

install -m 0644 "$sdk_root/Linux/cpp/examples/monitor.cpp" \
  "$sdk_root/Linux/cpp/examples/api_example.cpp" "$stage_root/shalom-sdk/samples/cpp/"
install -m 0644 "$repo_root/deploy/packaging/sdk/CMakeLists.txt.in" \
  "$stage_root/shalom-sdk/samples/cpp/CMakeLists.txt"
cp -a "$sdk_root/Linux/python/." "$stage_root/shalom-sdk/python/"
find "$stage_root/shalom-sdk/python" -type f -path '*/__pycache__/*' -delete
find "$stage_root/shalom-sdk/python" -type d -name __pycache__ -empty -delete
install -m 0644 "$sdk_root/Linux/python/examples/monitor.py" \
  "$stage_root/shalom-sdk/samples/python/monitor.py"
cp -a "$sdk_root/docs" "$stage_root/shalom-sdk/docs"
install -m 0644 "$sdk_root/LICENSE" "$sdk_root/NOTICE" "$sdk_root/VERSION" "$stage_root/shalom-sdk/"
sed "s/@VERSION@/$version/g" "$repo_root/deploy/packaging/sdk/README.md.in" \
  > "$stage_root/shalom-sdk/README.md"

sample_build="$build_root/sample-check"
cmake -S "$stage_root/shalom-sdk/samples/cpp" -B "$sample_build" \
  -DCMAKE_PREFIX_PATH="$stage_root/shalom-sdk"
cmake --build "$sample_build" --parallel

(cd "$stage_root" && find . -type f ! -name checksums.txt -print0 | sort -z | xargs -0 sha256sum > checksums.txt)
tar --zstd -cf "$archive" -C "$dist_root" "$release_name"
printf '%s\n' "Built: $archive"
