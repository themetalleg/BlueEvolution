#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
app_id="io.github.themetalleg.BlueEvolution"
action="${1:-launch}"

case "$action" in
install)
	version="${BLUEEVOLUTION_VERSION:-0.2.0-beta.1}"
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
		--command=env
	)
	preview_env=(
		HOME="$profile_root/home"
		XDG_CONFIG_HOME="$profile_root/config"
		XDG_DATA_HOME="$profile_root/data"
		XDG_CACHE_HOME="$profile_root/cache"
		GSETTINGS_BACKEND=keyfile
		GSETTINGS_SCHEMA_DIR=/app/share/glib-2.0/schemas
		BLUEEVOLUTION_DEMO_MODE=1
	)

	flatpak run "${run_args[@]}" "$app_id" "${preview_env[@]}" gsettings set \
		"$app_id" appearance-mode "$appearance"
	flatpak run "${run_args[@]}" "$app_id" "${preview_env[@]}" gsettings set \
		org.gnome.evolution.shell folder-bar-width 280
	flatpak run "${run_args[@]}" "$app_id" "${preview_env[@]}" gsettings set \
		org.gnome.evolution.shell buttons-style icons
	flatpak run "${run_args[@]}" "$app_id" "${preview_env[@]}" gsettings set \
		org.gnome.evolution.shell prefer-symbolic-icons yes
	flatpak run "${run_args[@]}" "$app_id" "${preview_env[@]}" gsettings set \
		org.gnome.evolution.shell toolbar-icon-size large
	flatpak run "${run_args[@]}" "$app_id" "${preview_env[@]}" gsettings set \
		org.gnome.evolution.shell use-header-bar true
	flatpak run "${run_args[@]}" "$app_id" "${preview_env[@]}" gsettings set \
		org.gnome.evolution.mail hpaned-size 1600000
	flatpak run "${run_args[@]}" "$app_id" "${preview_env[@]}" gsettings set \
		org.gnome.evolution.mail layout 1
	flatpak run "${run_args[@]}" "$app_id" "${preview_env[@]}" gsettings set \
		org.gnome.evolution.mail show-email false
	flatpak run "${run_args[@]}" "$app_id" "${preview_env[@]}" gsettings set \
		org.gnome.evolution.mail show-subject-above-sender false
	flatpak run "${run_args[@]}" "$app_id" "${preview_env[@]}" gsettings set \
		org.gnome.evolution.mail show-to-do-bar false
	flatpak run "${run_args[@]}" "$app_id" "${preview_env[@]}" gsettings set \
		org.gnome.evolution.mail show-preview-toolbar false
	flatpak run "${run_args[@]}" "$app_id" "${preview_env[@]}" gsettings set \
		org.gnome.evolution.mail show-startup-wizard false
	exec flatpak run "${run_args[@]}" "$app_id" "${preview_env[@]}" blueevolution --offline \
		--geometry="${BLUEEVOLUTION_DEV_GEOMETRY:-1600x950}"
	;;
*)
	echo "Usage: $0 [install|launch [system|light|dark]]" >&2
	exit 2
	;;
esac
