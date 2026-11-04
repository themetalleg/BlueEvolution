# Release Test Plan for BlueEvolution

**Purpose**: Comprehensive automated and manual testing to ensure releases work end-to-end without failures

## Automated Pre-Release Tests (to add as CI job)

### 1. Version Validation
- [ ] Version in CMakeLists.txt exists and is semantic (X.Y.Z)
- [ ] Version is higher than all existing tags
- [ ] Version not already released (no duplicate versions)
- [ ] Prerelease versions end with `-beta.N`, `-rc.N`, or `-alpha.N`

### 2. Git State Validation
- [ ] All commits on main are signed (if signature policy enforced)
- [ ] No uncommitted changes
- [ ] Main branch is ahead of last release commit
- [ ] CHANGELOG.md has entry for this version

### 3. Build Validation
- [ ] Flatpak build succeeds
- [ ] release.json is valid JSON
- [ ] SHA256 checksums are correct
- [ ] All required assets present

## Automated Post-Build Tests

### 1. Release Creation Validation
- [ ] Release exists with correct tag
- [ ] isDraft = false (unless pre-release)
- [ ] isPrerelease matches version format
- [ ] Assets uploaded (flatpak, tar.gz, checksums)
- [ ] release.json contains correct version

### 2. Publication Workflow Tests
- [ ] Publish workflow triggered automatically
- [ ] Previous version state.json is loaded
- [ ] New state.json created with v0.2.13
- [ ] Repository built and signed
- [ ] Pages deployment successful

### 3. Discover Visibility Tests
- [ ] Wait up to 4 hours for Discover indexing
- [ ] Query: `flatpak remote-ls --app | grep BlueEvolution`
- [ ] Verify version number shown
- [ ] Verify "Stable" channel selected
- [ ] Download size shown correctly

## Manual Verification Steps

### Step 1: Release Published (Day 1)
```bash
# Check GitHub
gh release view blueevolution-v0.2.13
# Should show:
# - isDraft: false
# - isPrerelease: false
# - assets: BlueEvolution-0.2.13-x86_64.flatpak (66 MB)
# - assets: release.json

# Check GitHub Pages deployment
curl -s https://themetalleg.github.io/BlueEvolution/release.json | jq .
# Should show: "version": "0.2.13"
```

### Step 2: Publish Workflow Completed (15-30 min after release)
```bash
gh run list --workflow=publish.yml --limit=1
# Should show: completed + success

# Check flatpak-state branch
git show origin/flatpak-state:state.json | jq '.latest_tag'
# Should show: "blueevolution-v0.2.13"
```

### Step 3: Discover Update Available (2-4 hours after publication)
```bash
# On Linux with Flatpak installed:
flatpak remote-ls flathub io.github.themetalleg.BlueEvolution
# Should show v0.2.13

flatpak install --assumeyes flathub io.github.themetalleg.BlueEvolution//0.2.13
# Should download and install without issues

# Open application
/app/bin/evolution.bin &
# Should start without SEGV
# Help > About should show version 0.2.13
```

### Step 4: Users Can Update (Day 2+)
```bash
# Discover should show "Update available" for previous users
# Click Update → should download and install v0.2.13
# Application starts, no SEGV crash
# Version displays correctly in Help menu
```

## Known Issues & Recovery

### Issue: Release created but stuck in Draft
**Symptom**: `isDraft: true` but should be false
**Cause**: release.yml still using `--draft` flag (OLD CODE)
**Fix**: Already fixed in commit d3daf9ad67
**Recovery**: Manually publish: `gh release edit blueevolution-v0.2.13 --draft=false`

### Issue: Publish workflow fails downloading previous release
**Symptom**: "release not found" error trying to download v0.2.12
**Cause**: Previous release was never published (stayed Draft)
**Fix**: Already fixed in packaging/publish-flatpak.mjs (error handling)
**Recovery**: Manually update flatpak-state.json to skip delta

### Issue: Version doesn't show in Discover
**Symptom**: Discover entry shows no version number
**Cause**: TBD - possibly missing metadata or Discover bug
**Fix**: Pending investigation
**Recovery**: Clear Discover cache: `flatpak remote-modify flathub --disable --enable`

### Issue: Discover doesn't find the update
**Symptom**: No update available in Discover after 4+ hours
**Cause**: Version not properly published to GitHub Pages
**Fix**: Check publish workflow logs, verify state.json on Pages
**Recovery**: Manually re-run publish workflow or fix state.json

## v0.2.13 Current Status

| Check | Status | Notes |
|-------|--------|-------|
| Release created | ✅ YES | blueevolution-v0.2.13 |
| isDraft | ✅ NO | Successfully published |
| Build succeeded | ✅ YES | Flatpak built successfully |
| Assets uploaded | ✅ YES | flatpak, tar.gz, checksums present |
| Publish workflow | ⚠️ FAILED | Used old code (before error handling), tried to download missing v0.2.12 |
| Pages deployed | ❌ UNKNOWN | Publish workflow failed before deployment |
| Flatpak-state updated | ✅ YES | Manually updated to v0.2.13 |
| Discover visibility | ⏳ PENDING | Waiting for indexing (may take 2-4 hours) |

## Next Release Checklist (v0.2.14+)

1. **Before pushing tag**:
   - [ ] Bump version in CMakeLists.txt
   - [ ] Update CHANGELOG.md
   - [ ] Commit with message "Bump version to X.Y.Z"
   - [ ] Run pre-release tests: `npm --prefix packaging run test-release`

2. **After pushing tag**:
   - [ ] Monitor build: `gh run list --workflow=release.yml --limit=1`
   - [ ] Wait for build to complete (typically 60-90 minutes)
   - [ ] Verify release created: `gh release view blueevolution-vX.Y.Z`
   - [ ] Check isDraft=false (should auto-publish with new code)

3. **After publish workflow completes**:
   - [ ] Check status: `gh run list --workflow=publish.yml --limit=1`
   - [ ] Verify pages deployed: `curl https://themetalleg.github.io/BlueEvolution/release.json | jq .version`
   - [ ] Wait 2-4 hours for Discover indexing

4. **After Discover updates**:
   - [ ] Test on real system: `flatpak update --appstream`
   - [ ] Verify version shown in Discover
   - [ ] Install and test: Application runs without crashes
   - [ ] Verify Help > About shows correct version

## Automation for Future Releases

Once release hardening is fully implemented, these should be automated:

1. **Pre-release CI job** - Run version/build validation tests
2. **Publication CI job** - Automatically publish and verify
3. **Discover verification job** - Poll for version availability
4. **Notification job** - Alert when release is live in Discover

Target: Fully automated releases with zero manual intervention by v0.3.0
