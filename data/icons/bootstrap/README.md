# Bootstrap Icons

BlueEvolution uses an application-private GTK icon theme built from Bootstrap Icons 1.13.1.
The SVG files are unmodified upstream assets; the MIT license is included in LICENSE.
Existing Evolution and GTK icon names are mapped in aliases.json so mail, calendars,
contacts, tasks, composer tools, dialogs, folders, and status indicators use the same family.
The application logo, third-party service branding, emoji content, sender photos, and
images inside messages are not interface controls and retain their original artwork.
The desktop icon theme is not changed.

To update the vendored assets, obtain the pinned npm package and run:

```sh
node scripts/import-bootstrap-icons.mjs /path/to/bootstrap-icons/package
node scripts/import-bootstrap-icons.mjs --check
```

The import generates aliases.cmake for CMake installations. Builds use only the vendored
files and do not download icons or require Node.js. Validation requires xmllint.