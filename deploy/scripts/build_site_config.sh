#!/usr/bin/env bash
set -euo pipefail

usage() {
  echo "Usage: deploy/scripts/build_site_config.sh --version <version>"
}

VERSION=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --version) VERSION="${2:-}"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "알 수 없는 인자: $1" >&2; usage >&2; exit 2 ;;
  esac
done

if [[ -z "$VERSION" || ! "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+([.-][A-Za-z0-9]+)*$ ]]; then
  echo "--version <major.minor.patch>가 필요합니다." >&2
  exit 2
fi

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
template_root="$repo_root/deploy/packaging/site-config"
dist_root="$repo_root/dist/robot"
stage_root="$(mktemp -d "${TMPDIR:-/tmp}/shalom-site-config.${VERSION}.XXXXXX")"
trap 'rm -rf "$stage_root"' EXIT

mkdir -p "$dist_root"
chmod 0755 "$stage_root"
install -d -m 0755 "$stage_root/DEBIAN" "$stage_root/etc" "$stage_root/etc/shalom" \
  "$stage_root/var" "$stage_root/var/lib" "$stage_root/var/lib/shalom" \
  "$stage_root/var/lib/shalom/maps" "$stage_root/var/log" "$stage_root/var/log/shalom"
install -m 0640 "$template_root/etc/shalom/robot.env" "$stage_root/etc/shalom/robot.env"
install -m 0644 "$template_root/debian/conffiles" "$stage_root/DEBIAN/conffiles"
sed "s/@VERSION@/$VERSION/g" "$template_root/debian/control.in" \
  > "$stage_root/DEBIAN/control"

dpkg-deb --root-owner-group --build "$stage_root" \
  "$dist_root/shalom-site-config_${VERSION}_all.deb"
printf '%s\n' "Built: $dist_root/shalom-site-config_${VERSION}_all.deb"
