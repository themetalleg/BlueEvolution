#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build_root="${BUILD_ROOT:-$repo_root/build/flatpak-local-preview}"
build_dir="$build_root/app"
manifest="$build_root/manifest.json"
preview_profile="${BLUEEVOLUTION_DEV_PROFILE:-${XDG_CACHE_HOME:-$HOME/.cache}/blueevolution-vscode-source-profile}"
version="${BLUEEVOLUTION_VERSION:-0.2.3-dev.1}"
channel="${BLUEEVOLUTION_CHANNEL:-beta}"
bundle="${BUNDLE_PATH:-$build_root/BlueEvolution-${version}-x86_64.flatpak}"

app_id=$(jq -er '.["app-id"]' "$manifest")
branch=$(jq -er '.branch' "$manifest")

build_inputs=(CMakeLists.txt config.h.in cmake data flatpak po src tests NEWS NEWS-1.0 appdata-releases.txt)
changed_files=$( { git -C "$repo_root" diff --name-only HEAD -- "${build_inputs[@]}"; git -C "$repo_root" ls-files --others --exclude-standard -- "${build_inputs[@]}"; } | sort -u )
while IFS= read -r path; do
	[[ -n "$path" ]] || continue
	if [[ "$path" == CMakeLists.txt || "$path" == */CMakeLists.txt || "$path" == cmake/* || "$path" == flatpak/* ]]; then
		printf 'Build configuration changed; incremental preview is not applicable: %s\n' "$path" >&2
		exit 2
	fi
done <<< "$changed_files"

source_stage=
best_objects=-1
for candidate in "$repo_root"/.flatpak-builder/build/evolution-*; do
	[[ -f "$candidate/CMakeCache.txt" && -f "$candidate/build.ninja" ]] || continue
	grep -Fqx "CMAKE_INSTALL_PREFIX:PATH=/app" "$candidate/CMakeCache.txt" || continue
	grep -Fqx "OVERRIDE_APPLICATION_ID:BOOL=$app_id" "$candidate/CMakeCache.txt" || continue
	grep -Fqx "WITH_WMCLASS_OVERRIDE:STRING=$app_id" "$candidate/CMakeCache.txt" || continue
	grep -Fqx "BLUEEVOLUTION_VERSION:STRING=$version" "$candidate/CMakeCache.txt" || continue
	object_count=$(find "$candidate" -type f -name '*.o' -printf . | wc -c)
	if (( object_count > best_objects )); then
		source_stage="$candidate"
		best_objects=$object_count
	fi
done
if [[ -z "$source_stage" ]]; then
	echo "No reusable SDK/Ninja tree matches $app_id $version." >&2
	exit 2
fi

while IFS= read -r path; do
	[[ -n "$path" ]] || continue
	dir=$(dirname "$path")
	if [[ -e "$repo_root/$path" ]]; then
		mkdir -p "$source_stage/$dir"
		cp -p "$repo_root/$path" "$source_stage/$path"
	else
		rm -f -- "$source_stage/$path"
	fi
done <<< "$changed_files"

preview_install="${FLATPAK_USER_DIR:-${XDG_DATA_HOME:-$HOME/.local/share}/flatpak}"
wrapper="$build_root/.preview-evolution-wrapper"
if [[ ! -f "$wrapper" ]]; then
	preview_location=$(FLATPAK_USER_DIR="$preview_profile/flatpak" FLATPAK_SYSTEM_DIR="$preview_install" flatpak info --user --show-location "$app_id" 2>/dev/null || true)
	if [[ -n "$preview_location" && -f "$preview_location/files/bin/evolution" ]] && grep -q '/app/bin/evolution.bin' "$preview_location/files/bin/evolution"; then
		cp -p "$preview_location/files/bin/evolution" "$wrapper"
	elif [[ -f "$build_dir/files/bin/evolution" ]] && grep -q '/app/bin/evolution.bin' "$build_dir/files/bin/evolution"; then
		cp -p "$build_dir/files/bin/evolution" "$wrapper"
	else
		echo "Missing preview Evolution service wrapper; use a full preview build once." >&2
		exit 2
	fi
fi

flatpak build --die-with-parent --nofilesystem=host:reset --bind-mount="/run/build/evolution=$source_stage" --build-dir=/run/build/evolution "$build_dir" /usr/bin/ninja -j"${FLATPAK_BUILD_JOBS:-2}" -C /run/build/evolution
flatpak build --die-with-parent --nofilesystem=host:reset --bind-mount="/run/build/evolution=$source_stage" --build-dir=/run/build/evolution "$build_dir" /usr/bin/ninja -j"${FLATPAK_BUILD_JOBS:-2}" -C /run/build/evolution install

mv -f "$build_dir/files/bin/evolution" "$build_dir/files/bin/evolution.bin"
cp -p "$wrapper" "$build_dir/files/bin/evolution"
install -D -m 644 "$build_dir/files/share/icons/hicolor/scalable/apps/blueevolution.svg" "$build_dir/files/share/icons/hicolor/scalable/apps/$app_id.svg"
metainfo="$build_dir/files/share/metainfo/$app_id.metainfo.xml"
tmp_metainfo="$metainfo.tmp"
release_type=stable
if [[ "$channel" == beta ]]; then release_type=development; fi
flatpak build --die-with-parent --nofilesystem=host:reset \
	--bind-mount="/run/build/release-metainfo.xsl=$repo_root/packaging/release-metainfo.xsl" \
	"$build_dir" /usr/bin/xsltproc --nonet \
	--stringparam version "$version" --stringparam type "$release_type" \
	--stringparam date "$(date +%Y-%m-%d)" /run/build/release-metainfo.xsl \
	"/app/share/metainfo/$app_id.metainfo.xml" > "$tmp_metainfo"
flatpak build --die-with-parent --nofilesystem=host:reset \
	"$build_dir" /usr/bin/xmllint --noout "/app/share/metainfo/$app_id.metainfo.xml.tmp"
mv "$tmp_metainfo" "$metainfo"
flatpak build-export --arch=x86_64 --no-update-summary "$build_root/repo" "$build_dir" "$branch"
flatpak build-bundle --arch=x86_64 --runtime-repo="${FLATPAK_RUNTIME_REPO:-https://dl.flathub.org/repo/flathub.flatpakrepo}" "$build_root/repo" "$bundle" "$app_id" "$branch"
(cd "$(dirname "$bundle")" && sha256sum "$(basename "$bundle")" > "$(basename "$bundle").sha256")
printf 'Incremental preview rebuilt from %s (%s cached objects).\n' "$source_stage" "$best_objects"
