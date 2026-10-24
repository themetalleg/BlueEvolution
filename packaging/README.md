# Releases and Updates

BlueEvolution uses GitHub Actions for builds, GitHub Releases for downloads,
and GitHub Pages for the signed Flatpak update repository. No separate server
or custom in-app updater is required.

## Channels

| Channel | Versions | Application ID | Flatpak branch |
| --- | --- | --- | --- |
| Stable | `0.2.0`, `0.2.1` | `io.github.themetalleg.BlueEvolution` | `stable` |
| Beta | `0.3.0-beta.1`, `0.3.0-rc.1` | `io.github.themetalleg.BlueEvolution.Beta` | `beta` |

Stable and beta have independent desktop entries, running application IDs,
EDS D-Bus services, and sandbox configuration/data directories. Their launcher
names are BlueEvolution and BlueEvolution Beta. Both can be installed and run
at the same time; beta never replaces stable. Mail accounts must be configured
separately. Local profiles are not copied or migrated automatically.

Connecting both applications to the same real account still affects the same
server mailbox. Read/delete/move actions and outgoing messages are real in
either channel. The desktop keyring and explicitly shared files are not
separate testing environments. Prefer a test account for beta evaluation.

## Installing and Updating

After the first approved release has deployed, use:

- Stable: <https://themetalleg.github.io/BlueEvolution/BlueEvolution.flatpakref>
- Beta: <https://themetalleg.github.io/BlueEvolution/BlueEvolution-Beta.flatpakref>

A channel's link is created only when that channel has a published build.
Both links register the same signed `blueevolution` remote but select different
application IDs and branches. Subsequent updates are delivered through Discover,
GNOME Software, or `flatpak update --user`. Enable automatic Flatpak updates in
the software manager to install them automatically. BlueEvolution does not
override desktop update policy or restart a running mail session.

Release bundles also embed the repository origin and public signing key.
An older development bundle without an update origin needs a one-time install
from the appropriate `.flatpakref`; do not uninstall with `--delete-data`.
For an already-installed development bundle, explicitly reinstall from the
public remote once its channel is live, keeping its existing profile:

```sh
flatpak remote-add --user --if-not-exists --from blueevolution https://themetalleg.github.io/BlueEvolution/BlueEvolution.flatpakrepo
flatpak install --user --reinstall blueevolution app/io.github.themetalleg.BlueEvolution/x86_64/stable
flatpak install --user --reinstall blueevolution app/io.github.themetalleg.BlueEvolution.Beta/x86_64/beta
```

Use explicit remote/ref reinstalls for existing installations; the `.flatpakref`
shortcut can report that an app is already installed instead of changing its
origin. Verify the result with `flatpak info --user --show-origin APP_ID`.

The historical `io.github.themetalleg.BlueEvolution` identity belongs to stable,
even if a locally installed development version was labelled beta.

## GitHub Setup

1. Set Pages to GitHub Actions as its build/deployment source.
2. Configure the `FLATPAK_GPG_PRIVATE_KEY` repository secret with an armored,
   unencrypted CI signing key. Never commit or print the private key. Keep a
   secure backup of the key and its revocation certificate outside GitHub.
3. Allow the `github-pages` environment to deploy from `main` and tags matching
   `blueevolution-v*`. Optional required reviewers can gate deployments.
4. Keep workflow permissions limited to those declared in the workflows.

The existing repository signing secret is reused. Public releases fail if the
key is missing; unsigned builds are not accepted by the publisher. A signing-key
change is deliberately blocked once publication state exists. Key rotation
requires a planned trust migration for installed clients, not replacing the
secret and hoping existing installations accept a different key.

## Creating a Release

1. Merge and push the tested source and release workflows to `main`.
2. Push an immutable version tag, for example `blueevolution-v0.3.0-beta.1`
   for beta or `blueevolution-v0.2.0` for stable. Alternatively, run Build draft
   release manually with the desired version.
3. Review the resulting draft, test the signed bundle and inspect its release
   notes. Stable and beta routing is derived from the semantic version, not
   from an editable GitHub label. Any prerelease suffix routes to beta.
4. Publish the draft using the GitHub UI. This user action triggers Publish
   Flatpak updates. Its release prerelease flag must match the version.
