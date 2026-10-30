# BlueEvolution

BlueEvolution — an independent fork of GNOME Evolution with a modern,
Outlook-inspired interface.

This repository is based on GNOME Evolution 3.62.1. BlueEvolution adds
light, dark, and desktop-following appearance settings, Outlook-inspired mail
rows and toolbars, themed icons, and an optional multi-provider Mail Assistant.
The broader UI redesign and distribution work are still in progress.

See [BLUEEVOLUTION.md](BLUEEVOLUTION.md) for the baseline, implementation
status, build notes, and known limitations. Upstream Evolution documentation
follows.

## Releases

Signed stable and beta releases are live on the
[installation page](https://themetalleg.github.io/BlueEvolution/).

| Channel | Install | Application ID |
| --- | --- | --- |
| Stable | [Install stable](https://themetalleg.github.io/BlueEvolution/BlueEvolution.flatpakref) | `io.github.themetalleg.BlueEvolution` |
| Beta | [Install beta](https://themetalleg.github.io/BlueEvolution/BlueEvolution-Beta.flatpakref) | `io.github.themetalleg.BlueEvolution.Beta` |

Both can be installed and run together with separate local profiles. Both use
the signed `blueevolution` Flatpak remote, but stable follows the `stable` branch
and beta follows `beta`; beta never replaces stable. Using the same account in
both applications still affects the same server mailbox.

Updates are delivered through Discover, GNOME Software, or `flatpak update --user`.
Users choose when to install updates. Automatic installation is optional and
controlled through their software manager's settings, not by BlueEvolution.

KDE Discover lists Flatpak applications only when its Flatpak backend is
installed. On Kubuntu, install `plasma-discover-backend-flatpak`; without it,
user-installed BlueEvolution apps do not appear in Discover's Installed or
Updates views. Restart Discover after installing the backend.

### Publishing Updates

1. Commit and push tested changes to `main`.
2. Push a new immutable `blueevolution-vX.Y.Z` tag for stable, or
  `blueevolution-vX.Y.Z-beta.N` for beta. Prerelease versions route to beta.
3. GitHub Actions builds, signs, tests, and creates a draft release.
4. Review and publish the draft. The publication workflow deploys the signed
  Flatpak repository to GitHub Pages while preserving both channels.
5. Verify the deployment, both public install links, and the remote refs before
  announcing the update. Connected installations then receive their channel's
  update through Flatpak; restart a running app to use an installed update.

See [release and update instructions](packaging/README.md) for full procedures,
existing-installation migration, validation commands, signing, and recovery.

## Mail Downloads

In an account's **Receiving Options**, **Automatically download messages** offers
**On demand**, **Unread messages only**, and **All messages**. Unread-only mode
preloads complete messages, including attachments, after folder synchronization
without marking them read. Existing age limits and explicit per-folder offline
settings still apply. External images keep their existing privacy controls.

Flatpak builds include the required Camel extension. Native builds against an
unpatched Evolution Data Server retain the existing synchronization checkbox.

## Mail Assistant

The Mail Assistant is included in `main`. Its composer sidebar starts closed
and can be opened from the toolbar or View menu.

### Setup

1. Open Settings > Integrations > Mail Assistant.
2. Select a provider and, optionally, change and save its model ID.
3. Enter your API key and save it after reviewing the provider-specific privacy notice.
4. Use **Test API Key** to check reply-generation access for the saved model.

| Provider | Default Model |
| --- | --- |
| OpenAI | `gpt-4.1-mini` |
| Google Gemini | `gemini-2.5-flash` |
| Claude (Anthropic) | `claude-haiku-4-5` |

Each provider has its own model, API key, and consent. Keys are stored only
through Secret Service in the desktop keyring, never in application settings,
and their saved values are not displayed. Changing providers never falls back
to another service. Removing a key also removes that provider's recorded consent.

Testing a saved key performs a tiny reply-generation request using a fixed
synthetic prompt, never mail. It uses the same model as the assistant and
may incur a small API charge. Success confirms generation works at that
moment; quota and rate limits can subsequently change.

### Conversation

Opening the assistant sends the current subject and plain-text draft,
including quoted mail, to the selected provider for four contextual reply ideas.
Suggestion labels and instructions follow the application language, while
drafted replies follow the email language unless instructed otherwise. Selecting
an idea drafts a reply and collapses the suggestions; expand them to choose
another. Your messages appear on the right and assistant replies on the left,
with styling that follows the application's light or dark appearance.
Opening the sidebar shares the existing window space without enlarging it.
Controls, tooltips, and status messages are localized, with complete German
translations for the panel and its integration settings.

**Insert at Cursor** inserts plain text through the native editor's undoable
insertion path. It never
automatically replaces the whole message or sends mail. Review every draft.
Messages and error text can be selected and copied.

### Privacy

Attachments are not uploaded and chat history is memory-only. Changing provider
or model clears the previous conversation before another request. OpenAI
Responses API requests use `store: false`; each provider's retention policies
and API charges still apply. Disabling response storage is not a guarantee of
zero provider retention. No mail is sent to a provider until you use the assistant
with a configured key and accepted consent.

### Development

For porting to another fork, the reusable manager lives in
`src/e-util/e-openai-manager.[ch]`; settings registration is isolated under
`src/modules/integrations`, and composer UI under `src/composer/e-composer-openai.[ch]`.
Dependencies are libsecret, libsoup 3, JSON-GLib, and GTK 3. Offline tests use a
loopback mock server and synthetic messages; they never call live providers.

## Icon Auditing

Set `BLUEEVOLUTION_ICON_AUDIT=strict` in a disposable test profile to abort
when an unthemed UI glyph reaches the shared regular-image loader or icon
filename API. For Flatpak, pass `--env=BLUEEVOLUTION_ICON_AUDIT=strict` to
`flatpak run`. Do not enable this in a normal mail session: audit failures
deliberately terminate the process. Photos, thumbnails, and full-color
application icons keep their regular image handling.

## GNOME Evolution

**Evolution** is the integrated mail, calendar and address book suite from
the Evolution Team.

See [the Evolution GNOME Wiki page][gnome-wiki] for more information.

If you are using Evolution, you may wish to subscribe to [the Evolution
users mailing list][mailing-list]. Visit there to subscribe or view archives
of the Evolution mailing list.

There is also a [#gnome-evolution] IRC channel on [Libera.Chat].

Help for Evolution is available in the user manual (select "Help" from
the menu after running the application), at the
[GNOME users help site][gnome-help], and in the --help strings (run
`evolution --help` at the command line).

The rest of this file is dedicated to building Evolution.

[icon]: https://raw.github.com/gnome-design-team/gnome-icons/master/apps/hicolor/48x48/apps/evolution.png "Evolution app icon"
[gnome-wiki]: https://gitlab.gnome.org/GNOME/evolution/-/wikis/home
[mailing-list]: https://lists.osuosl.org/mailman/listinfo/evolution-users
[#gnome-evolution]: ircs://irc.libera.chat:6697/gnome-evolution
[Libera.Chat]: https://libera.chat/
[gnome-help]: https://gnome.pages.gitlab.gnome.org/evolution/help/

DEPENDENCIES
------------

In order to build Evolution you need to have the full set of GNOME 3
(or greater) development libraries installed.

GNOME 3 or greater comes with most of the modern distributions, so
in most cases it should be enough to just install all the devel
packages from your distribution.

Please make sure you have the most recent versions of the libraries
installed, since bugs in the libraries can cause bugs in Evolution.

Additional dependencies, besides the stock GNOME libraries (the
dependencies should be compiled in the order they are listed here):

* [evolution-data-server of the same version as the Evolution is][eds]
* [libsoup 3.0 or later][libsoup]
* [WebKitGTK 2.34.0][webkitgtk]
* [Mozilla NSPR/NSS libraries][mozilla]
  These are needed if you want to compile Evolution with SSL and S/MIME
  support. Many distributions ship these as Mozilla development packages.

Other dependencies are claimed during the configure phase. If these are
optional, also a parameter for the CMake configure to not use that dependency
is shown.

[eds]: https://download.gnome.org/sources/evolution-data-server/
[libsoup]: https://download.gnome.org/sources/libsoup/
[webkitgtk]: https://webkitgtk.org/releases/
[mozilla]: https://www.mozilla.org/

CONFIGURING EVOLUTION
---------------------

First you have to decide whether you want to install Evolution (and
its dependencies) into the same prefix as the rest of your GNOME
install, or into a new prefix.

Installing everything into the same prefix as the rest of your GNOME
install will make it much easier to build and run programs, and easier
to switch between using packages and building it yourself, but it may
also make it harder to uninstall later.  Also, it increases the chance
that something goes wrong and your GNOME installation gets ruined.

If you want to install in a different prefix, you need to do the
following things:

* Set the environment variables to contain a colon-separated list
  of all the directories that will be involved in the build.
  The environment variables are `GSETTINGS_SCHEMA_DIR`,
  `LD_LIBRARY_PATH`, `PATH` and `PKG_CONFIG_PATH`.

  For example, if you have GNOME installed in `/usr` and you
  are installing Evolution and its dependencies in
  `/opt/evolution`, you want to do something like the following
  (assuming you are using Bash):

  ```bash
  export GSETTINGS_SCHEMA_DIR="/opt/evolution/share/glib-2.0/schemas"
  export LD_LIBRARY_PATH=/opt/evolution/lib:$LD_LIBRARY_PATH
  export PATH=/opt/evolution/bin:$PATH
  export PKG_CONFIG_PATH=/opt/evolution/lib/pkgconfig:$PKG_CONFIG_PATH
  ```

* Edit the D-Bus `session-local.conf` file (which is normally
  search for by D-Bus in `/etc/dbus-1/`) to include the
  location where you are installing Evolution.

  In the example given above (GNOME in `/usr`, Evolution and
  dependencies in `/opt/evolution`), your
  `session-local.conf` will have to look like this:

  ```xml
  <!DOCTYPE busconfig PUBLIC
        "-//freedesktop//DTD D-BUS Bus Configuration 1.0//EN"
        "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
  <busconfig>
        <!-- Search for .service files in /usr/local -->
        <servicedir>/opt/evolution/share/dbus-1/services</servicedir>
  </busconfig>
    ```

* Pass an appropriate `CMAKE_INSTALL_PREFIX` parameter to the configure
  scripts of Evolution and its dependencies, eg:

  ```bash
  cd ..../sources/evolution
  mkdir build
  cd build
  cmake -G "Unix Makefiles" \
        -DCMAKE_INSTALL_PREFIX=/opt/evolution \
        -DCMAKE_BUILD_TYPE=Release \
        ..
  ```

* Run `cmake --help` to get list of available generators (the -G argument)
          on your platform.

OPTIONAL FEATURES
-----------------

Some optional features can be enabled at compilation time by passing
appropriate flags to the CMake. These options are shown at the end
of the successful configure phase.

BUILDING EVOLUTION
------------------

After the Evolution is properly configured, run:

  ```bash
  make -j
  make -j install
  ```

to build it.

ONLINE BUILD MANUAL
-------------------

An [online build manual][Build Manual] is also available.

[Build Manual]: https://gitlab.gnome.org/GNOME/evolution/-/wikis/Building
