# BlueEvolution

BlueEvolution is an independent fork of [GNOME Evolution](https://gitlab.gnome.org/GNOME/evolution) with the goal of a modern, Outlook-inspired Linux interface. It is not affiliated with Microsoft or GNOME.

Public repository: [themetalleg/BlueEvolution](https://github.com/themetalleg/BlueEvolution). The next candidate is `0.2.0-beta.1`; it remains a prerelease, not a stable release. See [RELEASE_NOTES.md](RELEASE_NOTES.md) for changes and known limitations.

## Current Status

This is an early development branch, not a finished product. The upstream mail, calendar, contacts, task, authentication, and synchronization code is retained. Current BlueEvolution-specific changes include:

- Follow system reads the desktop color scheme from `org.freedesktop.portal.Settings`, including on KDE Plasma.
- Light and Dark apply GTK's application-level theme preference immediately.
- The choice is stored in the BlueEvolution-specific `io.github.themetalleg.BlueEvolution` GSettings schema.
- The main window, About dialog, desktop entry, application metadata, and icon use BlueEvolution branding. The About dialog retains upstream copyright and contributor attribution and shows the fork and Evolution versions separately.
- The Flatpak application ID and EDS D-Bus service prefix are BlueEvolution-specific. Its application settings use the keyfile backend, and the upstream DConf migration mapping is not enabled.
- A Flatpak build/export script, build CI workflow, draft release workflow, and scheduled upstream stable-release PR workflow are present.
- An Outlook-inspired three-pane layout, blue search header, command strip and icon navigation are implemented.
- The message list includes sender initials, date sections, measured date widths and muted previews. It remains experimental with large and threaded folders.
- Bootstrap Icons 1.13.1 are vendored with their MIT license and mapped through an application-private theme; the desktop icon theme, application branding, photos and message images are preserved.
- Reader backgrounds and standalone viewers follow the app's selected appearance. HTML rendering has changed and still needs broader real-world testing.
- Account-free CMake regression targets cover date widths, calendar/DST boundaries, UI definitions, toolbar labels, theme precedence and Bootstrap icon lookup. CI and release builds execute these checks in an isolated profile.

An in-app GitHub update checker, signed update metadata and a stable release are not implemented. Live-account workflows have not been re-certified for this beta. The native unbundled build still uses upstream data-service identity; do not install it over Evolution or connect it to live accounts.

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

The bundle and checksum are written under `build/flatpak/`. Install a bundle with `flatpak install --user --bundle build/flatpak/BlueEvolution-0.2.0-beta.1-x86_64.flatpak`; launch it with `flatpak run io.github.themetalleg.BlueEvolution`; remove it with `flatpak uninstall --user io.github.themetalleg.BlueEvolution`. A Flatpak install has its own application data directory and keyfile settings. The upstream Flatpak permissions intentionally continue to share the user's Secret Service/keyring, GPG home, and PKI files for existing authentication and signing integrations. Native non-Flatpak builds are not isolated and are not safe for side-by-side use yet.

In VS Code, run **BlueEvolution: Preview Light** or **BlueEvolution: Preview Dark** to build and launch the local source with a private profile and network access disabled. Mail from `examples/email-samples/` is imported into the local **Example E-Mails** folder and copied to Inbox for rendering review. No generated messages are added; an empty sample directory leaves Inbox empty. Regular app launches never import these local samples. Run `BlueEvolution: Build and install dev Flatpak` only when you intentionally want to install the development bundle.

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
- Latest draft prerelease `blueevolution-v0.1.2-dev` is available in [GitHub Releases](https://github.com/themetalleg/BlueEvolution/releases/tag/untagged-d4a5a0f3f331140495a5). It contains a real x86_64 Flatpak and a SHA-256 integrity file; the downloaded bundle passed checksum verification. The assets are unsigned because no signing secret is configured.
- Four earlier hosted builds exposed library-path mismatches for geocode-glib, GNOME Online Accounts, libgweather, and cmark. Those dependencies are now pinned to `/app/lib`; the 0.1.2 draft build completed successfully, and CI passed the bundle build plus isolated launcher smoke test on `main`. The upstream PR workflow requires repository Actions settings to allow `GITHUB_TOKEN` to create pull requests.
- No in-app update checker or publisher-authenticated update metadata exists. Optional Flatpak commit signing is implemented but not configured.
- The redesigned message list and reader are implemented but not production-certified.
- Customized Light/Dark, Calendar, Contacts and standalone-reader screenshots have been captured using only synthetic data. Review images remain in ignored `build/ui-review/`; the private/reference `Screenshots/` directory is never included in releases.
- The current beta must pass CI on its committed source before a new draft is created. Existing `0.1.2-dev` assets do not contain the new interface.