#!/usr/bin/env bash
set -euo pipefail

usage() {
  echo "Usage: deploy/scripts/build_hmi_linux_release.sh --version <version> [--allow-dirty]"
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
  echo "이 스크립트는 Ubuntu 24.04 Linux x86_64 runner에서만 실행합니다." >&2
  exit 1
fi

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
hmi_root="$repo_root/hmi"
hmi_version="$(sed -n 's/^project(inspection_hmi VERSION \([^ ]*\).*/\1/p' "$hmi_root/CMakeLists.txt")"
if [[ "$version" != "$hmi_version" ]]; then
  echo "HMI project version($hmi_version)과 요청 버전($version)이 다릅니다." >&2
  exit 1
fi
git_status="$(git -C "$repo_root" status --porcelain --untracked-files=all)"
if [[ $allow_dirty -eq 0 ]] && [[ -n "$git_status" ]]; then
  printf '%s\n' "$git_status" >&2
  echo "Git 작업 트리가 깨끗하지 않습니다. 릴리스에는 --allow-dirty를 사용하지 마십시오." >&2
  exit 1
fi
command -v qtpaths6 >/dev/null || { echo "qtpaths6가 필요합니다." >&2; exit 1; }

qt_lib_dir="$(qtpaths6 --query QT_INSTALL_LIBS)"
qt_plugin_dir="$(qtpaths6 --query QT_INSTALL_PLUGINS)"
for path in "$qt_lib_dir" "$qt_plugin_dir"; do
  [[ -d "$path" ]] || { echo "Qt 경로가 없습니다: $path" >&2; exit 1; }
done

dist_root="$repo_root/dist"
release_name="inspection-hmi-$version-linux-x86_64"
stage_root="$dist_root/$release_name"
app_root="$stage_root/inspection-hmi"
archive="$dist_root/$release_name.tar.zst"
build_root="$(mktemp -d "${TMPDIR:-/tmp}/shalom-hmi-build.XXXXXX")"
trap 'rm -rf "$build_root"' EXIT

rm -rf "$stage_root" "$archive"
cmake -S "$hmi_root" -B "$build_root" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DHMI_BUILD_TESTS=OFF
cmake --build "$build_root" --parallel
cmake --install "$build_root" --prefix "$app_root"

# HMI는 실행 제품이고 고객 SDK가 아니다. CMake install 규칙의 계약 헤더는
# SDK 번들과 혼동되지 않도록 고객 HMI release에서 제외한다.
if [[ -d "$app_root/include" ]]; then
  find "$app_root/include" -depth -delete
fi

# Qt's Linux deploy helper is not implemented upstream. Ubuntu 24.04를 대상 OS로
# 고정하고, 실제 X11 실행에 필요한 Qt6 shared object와 plugin만 실행 파일 옆에 둔다.
mkdir -p "$app_root/plugins"
shopt -s nullglob
qt_libraries=()
for module in Core DBus Gui Network OpenGL Svg Widgets XcbQpa; do
  qt_libraries+=("$qt_lib_dir"/libQt6"$module".so*)
done
if [[ ${#qt_libraries[@]} -eq 0 ]]; then
  echo "Qt6 공유 라이브러리를 찾지 못했습니다: $qt_lib_dir" >&2
  exit 1
fi
cp -a "${qt_libraries[@]}" "$app_root/bin/"
mkdir -p "$app_root/plugins/platforms" "$app_root/plugins/imageformats" \
  "$app_root/plugins/iconengines" "$app_root/plugins/tls" "$app_root/plugins/xcbglintegrations"
install -m 0755 "$qt_plugin_dir/platforms/libqxcb.so" "$app_root/plugins/platforms/"
for plugin in libqgif.so libqico.so libqjpeg.so libqsvg.so; do
  [[ -f "$qt_plugin_dir/imageformats/$plugin" ]] && install -m 0755 "$qt_plugin_dir/imageformats/$plugin" "$app_root/plugins/imageformats/"
done
[[ -f "$qt_plugin_dir/iconengines/libqsvgicon.so" ]] && install -m 0755 "$qt_plugin_dir/iconengines/libqsvgicon.so" "$app_root/plugins/iconengines/"
for plugin in libqcertonlybackend.so libqopensslbackend.so; do
  [[ -f "$qt_plugin_dir/tls/$plugin" ]] && install -m 0755 "$qt_plugin_dir/tls/$plugin" "$app_root/plugins/tls/"
done
for plugin in libqxcb-egl-integration.so libqxcb-glx-integration.so; do
  [[ -f "$qt_plugin_dir/xcbglintegrations/$plugin" ]] && install -m 0755 "$qt_plugin_dir/xcbglintegrations/$plugin" "$app_root/plugins/xcbglintegrations/"
done

# The Qt 6.4 binary distribution links against the ICU ABI it was built with.
# Ubuntu 24.04 does not guarantee that legacy ABI is installed on the customer
# PC, so ship the exact resolved ICU closure alongside Qt instead of relying on
# the system package version.
mapfile -t icu_libraries < <(
  for library in "$app_root/bin"/libQt6*.so* "$app_root/plugins"/*/*.so; do
    [[ -f "$library" ]] || continue
    LD_LIBRARY_PATH="$app_root/bin" ldd "$library" 2>/dev/null |
      awk '/libicu[^ ]* => \/[^ ]+/ {print $3}'
  done | sort -u
)
if [[ ${#icu_libraries[@]} -eq 0 ]]; then
  echo "Qt ICU 런타임 의존성을 찾지 못했습니다." >&2
  exit 1
fi
mkdir -p "$app_root/licenses"
install -m 0644 "$hmi_root/licenses"/*.txt "$hmi_root/licenses/README.md" "$app_root/licenses/"
for library in "${icu_libraries[@]}"; do
  library_dir="$(dirname "$library")"
  library_stem="$(basename "$library" | sed -E 's/(\.so).*/\1/')"
  cp -a "$library_dir/$library_stem"* "$app_root/bin/"

  # Debian package copyright files identify the ICU copyright holders and its
  # Unicode licence. Include them when that metadata is available.
  owner="$(dpkg-query -S "$(readlink -f "$library")" 2>/dev/null | head -n1 | cut -d: -f1 || true)"
  [[ -n "$owner" && -f "/usr/share/doc/$owner/copyright" ]] &&
    install -m 0644 "/usr/share/doc/$owner/copyright" "$app_root/licenses/$owner.copyright"
done
cat > "$app_root/bin/qt.conf" <<'EOF'
[Paths]
Plugins = ../plugins
EOF
install -m 0755 "$repo_root/deploy/packaging/hmi/run.sh.in" "$app_root/run.sh"

for target in "$app_root/bin/inspection_hmi" "$app_root/plugins/platforms/libqxcb.so"; do
  [[ -f "$target" ]] || { echo "필수 HMI runtime 파일이 없습니다: $target" >&2; exit 1; }
  if LD_LIBRARY_PATH="$app_root/bin" ldd "$target" | grep -q 'not found'; then
    echo "런타임 의존성이 누락되었습니다: $target" >&2
    LD_LIBRARY_PATH="$app_root/bin" ldd "$target" >&2
    exit 1
  fi
done

(cd "$stage_root" && find . -type f ! -name checksums.txt -print0 | sort -z | xargs -0 sha256sum > checksums.txt)
tar --zstd -cf "$archive" -C "$dist_root" "$release_name"
printf '%s\n' "Built: $archive"
