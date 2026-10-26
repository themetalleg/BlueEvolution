#!/usr/bin/env bash
set -euo pipefail

build_dir=${1:?Flatpak Builder build directory is required}
app_id=${2:?Application ID is required}
version=${3:?Application version is required}

[[ -d "$build_dir" ]] || exit 0

keep=
best_objects=-1
best_generation=-1
for candidate in "$build_dir"/evolution-*; do
	[[ -f "$candidate/CMakeCache.txt" && -f "$candidate/build.ninja" ]] || continue
	grep -Fqx "CMAKE_INSTALL_PREFIX:PATH=/app" "$candidate/CMakeCache.txt" || continue
	grep -Fqx "OVERRIDE_APPLICATION_ID:BOOL=$app_id" "$candidate/CMakeCache.txt" || continue
	grep -Fqx "WITH_WMCLASS_OVERRIDE:STRING=$app_id" "$candidate/CMakeCache.txt" || continue
	grep -Fqx "BLUEEVOLUTION_VERSION:STRING=$version" "$candidate/CMakeCache.txt" || continue
	object_count=$(find "$candidate" -type f -name '*.o' -printf . | wc -c | tr -d '[:space:]')
	generation=${candidate##*-}
	[[ "$generation" =~ ^[0-9]+$ ]] || continue
	if (( object_count > best_objects || (object_count == best_objects && generation > best_generation) )); then
		keep=$candidate
		best_objects=$object_count
		best_generation=$generation
	fi
done

[[ -n "$keep" ]] || exit 0

removed=0
for candidate in "$build_dir"/evolution-*; do
	[[ -d "$candidate" && "$candidate" != "$keep" ]] || continue
	rm -rf -- "$candidate"
	((removed += 1))
done

if (( removed > 0 )); then
	printf 'Pruned %s obsolete Evolution build tree(s); kept %s (%s cached objects).\n' \
		"$removed" "${keep##*/}" "$best_objects"
fi