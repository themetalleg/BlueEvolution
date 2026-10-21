# 0.2.0-beta.1

This is an early, unsigned beta for evaluation, not a production-ready replacement
for GNOME Evolution. Back up existing mail data and use a separate test account.

## Changes

- Outlook-inspired three-pane mail layout, blue search header and command strip.
- Bootstrap Icons 1.13.1 throughout the application, with its MIT license included.
- Sender initials, quieter previews, row separators and local-calendar date sections.
- Measured date widths that remain visible as the message-list pane narrows.
- App-specific System, Light and Dark modes, including standalone message viewers.
- Theme-safe reader backgrounds, disclosure chevrons and window-control icons.
- Offline VS Code previews with a private profile and five synthetic emails.
- Permanent regression coverage for date allocation, calendar/DST boundaries,
  toolbar labels, UI definitions, theme precedence and Bootstrap icon resolution.

## Known Limitations

- No in-app updater or published Flatpak update repository; updates are manual.
- The beta is unsigned unless the release workflow is supplied a signing key.
  SHA-256 checksums verify integrity, not publisher identity.
- Authentication, sending/receiving, attachments and server synchronization have
  not been re-certified against live accounts for this beta. CI uses no accounts.
- Native non-Flatpak installation is not supported for side-by-side use.
- The mail-list redesign remains experimental, especially with large/threaded
  folders and unusual message content. Light/dark HTML rendering needs broader
  real-world testing.
- GTK window decorations and desktop integration can vary between compositors.

The upstream baseline remains Evolution, Evolution Data Server and Evolution EWS
3.62.1. Upstream credits and license files are preserved. Bootstrap's license is
installed with the private icon theme. This project is not affiliated with Microsoft
or GNOME.