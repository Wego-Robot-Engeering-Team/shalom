#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: deploy/scripts/collect_licenses.sh --output <LICENSES directory> --packages <deb directory>

Copies declared bundled-component notices and Debian copyright notices from the
actual offline .deb files. A missing declared notice is a release failure.
EOF
}

output=""
packages=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --output) output="${2:-}"; shift 2 ;;
    --packages) packages="${2:-}"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "알 수 없는 인자: $1" >&2; usage >&2; exit 2 ;;
  esac
done

if [[ -z "$output" || -z "$packages" ]]; then
  usage >&2
  exit 2
fi
if [[ ! -d "$packages" ]]; then
  echo ".deb 디렉터리가 없습니다: $packages" >&2
  exit 1
fi

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
components="$repo_root/deploy/config/bundled-license-components.tsv"
work="$(mktemp -d "${TMPDIR:-/tmp}/shalom-licenses.XXXXXX")"
trap 'rm -rf "$work"' EXIT

rm -rf "$output"
mkdir -p "$output/bundled" "$output/system" "$output/texts"

index="$output/INDEX.md"
printf '%s\n\n' '# Third-party license index' > "$index"
printf '%s\n' '| Component | Source | Notice |' >> "$index"
printf '%s\n' '| --- | --- | --- |' >> "$index"

while IFS=$'\t' read -r name source license notice; do
  [[ -z "$name" || "$name" == \#* ]] && continue
  source_dir="$repo_root/$source"
  license_source="$source_dir/$license"
  destination="$output/bundled/$name"
  if [[ ! -f "$license_source" ]]; then
    echo "필수 라이선스가 없습니다: $license_source" >&2
    exit 1
  fi
  mkdir -p "$destination"
  install -m 0644 "$license_source" "$destination/LICENSE"
  if [[ "$notice" != "-" ]]; then
    notice_source="$source_dir/$notice"
    if [[ ! -f "$notice_source" ]]; then
      echo "필수 NOTICE가 없습니다: $notice_source" >&2
      exit 1
    fi
    install -m 0644 "$notice_source" "$destination/NOTICE"
  fi
  printf '| `%s` | `%s` | `bundled/%s/` |\n' "$name" "$source" "$name" >> "$index"
done < "$components"

shopt -s nullglob
debs=("$packages"/*.deb)
if [[ ${#debs[@]} -eq 0 ]]; then
  echo "라이선스를 수집할 .deb 파일이 없습니다: $packages" >&2
  exit 1
fi

for deb in "${debs[@]}"; do
  package_name="$(dpkg-deb -f "$deb" Package)"
  extract_dir="$work/$package_name"
  mkdir -p "$extract_dir"
  dpkg-deb -x "$deb" "$extract_dir"
  copyright_files=("$extract_dir"/usr/share/doc/*/copyright)
  if [[ ${#copyright_files[@]} -eq 0 ]]; then
    printf '| `%s` | Debian package | no embedded copyright file |\n' "$package_name" >> "$index"
    continue
  fi
  for copyright_file in "${copyright_files[@]}"; do
    doc_package="$(basename "$(dirname "$copyright_file")")"
    install -m 0644 "$copyright_file" "$output/system/$doc_package.copyright"
    printf '| `%s` | Debian package | `system/%s.copyright` |\n' "$package_name" "$doc_package" >> "$index"
  done
done

for item in Apache-2.0 BSD-3-clause GPL-2 LGPL-2.1 LGPL-3; do
  source="/usr/share/common-licenses/$item"
  [[ -f "$source" ]] && install -m 0644 "$source" "$output/texts/$item.txt"
done
