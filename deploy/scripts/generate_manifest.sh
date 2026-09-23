#!/usr/bin/env bash
set -euo pipefail

usage() {
  echo "Usage: deploy/scripts/generate_manifest.sh --version <version> --release-root <directory>"
}

version=""
release_root=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --version) version="${2:-}"; shift 2 ;;
    --release-root) release_root="${2:-}"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "알 수 없는 인자: $1" >&2; usage >&2; exit 2 ;;
  esac
done
if [[ -z "$version" || -z "$release_root" || ! -d "$release_root" ]]; then
  usage >&2
  exit 2
fi
command -v jq >/dev/null || { echo "jq가 필요합니다." >&2; exit 1; }

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
submodules="$(git -C "$repo_root" submodule status --recursive | awk '{gsub(/^[+-]/, "", $1); printf "%s\\t%s\\n", $2, $1}')"

files_json="$({
  cd "$release_root"
  find . -type f ! -name manifest.json ! -name checksums.txt -print0 | sort -z |
    xargs -0 sha256sum |
    jq -R 'split("  ") | {path: .[1], sha256: .[0]}'
} | jq -s '.')"

submodules_json="$(printf '%s\n' "$submodules" | jq -Rn '[inputs | select(length > 0) | split("\t") | {path: .[0], commit: .[1]}]')"

jq -n \
  --arg version "$version" \
  --arg source_commit "$(git -C "$repo_root" rev-parse HEAD)" \
  --arg build_type "Release" \
  --arg architecture "arm64" \
  --arg ros_distro "Jazzy" \
  --arg generated_at "$(date -u +%Y-%m-%dT%H:%M:%SZ)" \
  --argjson submodules "$submodules_json" \
  --argjson files "$files_json" \
  '{release_version: $version, target: "Jetson Orin AGX / Ubuntu 24.04 / arm64", architecture: $architecture, ros_distro: $ros_distro, source_commit: $source_commit, build: {type: $build_type, generated_at: $generated_at}, submodules: $submodules, files: $files}' \
  > "$release_root/manifest.json"
