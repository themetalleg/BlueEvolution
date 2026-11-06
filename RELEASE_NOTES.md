# BlueEvolution 0.2.16 Release Notes

This stable release promotes the inline-image fix tested in 0.2.16-beta.1.
It restores embedded screenshots missing from HTML mail after WebKit runtime
updates.

## Changes

- Embedded `cid:` images use a WebKit-compatible local display URL, so screenshots
  inside HTML messages can load without enabling remote images.
- Copying HTML or replying to selected content restores ordinary CID references;
  internal display URLs do not become outgoing image references.
- External-image loading policy and tracking protections remain unchanged.
- Includes the settings-preservation fix introduced in stable 0.2.15, without
  migrating profiles or overwriting existing account settings.
- Adds installed CID and WebView regressions to both CI and release gates,
  using synthetic content without accounts or network access.
- Repairs Discover catalogue generation and verifies both channels' application
  IDs, Flatpak refs, and release versions before publication.

## Updating

Update the stable application through your software manager or run:

```sh
flatpak update --user io.github.themetalleg.BlueEvolution
```

Restart BlueEvolution normally, open an affected message in HTML view, and
check that its embedded screenshot is visible with remote images still blocked.
Also check copying selected HTML and replying to a selection containing an image.

Existing stable profiles are retained; no reinstall or account migration is
required. Beta remains at 0.2.16-beta.1 with its independent profile. Do not copy
one channel's profile over the other.

This project is not affiliated with Microsoft or GNOME.