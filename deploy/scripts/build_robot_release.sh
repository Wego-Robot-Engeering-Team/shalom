#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: deploy/scripts/build_robot_release.sh --version <version> [--allow-dirty]

Builds shalom-runtime and shalom-site-config, downloads their arm64 apt runtime
dependencies, collects notices, generates manifest/checksums, and creates
dist/shalom-release-<version>-arm64.tar.zst. Run on the dedicated Jetson
Ubuntu 24.04 / ROS 2 Jazzy release runner.
EOF
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
if [[ "$(dpkg --print-architecture)" != "arm64" ]]; then
  echo "이 스크립트는 Jetson arm64 release runner에서만 실행합니다." >&2
  exit 1
fi

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
dist_root="$repo_root/dist"
robot_dist="$dist_root/robot"
release_name="shalom-release-$version"
release_root="$dist_root/$release_name"
archive="$dist_root/$release_name-arm64.tar.zst"
apt_cache="$(mktemp -d "${TMPDIR:-/tmp}/shalom-apt.$version.XXXXXX")"
trap 'rm -rf "$apt_cache"' EXIT

runtime_args=(--version "$version")
if [[ $allow_dirty -eq 1 ]]; then
  runtime_args+=(--allow-dirty)
fi
"$repo_root/deploy/scripts/build_robot_runtime.sh" "${runtime_args[@]}"
"$repo_root/deploy/scripts/build_site_config.sh" --version "$version"

runtime_deb="$robot_dist/shalom-runtime_${version}_arm64.deb"
site_config_deb="$robot_dist/shalom-site-config_${version}_all.deb"
for file in "$runtime_deb" "$site_config_deb"; do
  [[ -f "$file" ]] || { echo "패키지가 생성되지 않았습니다: $file" >&2; exit 1; }
done

mapfile -t apt_packages < <(awk 'NF && $1 !~ /^#/' "$repo_root/deploy/config/robot-runtime-apt-packages.txt")
if [[ ${#apt_packages[@]} -eq 0 ]]; then
  echo "apt runtime 입력 목록이 비어 있습니다." >&2
  exit 1
fi

mkdir -p "$apt_cache/partial"
sudo apt-get -y --download-only --reinstall \
  -o "Dir::Cache::archives=$apt_cache" \
  install "${apt_packages[@]}"
sudo chown -R "$(id -u):$(id -g)" "$apt_cache"

rm -rf "$release_root" "$archive"
mkdir -p "$release_root/robot/packages" "$release_root/robot/site-config"
install -m 0755 "$repo_root/deploy/packaging/release/install.sh.in" "$release_root/robot/install.sh"
sed -i "s/@VERSION@/$version/g" "$release_root/robot/install.sh"
install -m 0644 "$repo_root/deploy/packaging/release/WEGO-PROPRIETARY-NOTICE.txt" \
  "$release_root/WEGO-PROPRIETARY-NOTICE.txt"
sed "s/@VERSION@/$version/g" "$repo_root/deploy/packaging/release/RELEASE.md.in" \
  > "$release_root/RELEASE.md"

install -m 0644 "$runtime_deb" "$site_config_deb" "$release_root/robot/packages/"
shopt -s nullglob
apt_debs=("$apt_cache"/*.deb)
if [[ ${#apt_debs[@]} -eq 0 ]]; then
  echo "apt가 runtime .deb를 내려받지 못했습니다." >&2
  exit 1
fi
install -m 0644 "${apt_debs[@]}" "$release_root/robot/packages/"
install -m 0644 "$repo_root/deploy/packaging/site-config/etc/shalom/robot.env" \
  "$release_root/robot/site-config/robot.env.example"

"$repo_root/deploy/scripts/collect_licenses.sh" \
  --output "$release_root/LICENSES" \
  --packages "$release_root/robot/packages"
"$repo_root/deploy/scripts/generate_manifest.sh" \
  --version "$version" --release-root "$release_root"
(
  cd "$release_root"
  find . -type f ! -name checksums.txt -print0 | sort -z | xargs -0 sha256sum > checksums.txt
)

tar --zstd -cf "$archive" -C "$dist_root" "$release_name"
printf '%s\n' "Built: $archive"
