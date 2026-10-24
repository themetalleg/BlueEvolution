#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
manifest_source="$repo_root/flatpak/io.github.themetalleg.BlueEvolution.json"
build_root="${BUILD_ROOT:-$repo_root/build/flatpak}"
build_dir="$build_root/app"
state_dir="${FLATPAK_STATE_DIR:-$build_root/state}"
repo_dir="$build_root/repo"
version="${BLUEEVOLUTION_VERSION:-0.2.0-beta.1}"
if [[ ! "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.-]+)?(\+[0-9A-Za-z.-]+)?$ ]]; then
	echo "BLUEEVOLUTION_VERSION must be a semantic version." >&2
	exit 1
fi
channel="${BLUEEVOLUTION_CHANNEL:-}"
if [[ -z "$channel" ]]; then
	if [[ "$version" == *-* ]]; then channel=beta; else channel=stable; fi
fi
if [[ "$channel" != stable && "$channel" != beta ]]; then
	echo "BLUEEVOLUTION_CHANNEL must be stable or beta." >&2
	exit 1
fi
if [[ "$channel" == stable && "$version" == *-* ]]; then
	echo "Prerelease versions cannot be published to stable." >&2
	exit 1
fi
app_id=io.github.themetalleg.BlueEvolution
app_name=BlueEvolution
if [[ "$channel" == beta ]]; then
	app_id+=.Beta
	app_name="BlueEvolution Beta"
fi
bundle="${BUNDLE_PATH:-$build_root/BlueEvolution-${version}-x86_64.flatpak}"
manifest="$build_root/manifest.json"

if [[ "$(uname -m)" != "x86_64" ]]; then
	echo "BlueEvolution's current Flatpak package target is x86_64." >&2
	exit 1
fi

mkdir -p "$build_root" "$state_dir"
jq --arg source "$repo_root" --arg version "$version" --arg channel "$channel" \
	--arg app_id "$app_id" --arg app_name "$app_name" '
	walk(if type == "string" then gsub("io\\.github\\.themetalleg\\.BlueEvolution"; $app_id) else . end) |
	.["app-id"] = $app_id | .branch = $channel |
  (.modules[] | select(.name == "evolution") | .sources[0].path) = $source |
	(.modules[] | select(.name == "evolution") | .["config-opts"]) += [
		"-DBLUEEVOLUTION_VERSION=" + $version,
		"-DBLUEEVOLUTION_APPLICATION_NAME=" + $app_name
	] |
	(.modules[] | select(.name == "news-to-appdata") | .["build-commands"]) =
		["./update-appdata.sh " + (if $channel == "beta" then "development" else "stable" end)] |
	(.modules[] | select(.name == "news-to-appdata") | .sources) = [
		{
			type: "script", "dest-filename": "update-appdata.sh",
			commands: [
				"xsltproc --nonet --stringparam version " + $version + " --stringparam type \"$1\" --stringparam date \"$(date +%Y-%m-%d)\" release-metainfo.xsl /app/share/" + $app_id + ".metainfo.xml > /app/share/metainfo/" + $app_id + ".metainfo.xml",
				"xmllint --noout /app/share/metainfo/" + $app_id + ".metainfo.xml",
				"rm /app/share/NEWS.eds /app/share/NEWS.evo /app/share/NEWS.ews /app/share/news-to-appdata.c /app/share/" + $app_id + ".metainfo.xml"
			]
		},
		{ type: "file", path: ($source + "/packaging/release-metainfo.xsl") }
	]
' "$manifest_source" > "$manifest"

if [[ "${1:-}" == --prepare-only ]]; then
	echo "Manifest: $manifest ($channel, $app_id)"
	exit 0
fi

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
if [[ -n "${FLATPAK_REPO_URL:-}" ]]; then
	bundle_key_args+=(--repo-url="$FLATPAK_REPO_URL")
fi

if [[ -n "${FLATPAK_GPG_KEY_ID:-}" ]]; then
	public_key="$build_root/flatpak-signing-key.gpg"
	gpg --batch --yes --output "$public_key" --export "$FLATPAK_GPG_KEY_ID"
	export_args=(--gpg-sign "$FLATPAK_GPG_KEY_ID")
	bundle_key_args+=(--gpg-keys "$public_key")
fi

flatpak-builder --user --disable-rofiles-fuse --build-only --force-clean \
	--jobs="${FLATPAK_BUILD_JOBS:-2}" \
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

archive="$build_root/BlueEvolution-${version}-repository.tar.gz"
tar -C "$repo_dir" -czf "$archive" .
(cd "$build_root" && sha256sum "$(basename "$archive")" > "$(basename "$archive").sha256")
echo "Repository: $archive"
echo "Bundle: $bundle"
echo "Checksum: $bundle.sha256"