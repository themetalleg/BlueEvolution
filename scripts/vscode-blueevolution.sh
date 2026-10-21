#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
app_id="io.github.themetalleg.BlueEvolution"
action="${1:-launch}"

case "$action" in
install)
	version="${BLUEEVOLUTION_VERSION:-0.1.3-dev}"
	build_root="${BUILD_ROOT:-$repo_root/build/flatpak}"
	bundle="${BUNDLE_PATH:-$build_root/BlueEvolution-${version}-x86_64.flatpak}"
	BLUEEVOLUTION_VERSION="$version" "$repo_root/packaging/build-flatpak.sh"
	flatpak install --user --assumeyes --bundle "$bundle"
	;;
launch)
	appearance="${2:-system}"
	case "$appearance" in
		system|light|dark) ;;
		*) echo "Usage: $0 launch [system|light|dark]" >&2; exit 2 ;;
	esac

	if ! flatpak info --user "$app_id" >/dev/null 2>&1; then
		echo "BlueEvolution is not installed. Run the 'Build and install dev Flatpak' task first." >&2
		exit 1
	fi

	profile_root="${BLUEEVOLUTION_DEV_PROFILE:-${XDG_CACHE_HOME:-$HOME/.cache}/blueevolution-vscode-profile}"
	mkdir -p "$profile_root/home" "$profile_root/config" "$profile_root/data" "$profile_root/cache"

	run_args=(
		--user
		--unshare=network
		--filesystem="$profile_root"
		--nofilesystem='~/.gnupg'
		--nofilesystem='~/.pki'
		--nofilesystem='xdg-config/gnupg'
		--nofilesystem='xdg-config/pki'
		--no-talk-name=org.freedesktop.secrets
		--no-talk-name=org.gnome.keyring.SystemPrompter
		--no-talk-name=org.gnome.OnlineAccounts
		--env=HOME="$profile_root/home"
		--env=XDG_CONFIG_HOME="$profile_root/config"
		--env=XDG_DATA_HOME="$profile_root/data"
		--env=XDG_CACHE_HOME="$profile_root/cache"
		--env=GSETTINGS_BACKEND=keyfile
		--env=GSETTINGS_SCHEMA_DIR=/app/share/glib-2.0/schemas
	)

	flatpak run "${run_args[@]}" --command=gsettings "$app_id" set \
		"$app_id" appearance-mode "$appearance"
	exec flatpak run "${run_args[@]}" "$app_id" --offline
	;;
*)
	echo "Usage: $0 [install|launch [system|light|dark]]" >&2
	exit 2
	;;
esac