5. Wait for successful Pages deployment and state recording before announcing
   the update. Both channel install links remain available once published.

Pages caches can briefly serve the preceding deployment after a job succeeds.
Allow up to ten minutes for propagation and verify both installer links and
`flatpak remote-info --user blueevolution app/APP_ID/x86_64/CHANNEL` before
announcing the release. Never disable GPG verification to work around caches.

Draft builds can be explicitly replaced with `replace_draft`. Published
versioned build assets are not rebuilt or replaced by the release workflow.
Do not publish older drafts created before this repository-publishing workflow;
replace/rebuild those drafts first so their signed repository assets exist.
An existing version tag must point to the commit being built. A draft whose
tag points to older source cannot silently distribute a build from newer main;
prefer a fresh version tag, or deliberately correct that unpublished tag first.

The publisher consumes the tested repository asset rather than rebuilding the
application. It verifies checksums and commit signatures, rejects downgrades
within each channel, preserves the other channel, and signs the repository
summary. Two previous application commits per channel are retained as rollback
history; these history refs are not installable app refs.

## Publication State and Recovery

Publication jobs are serialized, and a running job is not cancelled by a newer
publication. GitHub can replace a pending run if several releases are published
in quick succession. Publish one release at a time and verify its deployment;
rerun the publisher manually for any channel whose queued run was replaced.
The `flatpak-state` branch contains a small JSON state file, updated only after
Pages deployment succeeds. It records channel versions, the trusted signing
fingerprint, and the checksum/location of the repository snapshot.

Each publication prepares a distinct state commit and uses its SHA as the Pages
build identity. This matters when stable and beta are built from the same source
commit: reusing that source SHA can leave Pages serving the preceding artifact.
Preparing the commit does not advance the state branch; that still happens only
after deployment succeeds.

The merged snapshot is stored with a unique `BlueEvolution-updates-RUN-ATTEMPT.tar.gz`
name on the most recent deployed GitHub release. Its exact name is recorded in
the state branch. Failed publications cannot overwrite the active checkpoint.
It preserves both channels and rollback
history. Do not delete or modify that asset or the `flatpak-state` branch;
they are required for the next release. Versioned source/bundle assets remain
unchanged. Pages hosts only the resulting static repository, installer links,
and public key, not the private signing key or source worktree.

For a failed publication, rerun Publish Flatpak updates manually with the same
release tag. Already-recorded releases are no-ops. If Pages deployed but state
recording failed, retry that tag before publishing another release. Never
manually rewind the state branch or change a previously published release's
version/asset contents to perform a rollback; publish a higher-version repair.

For hosting recovery after state was already recorded, run the publisher from
`main`, select the latest snapshot's release tag, and set `redeploy` to true.
This reconstructs both channel refs from their immutable signed release assets
and republishes them with a fresh deployment identity; it does not rebuild or
replace the versioned release binaries.

The publisher stops before deploying a site of 950 MB or more. GitHub Pages has
size/bandwidth limits; monitor repository growth and move to Flathub or another
Flatpak host if these become restrictive. Such a migration needs to preserve
the installed clients' signing trust and update origin.

## Local Validation

```sh
npm --prefix packaging ci --ignore-scripts
npm --prefix packaging test
BLUEEVOLUTION_TEST_FLATPAK=1 npm --prefix packaging test
```

The release tests need Node.js, `jq`, `xsltproc`, and `xmllint`.
The integration test additionally needs `flatpak`, `ostree`, and `gpg`. It uses synthetic
applications, a temporary signing key, and an isolated Flatpak installation.
It verifies co-installation, independent signed updates and unsigned rejection;
it never reads mail or changes the normal Flatpak installation.

Use `BLUEEVOLUTION_CHANNEL=stable` or `beta` with `packaging/build-flatpak.sh`.
The default follows `BLUEEVOLUTION_VERSION`; stable rejects prerelease versions.
`--prepare-only` produces the channel manifest without compiling. Keep build
and Flatpak-builder state directories on the same filesystem. For local bundles
that should follow public updates, also set
`FLATPAK_REPO_URL=https://themetalleg.github.io/BlueEvolution/repo` and sign with
the release key. Unsigned local bundles are for development, not distribution.