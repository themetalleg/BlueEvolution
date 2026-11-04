# BlueEvolution Release Process

This document describes the complete release process for BlueEvolution, ensuring smooth and consistent releases without interruptions.

## Key Principles

1. **Every release must have a new version number** - Version numbers are critical for Flatpak to recognize updates
2. **Semantic versioning** - MAJOR.MINOR.PATCH (e.g., 0.2.12)
3. **Single source of truth** - Version defined in one place
4. **Automated workflows** - Minimal manual steps
5. **Clear status tracking** - Know exactly what's happening at each step

## Version Management

### Current Version Location
Edit the version in:
- **File**: `meson.build` (or equivalent project configuration)
- **Format**: `version: '0.x.y'`

### Incrementing Version
- **Patch release** (bug fixes): `0.2.11` → `0.2.12`
- **Minor release** (features): `0.2.y` → `0.3.0`
- **Major release** (breaking changes): `0.x.y` → `1.0.0`

## Pre-Release Checklist

Before starting a release:

- [ ] All commits merged to main
- [ ] All tests passing
- [ ] Git history clean (no work-in-progress commits)
- [ ] Version number incremented in source code
- [ ] CHANGELOG.md updated with release notes
- [ ] Release notes prepared (see template below)

### Example CHANGELOG Entry

```markdown
## [0.2.12] - 2026-11-04

### Fixed
- SEGV crash on startup due to memory safety issue in UID filtering
- Double-free error in GPtrArray handling

### Security
- Fixed potential memory corruption in message list regeneration

### Previous Release
- See 0.2.11 for unread message filter fixes
```

## Release Process (Step-by-Step)

### 1. Create Git Tag with Version

```bash
# Update version in meson.build
# Edit CHANGELOG.md
# Commit the changes
git add meson.build CHANGELOG.md
git commit -m "Prepare v0.x.y release

Release notes:
- Feature 1
- Fix 1
- Security improvement 1"

# Create annotated tag (REQUIRED - not lightweight)
git tag -a blueevolution-v0.x.y -m "BlueEvolution 0.x.y - Release notes here"

# Push tag to trigger build
git push origin main
git push origin blueevolution-v0.x.y
```

### 2. Build Process (Automatic)

**Triggered by**: Git tag push starting with `blueevolution-v`

**Workflow**: `.github/workflows/release.yml`
- Compiles Flatpak bundle
- Creates release assets
- Generates checksums
- Creates draft release on GitHub

**Status**: Check with `gh run list -L 1 --workflow=release.yml`

**Wait for**: Build completes (typically 15-20 minutes)

```bash
# Monitor build
gh run watch <RUN_ID> --interval 10
# or check status
gh run view <RUN_ID> --json status,conclusion
```

### 3. Verify Release Assets

```bash
# Check release created
gh release view blueevolution-v0.x.y

# Verify all assets present:
# - BlueEvolution-0.x.y-x86_64.flatpak
# - BlueEvolution-0.x.y-x86_64.flatpak.sha256
# - BlueEvolution-0.x.y-repository.tar.gz
# - BlueEvolution-0.x.y-repository.tar.gz.sha256
# - release.json
```

### 4. Publish Release

```bash
# Publish the draft release (triggers publish workflow)
gh release edit blueevolution-v0.x.y --draft=false
```

**Workflow**: `.github/workflows/publish.yml`
- Creates signed Flatpak repository
- Generates update archives
- Updates flatpak-state branch
- Deploys to GitHub Pages

**Status**: Check with `gh run view <RUN_ID> --json status,conclusion`

**Wait for**: Publish completes (typically 5-10 minutes)

### 5. Verify Publication

```bash
# Check release is published (not draft)
gh release view blueevolution-v0.x.y --json isDraft,isPrerelease

# Should show: false false (published, not pre-release)

# Check Flatpak repository updated
curl https://themetalleg.github.io/BlueEvolution/index.html

# Should show: BlueEvolution (0.x.y)

# Check flatpak-state branch updated
git show origin/flatpak-state:state.json | jq '.latest_tag'
# Should show: "blueevolution-v0.x.y"
```

### 6. Announce Release

```bash
# Post to relevant channels
# - GitHub Releases page
# - User documentation
# - Flatpak catalog (auto-indexed)
```

## Troubleshooting

### Build Failed

```bash
# Check full logs
gh run view <RUN_ID> --log | head -500

# Fix issue, increment patch version, retry
git tag -d blueevolution-v0.x.y
git push origin :blueevolution-v0.x.y
# Fix code, increment version
git tag -a blueevolution-v0.x.(y+1) -m "..."
git push origin blueevolution-v0.x.(y+1)
```

### Publish Failed - Already Published

```bash
# If release is marked "already published" but Pages not updated:
gh workflow run publish.yml -f tag=blueevolution-v0.x.y -f redeploy=true

# This redeploys without rebuilding
```

### Pages Not Updated

```bash
# Wait 1-2 hours for DNS/CDN propagation
# Or force clear cache:
gh workflow run publish.yml -f tag=blueevolution-v0.x.y -f redeploy=true
```

## Automation Notes

### What's Automated

- ✅ Flatpak build compilation
- ✅ Asset generation and checksumming
- ✅ Repository signing (GPG)
- ✅ Flatpak state tracking
- ✅ GitHub Pages deployment
- ✅ Release notes generation

### What Requires Manual Steps

- ⚠️ Version number increment (source file)
- ⚠️ Changelog update
- ⚠️ Release notes writing
- ⚠️ Tag creation and push
- ⚠️ Publishing draft release (one `gh` command)
- ⚠️ Verification steps

## Future Improvements

Potential automations to add:

1. **Auto-version-bump**: Script to increment version in meson.build
2. **Release template**: Prepare CHANGELOG entry from commits
3. **One-command release**: Single script that:
   - Bumps version
   - Updates CHANGELOG
   - Creates tag
   - Pushes tag
   - Publishes release
   - Waits for completion
   - Verifies publication
4. **Pre-flight checks**: Validate all conditions before starting

## Quick Reference

### Release a Patch Fix (0.2.11 → 0.2.12)

```bash
# 1. Update meson.build version to 0.2.12
# 2. Update CHANGELOG.md
# 3. Commit
git commit -am "Prepare v0.2.12 release - crash fix"

# 4. Tag
git tag -a blueevolution-v0.2.12 -m "BlueEvolution 0.2.12

Fixed:
- SEGV crash on startup
- Memory safety issue in UID filtering"

# 5. Push
git push origin main blueevolution-v0.2.12

# 6. Wait for build (~15 min)
gh run watch $(gh run list -L 1 --workflow=release.yml -q '.[0].databaseId')

# 7. Publish
gh release edit blueevolution-v0.2.12 --draft=false

# 8. Wait for publish (~5 min)
gh run watch $(gh run list -L 1 --workflow=publish.yml -q '.[0].databaseId')

# 9. Verify
curl https://themetalleg.github.io/BlueEvolution/index.html | grep "0.2.12"
```

## Release History Template

Keep this in RELEASES.md or project wiki:

```markdown
# Release History

## v0.2.12 - 2026-11-04
- Status: Published ✅
- Build time: 15 minutes
- Publish time: 5 minutes
- Issues: None
- Notes: Emergency fix for SEGV crash

## v0.2.11 - 2026-11-03
- Status: Deleted (contained bugs)
- Reason: Caused SEGV on startup

## v0.2.10 - 2026-11-02
- Status: Published ✅
```

---

**Last Updated**: 2026-11-04  
**Release Process Version**: 1.0  
**Status**: Ready for use
