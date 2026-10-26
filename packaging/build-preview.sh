#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build_root="${BUILD_ROOT:-$repo_root/build/flatpak-local-preview}"
version="${BLUEEVOLUTION_VERSION:-0.2.3-dev.1}"
channel="${BLUEEVOLUTION_CHANNEL:-beta}"
bundle="${BUNDLE_PATH:-$build_root/BlueEvolution-${version}-x86_64.flatpak}"
state_dir="${FLATPAK_STATE_DIR:-$build_root/state}"
marker="$build_root/.preview-source-id"
checksum="${bundle}.sha256"

prune_obsolete_builder_trees() {
	local app_id=io.github.themetalleg.BlueEvolution
	[[ "$channel" == beta ]] && app_id+=.Beta
	bash "$repo_root/scripts/prune-flatpak-builder-cache.sh" \
		"$state_dir/build" "$app_id" "$version"
}

mkdir -p "$build_root"
exec 9>"$build_root/.preview-build.lock"
flock 9

source_id=$(
	{
		printf '%s\0' "$version" "$channel" "${FLATPAK_BUILD_JOBS:-2}" \
			"$(flatpak-builder --version)" "$(uname -m)"
		git -C "$repo_root" rev-parse HEAD
		build_inputs=(CMakeLists.txt config.h.in cmake data flatpak po src tests NEWS NEWS-1.0 appdata-releases.txt packaging/build-flatpak.sh packaging/release-metainfo.xsl scripts/import-bootstrap-icons.mjs)
		git -C "$repo_root" status --porcelain=v1 --untracked-files=all -- "${build_inputs[@]}"
		git -C "$repo_root" diff --binary HEAD -- "${build_inputs[@]}"
		git -C "$repo_root" ls-files --others --exclude-standard -z -- "${build_inputs[@]}" |
			while IFS= read -r -d '' path; do
				sha256sum -- "$repo_root/$path"
			done
	} | sha256sum | cut -d ' ' -f 1
)

if [[ "${1:-}" == --adopt-existing ]]; then
	if [[ ! -f "$bundle" || ! -f "$checksum" || ! -f "$build_root/manifest.json" || ! -f "$build_root/app/metadata" ]]; then
		echo "Cannot adopt a preview without its bundle, checksum, manifest and app metadata." >&2
		exit 1
	fi
	(cd "$(dirname "$bundle")" && sha256sum --check "$(basename "$checksum")")
	app_id=$(jq -er '.["app-id"]' "$build_root/manifest.json")
	branch=$(jq -er '.branch' "$build_root/manifest.json")
	grep -Fxq "name=$app_id" "$build_root/app/metadata"
	while IFS= read -r -d '' path; do
		if [[ -e "$repo_root/$path" && "$repo_root/$path" -nt "$bundle" ]]; then
			echo "Application input is newer than the preview bundle: $path" >&2
			exit 1
		fi
	done < <(git -C "$repo_root" ls-files --cached --others --exclude-standard -z -- \
		CMakeLists.txt config.h.in cmake data flatpak po src tests NEWS NEWS-1.0 appdata-releases.txt packaging/build-flatpak.sh packaging/release-metainfo.xsl scripts/import-bootstrap-icons.mjs)
	bundle_repo=$(mktemp -d "$build_root/.preview-bundle-check.XXXXXX")
	trap 'rm -rf -- "$bundle_repo"' EXIT
	ostree init --repo="$bundle_repo" --mode=archive-z2
	flatpak build-import-bundle --no-update-summary "$bundle_repo" "$bundle"
	ref="app/$app_id/$(uname -m)/$branch"
	bundle_commit=$(ostree rev-parse --repo="$bundle_repo" "$ref")
	build_commit=$(ostree rev-parse --repo="$build_root/repo" "$ref")
	if [[ "$bundle_commit" != "$build_commit" ]]; then
		echo "Preview bundle does not match its local OSTree ref." >&2
		exit 1
	fi
	trap - EXIT
	rm -rf -- "$bundle_repo"
	printf '%s\n' "$source_id" > "$marker.tmp"
	mv "$marker.tmp" "$marker"
	echo "Verified and cached the existing preview bundle ($version, $channel)."
	exit 0
fi

if [[ -f "$marker" && -f "$bundle" && -f "$checksum" && -f "$build_root/manifest.json" ]] &&
	[[ "$(<"$marker")" == "$source_id" ]] &&
	(cd "$(dirname "$bundle")" && sha256sum --status --check "$(basename "$checksum")"); then
	prune_obsolete_builder_trees
	echo "Preview build is current ($version, $channel); reusing cached bundle."
	exit 0
fi

incremental_status=2
if [[ -f "$build_root/manifest.json" && -f "$build_root/app/metadata" ]]; then
	set +e
	BUILD_ROOT="$build_root" BLUEEVOLUTION_VERSION="$version" BLUEEVOLUTION_CHANNEL="$channel" \
		"$repo_root/scripts/vscode-blueevolution-incremental.sh"
	incremental_status=$?
	set -e
fi

if [[ "$incremental_status" == 0 ]]; then
	(cd "$(dirname "$bundle")" && sha256sum --check "$(basename "$checksum")")
	prune_obsolete_builder_trees
	printf '%s\n' "$source_id" > "$marker.tmp"
	mv "$marker.tmp" "$marker"
	exit 0
elif [[ "$incremental_status" != 2 ]]; then
	echo "Incremental preview update failed; preserving the existing candidate." >&2
	exit "$incremental_status"
fi

FLATPAK_BUILD_FORCE_CLEAN=true \
	BUILD_ROOT="$build_root" \
	BLUEEVOLUTION_VERSION="$version" \
	BLUEEVOLUTION_CHANNEL="$channel" \
	"$repo_root/packaging/build-flatpak.sh"

(cd "$(dirname "$bundle")" && sha256sum --check "$(basename "$checksum")")
prune_obsolete_builder_trees
printf '%s\n' "$source_id" > "$marker.tmp"
mv "$marker.tmp" "$marker"