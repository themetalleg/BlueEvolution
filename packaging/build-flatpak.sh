#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
manifest_source="$repo_root/flatpak/io.github.themetalleg.BlueEvolution.json"
build_root="${BUILD_ROOT:-$repo_root/build/flatpak}"
build_dir="$build_root/app"
state_dir="${FLATPAK_STATE_DIR:-$build_root/state}"
repo_dir="$build_root/repo"
version="${BLUEEVOLUTION_VERSION:-0.2.0-beta.1}"
bundle="${BUNDLE_PATH:-$build_root/BlueEvolution-${version}-x86_64.flatpak}"
manifest="$build_root/manifest.json"

if [[ "$(uname -m)" != "x86_64" ]]; then
	echo "BlueEvolution's current Flatpak package target is x86_64." >&2
	exit 1
fi

mkdir -p "$build_root" "$state_dir"
jq --arg source "$repo_root" --arg version "$version" '
  (.modules[] | select(.name == "evolution") | .sources[0].path) = $source |
  (.modules[] | select(.name == "evolution") | .["config-opts"]) += ["-DBLUEEVOLUTION_VERSION=" + $version]
' "$manifest_source" > "$manifest"

builder_version=$(flatpak-builder --version | sed -E 's/.*[^0-9]([0-9]+\.[0-9]+\.[0-9]+).*/\1/')
if [[ "$(printf '%s\n' "$builder_version" 1.4.8 | sort -V | head -n 1)" == "1.4.8" ]]; then
	jq '
	  (.modules[] | select(.name == "geocode-glib" or .name == "gnome-online-accounts" or .name == "libgweather4") | .["config-opts"]) |= map(select(. != "-Dlibdir=lib"))
	' "$manifest" > "$manifest.tmp"
	mv "$manifest.tmp" "$manifest"
fi

runtime=$(jq -r '.runtime' "$manifest")
runtime_version=$(jq -r '.["runtime-version"]' "$manifest")
sdk=$(jq -r '.sdk' "$manifest")
app_id=$(jq -r '.["app-id"]' "$manifest")
command=$(jq -r '.command' "$manifest")
branch=$(jq -r '.branch' "$manifest")
mapfile -t finish_args < <(jq -r '.["finish-args"][]' "$manifest")
export_args=()
bundle_key_args=()

if [[ -n "${FLATPAK_GPG_KEY_ID:-}" ]]; then
	public_key="$build_root/flatpak-signing-key.gpg"
	gpg --batch --yes --output "$public_key" --export "$FLATPAK_GPG_KEY_ID"
	export_args=(--gpg-sign "$FLATPAK_GPG_KEY_ID")
	bundle_key_args=(--gpg-keys "$public_key")
fi

flatpak-builder --user --disable-rofiles-fuse --build-only --force-clean \
	--state-dir="$state_dir" --repo="$repo_dir" "$build_dir" "$manifest"
install -D -m644 \
	"$build_dir/files/share/icons/hicolor/scalable/apps/blueevolution.svg" \
	"$build_dir/files/share/icons/hicolor/scalable/apps/$app_id.svg"
flatpak build-finish \
	--command="$command" \
	--runtime="$runtime/x86_64/$runtime_version" \
	--sdk="$sdk/x86_64/$runtime_version" \
	"${finish_args[@]}" "$build_dir"
flatpak build-export --arch=x86_64 --no-update-summary "${export_args[@]}" "$repo_dir" "$build_dir" "$branch"
flatpak build-bundle \
	--arch=x86_64 \
	--runtime-repo="${FLATPAK_RUNTIME_REPO:-https://dl.flathub.org/repo/flathub.flatpakrepo}" \
	"${bundle_key_args[@]}" \
	"$repo_dir" "$bundle" "$app_id" "$branch"
bundle_dir=$(dirname "$bundle")
bundle_name=$(basename "$bundle")
(cd "$bundle_dir" && sha256sum "$bundle_name" > "$bundle_name.sha256")

echo "Bundle: $bundle"
echo "Checksum: $bundle.sha256"