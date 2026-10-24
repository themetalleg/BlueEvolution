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
launch|preview)
	appearance="${2:-system}"
	case "$appearance" in
		system|light|dark) ;;
		*) echo "Usage: $0 launch [system|light|dark]" >&2; exit 2 ;;
	esac

	if [[ "$action" == launch ]] && ! flatpak info --user "$app_id" >/dev/null 2>&1; then
		echo "BlueEvolution is not installed. Run the 'Build and install dev Flatpak' task first." >&2
		exit 1
	fi

	profile_name=blueevolution-vscode-profile
	if [[ "$action" == preview ]]; then
		profile_name="blueevolution-vscode-source-${appearance}-profile"
	fi
	profile_root="${BLUEEVOLUTION_DEV_PROFILE:-${XDG_CACHE_HOME:-$HOME/.cache}/$profile_name}"
	if [[ "$action" == preview && "${BLUEEVOLUTION_PREVIEW_BUILD:-true}" == true ]]; then
		BLUEEVOLUTION_DEV_PROFILE="$profile_root" "$repo_root/packaging/build-preview.sh"
	fi
	mkdir -p "$profile_root/home" "$profile_root/config" "$profile_root/data" "$profile_root/cache"
	mkdir -p -m 700 "$profile_root/runtime"

	run_args=(
		--unshare=network
		--no-documents-portal
		--filesystem="$profile_root"
		--nofilesystem='~/.gnupg'
		--nofilesystem='~/.pki'
		--nofilesystem='xdg-config/gnupg'
		--nofilesystem='xdg-config/pki'
	)
	if [[ "${GDK_BACKEND:-}" == x11 ]]; then
		run_args+=(--socket=x11 --nosocket=wayland)
	fi
	session_installation=()
	if [[ "$action" == preview ]]; then
		build_root="${BUILD_ROOT:-$repo_root/build/flatpak-local-preview}"
		if [[ ! -f "$build_root/app/metadata" || ! -f "$build_root/manifest.json" ]]; then
			echo "Build the local preview first; see packaging/README.md." >&2
			exit 1
		fi
		preview_app_id=$(jq -er '.["app-id"]' "$build_root/manifest.json")
		version=$(jq -er '.modules[] | select(.name == "evolution") | .["config-opts"][] | select(startswith("-DBLUEEVOLUTION_VERSION=")) | ltrimstr("-DBLUEEVOLUTION_VERSION=")' "$build_root/manifest.json")
		bundle="${BUNDLE_PATH:-$build_root/BlueEvolution-${version}-x86_64.flatpak}"
		if [[ ! -f "$bundle" ]]; then
			echo "Local preview bundle not found; set BUNDLE_PATH to the built bundle." >&2
			exit 1
		fi
		installed_root="${FLATPAK_USER_DIR:-${XDG_DATA_HOME:-$HOME/.local/share}/flatpak}"
		preview_flatpak=(env "FLATPAK_USER_DIR=$profile_root/flatpak" "FLATPAK_SYSTEM_DIR=$installed_root" flatpak)
		session_installation=("FLATPAK_USER_DIR=$profile_root/flatpak" "FLATPAK_SYSTEM_DIR=$installed_root")
		branch=$(jq -er '.branch' "$build_root/manifest.json")
		expected_commit=""
		if [[ -f "$build_root/repo/refs/heads/app/$preview_app_id/x86_64/$branch" ]]; then
			read -r expected_commit < "$build_root/repo/refs/heads/app/$preview_app_id/x86_64/$branch"
		fi
		current_commit=$("${preview_flatpak[@]}" info --user --show-commit "$preview_app_id" 2>/dev/null || true)
		if [[ ! "$expected_commit" =~ ^[0-9a-f]{64}$ || "$current_commit" != "$expected_commit" ]]; then
			"${preview_flatpak[@]}" install --user --or-update --assumeyes --noninteractive --no-deps --bundle "$bundle"
		fi
		runner=("${preview_flatpak[@]}" run --user "${run_args[@]}" \
			--no-talk-name=org.freedesktop.secrets \
			--no-talk-name=org.gnome.keyring.SystemPrompter \
			--no-talk-name=org.gnome.OnlineAccounts --command=env "$preview_app_id")
	else
		runner=(flatpak run --user "${run_args[@]}" \
			--no-talk-name=org.freedesktop.secrets \
			--no-talk-name=org.gnome.keyring.SystemPrompter \
			--no-talk-name=org.gnome.OnlineAccounts --command=env "$app_id")
	fi
	preview_env=(
		HOME="$profile_root/home"
		XDG_CONFIG_HOME="$profile_root/config"
		XDG_DATA_HOME="$profile_root/data"
		XDG_CACHE_HOME="$profile_root/cache"
		GSETTINGS_BACKEND=keyfile
		GSETTINGS_SCHEMA_DIR=/app/share/glib-2.0/schemas
		BLUEEVOLUTION_DEMO_MODE=1
	)
	if [[ -n "${GDK_BACKEND:-}" ]]; then
		preview_env+=("GDK_BACKEND=$GDK_BACKEND")
	fi
	if [[ "${GDK_BACKEND:-}" == x11 && -n "${DISPLAY:-}" ]]; then
		preview_env+=("DISPLAY=$DISPLAY")
	fi

	"${runner[@]}" "${preview_env[@]}" gsettings set \
		"$app_id" appearance-mode "$appearance"
	"${runner[@]}" "${preview_env[@]}" gsettings set \
		org.gnome.evolution.shell folder-bar-width 280
	"${runner[@]}" "${preview_env[@]}" gsettings set \
		org.gnome.evolution.shell buttons-style icons
	"${runner[@]}" "${preview_env[@]}" gsettings set \
		org.gnome.evolution.shell prefer-symbolic-icons yes
	"${runner[@]}" "${preview_env[@]}" gsettings set \
		org.gnome.evolution.shell toolbar-icon-size large
	"${runner[@]}" "${preview_env[@]}" gsettings set \
		org.gnome.evolution.shell use-header-bar true
	"${runner[@]}" "${preview_env[@]}" gsettings set \
		org.gnome.evolution.mail hpaned-size 1600000
	"${runner[@]}" "${preview_env[@]}" gsettings set \
		org.gnome.evolution.mail layout 1
	"${runner[@]}" "${preview_env[@]}" gsettings set \
		org.gnome.evolution.mail show-email false
	"${runner[@]}" "${preview_env[@]}" gsettings set \
		org.gnome.evolution.mail show-subject-above-sender false
	"${runner[@]}" "${preview_env[@]}" gsettings set \
		org.gnome.evolution.mail show-to-do-bar false
	"${runner[@]}" "${preview_env[@]}" gsettings set \
		org.gnome.evolution.mail show-preview-toolbar false
	"${runner[@]}" "${preview_env[@]}" gsettings set \
		org.gnome.evolution.mail show-startup-wizard false
	exec env -u LD_LIBRARY_PATH -u LD_PRELOAD -u GTK_PATH -u GIO_EXTRA_MODULES -u GIO_MODULE_DIR \
		"${session_installation[@]}" "XDG_RUNTIME_DIR=$profile_root/runtime" \
		dbus-run-session -- "${runner[@]}" "${preview_env[@]}" blueevolution --offline \
		--geometry="${BLUEEVOLUTION_DEV_GEOMETRY:-1600x950}"
	;;
*)
	echo "Usage: $0 [install|launch [system|light|dark]|preview [system|light|dark]]" >&2
	exit 2
	;;
esac
