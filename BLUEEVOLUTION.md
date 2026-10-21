# BlueEvolution

BlueEvolution is an independent fork of [GNOME Evolution](https://gitlab.gnome.org/GNOME/evolution) with the goal of a modern, Outlook-inspired Linux interface. It is not affiliated with Microsoft or GNOME.

Public repository: [themetalleg/BlueEvolution](https://github.com/themetalleg/BlueEvolution). The first release is a draft prerelease, not a stable release.

## Current Status

This is an early development branch, not a finished product. The upstream mail, calendar, contacts, task, authentication, and synchronization code is retained. Current BlueEvolution-specific changes include:

- Follow system reads the desktop color scheme from `org.freedesktop.portal.Settings`, including on KDE Plasma.
- Light and Dark apply GTK's application-level theme preference immediately.
- The choice is stored in the BlueEvolution-specific `io.github.themetalleg.BlueEvolution` GSettings schema.
- The main window, About dialog, desktop entry, application metadata, and icon use BlueEvolution branding. The About dialog retains upstream copyright and contributor attribution and shows the fork and Evolution versions separately.
- The Flatpak application ID and EDS D-Bus service prefix are BlueEvolution-specific. Its application settings use the keyfile backend, and the upstream DConf migration mapping is not enabled.
- A Flatpak build/export script, build CI workflow, draft release workflow, and scheduled upstream stable-release PR workflow are present.
- HTML email bodies are not restyled.

The Outlook-inspired message list and reading-pane redesign, an in-app GitHub update checker, signed update metadata, and a published release are not implemented. The native unbundled build still uses upstream data-service identity; do not install it over Evolution or connect it to live accounts.

## Upstream Baseline

- Repository: [GNOME Evolution](https://gitlab.gnome.org/GNOME/evolution)
- Stable release: `3.62.1`
- Release commit: `240f9f320afee6601682748f39dca84f007dd0e5`
- Evolution Data Server: `3.62.1` (`ae37a0c718d3f976d1961c610437c4d1f27ba6e3`)
- Evolution EWS: `3.62.1` (`2fdc17db2005eaea388ee51a41539522d3a71714`)
- Toolkit/build system: GTK 3 and CMake
- Required companion libraries include Evolution Data Server at the same release, WebKitGTK 2.40 or newer, and the GNOME development libraries listed in the upstream README.

The local `upstream/evolution-3.62` branch records the release baseline; `upstream/gnome-51` tracks the stable maintenance branch. BlueEvolution changes are kept on `main`. The scheduled workflow attempts a source merge only into a review branch and opens or updates one PR. Conflicts stop the job for manual resolution; upstream changes are never auto-merged.

## Build Safety

Evolution's upstream [build instructions](README.md#dependencies) describe the CMake build and dependencies. The supported packaging target is an x86_64 Flatpak based on GNOME Platform/SDK 49. Its launcher is `blueevolution`; the Flatpak application ID is `io.github.themetalleg.BlueEvolution`.

Install the Flatpak SDK/runtime and tools, then build a bundle:

```sh
sudo apt update
sudo apt install flatpak flatpak-builder jq
flatpak remote-add --user --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
flatpak install --user --assumeyes flathub org.gnome.Sdk//49 org.gnome.Platform//49
packaging/build-flatpak.sh
```

The bundle and checksum are written under `build/flatpak/`. Install a bundle with `flatpak install --user --bundle build/flatpak/BlueEvolution-0.1.0-dev-x86_64.flatpak`; launch it with `flatpak run io.github.themetalleg.BlueEvolution`; remove it with `flatpak uninstall --user io.github.themetalleg.BlueEvolution`. A Flatpak install has its own application data directory and keyfile settings. The upstream Flatpak permissions intentionally continue to share the user's Secret Service/keyring, GPG home, and PKI files for existing authentication and signing integrations. Native non-Flatpak builds are not isolated and are not safe for side-by-side use yet.

The release workflow builds a `.flatpak` bundle and SHA-256 file and creates a draft for tags named `blueevolution-vX.Y.Z`. To enable signing, create a dedicated CI-only GPG signing key in a separate GPG home, make it usable noninteractively, and export its ASCII-armored private key to a protected local file. Add it without displaying the value using:

```sh
gh secret set FLATPAK_GPG_PRIVATE_KEY --repo themetalleg/BlueEvolution < release-signing-key.asc
```

Restrict repository Actions access to trusted maintainers, keep an offline backup, and revoke/rotate the key if exposed. The workflow signs the OSTree commit and embeds the public key in the bundle. Without this secret, drafts are unsigned; the SHA-256 file detects corruption only and is not publisher authentication. Users currently update by installing a newer bundle manually; no in-app GitHub release checker or unattended installation exists.

The build ran in GNOME's Flatpak SDK with temporary `HOME`/XDG paths and in-memory settings. It did not connect to live mail accounts. The SDK's optional AppStream compose extension is not available from the configured Flathub remote, so package generation uses Flatpak's native build-finish/export commands instead of AppStream composition.

The local `Screenshots/` reference directory is ignored by Git because it contains third-party design references and may contain personal mail information. It must not be published without explicit approval.

## Licensing

This project retains Evolution's source history, per-file license headers, contributor attribution, and upstream license files. Review the relevant source headers and `COPYING*` files before redistributing modified binaries or bundled dependencies.

## Not Yet Available

- No stable public release exists yet.
- No `.deb` package exists; the tested packaging route is Flatpak.
- Draft prerelease `blueevolution-v0.1.0-dev` is available in [GitHub Releases](https://github.com/themetalleg/BlueEvolution/releases/tag/untagged-0d68cdcb56ca5df51b08). It contains a real x86_64 Flatpak and a SHA-256 integrity file, but is unsigned and must not be treated as publisher-authenticated.
- GitHub Actions has run the draft workflow; two attempts exposed missing dependency pkg-config paths in the hosted build. Explicit `/app/lib` settings are being added before another run. The upstream PR workflow requires repository Actions settings to allow `GITHUB_TOKEN` to create pull requests.
- No in-app update checker, signed assets, or publisher-authenticated update metadata exists.
- The Outlook-inspired message-list and reading-pane redesign is not implemented.
- No screenshots of the customized interface have been captured yet.