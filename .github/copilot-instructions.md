# BlueEvolution Project Instructions

BlueEvolution is a C/GObject, GTK 3 and WebKit fork of GNOME Evolution. Preserve
existing architecture and keep changes focused. Use synthetic mail for tests;
never copy private messages, credentials, or signing keys into code or logs.

## Releases and Channels

- Read [the release guide](../packaging/README.md) before packaging, publishing,
  changing channel identities, or migrating installations. Keep it and the
  [root README](../README.md) consistent with the actual workflows.
- Distribution is already live through GitHub Actions, GitHub Releases and the
  signed GitHub Pages Flatpak repository. Do not describe updates as manual-only
  or replace this with a custom in-app updater or a separately managed server.
- Stable uses `io.github.themetalleg.BlueEvolution` on branch `stable`; beta uses
  `io.github.themetalleg.BlueEvolution.Beta` on branch `beta`. Keep runtime IDs,
  desktop entries, WMClass, AppStream metadata and EDS D-Bus prefixes aligned.
  The apps must coexist with independent local data/configuration directories.
- `blueevolution-vX.Y.Z` tags route to stable; semantic prereleases such as
  `blueevolution-vX.Y.Z-beta.N` route to beta. Never send prereleases to stable,
  reuse published versions, rewrite release tags, or downgrade a channel.
- A local build is not publication. When explicitly asked to publish, complete
  the authorized commit/push, signed draft build, draft review/publication,
  Pages deployment and live-channel verification. Do not claim completion while
  those steps are merely configured or still running. Do not publish unasked.

## Publication Safeguards

- Reuse the protected `FLATPAK_GPG_PRIVATE_KEY` secret. Never print/export its
  value, commit a private key, disable GPG verification, or silently change keys.
- Preserve both channels, the `flatpak-state` branch and its referenced immutable
  `BlueEvolution-updates-RUN-ATTEMPT.tar.gz` asset. Advance state only after a
  successful deployment; failed retries must not overwrite the active checkpoint.
- Preserve the unique publication commit used as the Pages build identity.
  `packaging/pages-deploy` is a Node action wrapping the unchanged official
  deployment code: a reserved `GITHUB_SHA` step-env override is ignored, while
  running that code as a shell step loses the artifact runtime token.
- Check live installer links and explicit remote refs, not only workflow success.
  A user's publication action triggers the publisher; do not assume publishing
  with a workflow's `GITHUB_TOKEN` triggers another workflow automatically.

## Build and Validation

- Run `npm --prefix packaging ci --ignore-scripts` and `npm --prefix packaging test`.
  Run `BLUEEVOLUTION_TEST_FLATPAK=1 npm --prefix packaging test` for signed
  side-by-side install/update coverage; dependencies are listed in the guide.
- Support the runner's jq 1.6 syntax. Keep Flatpak build and builder-state paths
  on the same filesystem. Default compilation is bounded to two jobs to avoid
  memory-pressure kills; override with `FLATPAK_BUILD_JOBS` when appropriate.
- Generate release metadata with `packaging/release-metainfo.xsl` and validate
  XML; do not replace XML with text substitutions. Headless native CI gates use
  the installed bundle inside `dbus-run-session` and `xvfb-run`, not just the SDK.

## Installed Profiles and Updates

- Back up and verify profiles before removal or migration. Preserve accounts,
  settings, local data and the desktop keyring; do not use `--delete-data`.
  Do not force-close mail windows or duplicate queued outgoing messages.
- Migrate existing bundles with an explicit remote/ref reinstall, as documented;
  the `.flatpakref` shortcut may refuse an already-installed app without changing
  its origin. Verify both origins are `blueevolution` and profiles stay separate.
- Users control update installation through their software manager. Do not add
  a BlueEvolution-specific update timer or in-app updater; preserve the signed
  Flatpak release infrastructure and the user's software-manager settings.