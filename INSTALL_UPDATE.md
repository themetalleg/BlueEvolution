# How to Install or Update BlueEvolution

BlueEvolution is distributed as a custom Flatpak repository. It's not in the official Flathub catalog, so updates don't appear automatically in Discover.

## Quick Update (Recommended)

If you already have BlueEvolution installed:

```bash
# Option 1: Refresh and update from Flatpak
flatpak update io.github.themetalleg.BlueEvolution

# Option 2: Manually add repository and update
flatpak remote-add --if-not-exists blueevolution \
  https://themetalleg.github.io/BlueEvolution/repo
flatpak update io.github.themetalleg.BlueEvolution
```

## Fresh Installation

If you don't have BlueEvolution yet:

### Method 1: Using .flatpakref File (Easiest)
```bash
flatpak install https://themetalleg.github.io/BlueEvolution/dist/BlueEvolution.flatpakref
```

### Method 2: Command Line
```bash
flatpak remote-add --if-not-exists blueevolution \
  https://themetalleg.github.io/BlueEvolution/repo
flatpak install blueevolution io.github.themetalleg.BlueEvolution
```

### Method 3: Discover (if properly configured)
```bash
# Add repository to Discover
flatpak remote-add --if-not-exists blueevolution \
  https://themetalleg.github.io/BlueEvolution/repo

# Then open Discover and search for BlueEvolution
```

## Latest Version

**Version 0.2.13** (Released 2026-11-04)
- **Fixed**: SEGV crash when filtering unread messages
- **Fixed**: Memory management bug in UID filtering

**Installation**: Follow any of the methods above

## Troubleshooting

### I installed it but Discover doesn't show updates
BlueEvolution uses a custom repository, not Flathub. Updates might not appear in Discover's default view.
- Solution: Use `flatpak update io.github.themetalleg.BlueEvolution` command instead

### Discover shows a different version than expected
Discover may be pulling from Flathub if BlueEvolution is listed there.
- Solution: Make sure your custom repository is configured:
  ```bash
  flatpak remotes | grep blueevolution
  ```

### The installation fails with signature errors
The repository is signed with a specific key. Make sure you have internet access and try again.
- Solution: `flatpak remote-modify blueevolution --disable --enable`

## Manual Repository Configuration

If you want to manage updates manually:

```bash
# Add the repository
flatpak remote-add --if-not-exists blueevolution \
  https://themetalleg.github.io/BlueEvolution/repo

# List all remotes
flatpak remotes

# Update from all remotes
flatpak update

# Remove the repository (if needed)
flatpak remote-delete blueevolution
```

## Release Notes

See [GitHub Releases](https://github.com/themetalleg/BlueEvolution/releases) for detailed changelog.

Current release: **0.2.13** - SEGV crash fix
