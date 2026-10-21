#!/usr/bin/env python3
import re
import sys
from pathlib import Path


def replace_once(text: str, pattern: str, replacement: str) -> str:
	updated, count = re.subn(pattern, replacement, text, count=1, flags=re.MULTILINE)
	if count != 1:
		raise SystemExit(f"Expected one upstream metadata line matching {pattern!r}")
	return updated


if len(sys.argv) != 5:
	raise SystemExit("usage: update-upstream-metadata.py VERSION EVOLUTION_SHA EDS_SHA EWS_SHA")

version, evolution_sha, eds_sha, ews_sha = sys.argv[1:]
if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version):
	raise SystemExit(f"Not a stable release version: {version}")
for sha in (evolution_sha, eds_sha, ews_sha):
	if not re.fullmatch(r"[0-9a-f]{40}", sha):
		raise SystemExit(f"Not a Git commit hash: {sha}")

path = Path("BLUEEVOLUTION.md")
text = path.read_text(encoding="utf-8")
text = replace_once(text, r"^- Stable release: `[^`]+`$", f"- Stable release: `{version}`")
text = replace_once(text, r"^- Release commit: `[^`]+`$", f"- Release commit: `{evolution_sha}`")
text = replace_once(
	text,
	r"^- Evolution Data Server: `[^`]+` \(`[^`]+`\)$",
	f"- Evolution Data Server: `{version}` (`{eds_sha}`)",
)
text = replace_once(
	text,
	r"^- Evolution EWS: `[^`]+` \(`[^`]+`\)$",
	f"- Evolution EWS: `{version}` (`{ews_sha}`)",
)
path.write_text(text, encoding="utf-8")