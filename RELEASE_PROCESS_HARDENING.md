# BlueEvolution Release Process: Critical Hardening Required

**Status**: URGENT - Multiple systemic failures identified across all release attempts

## Critical Issues Found

### Issue 1: Draft Releases Never Auto-Publish
**Symptom**: v0.2.12 build succeeded, created Draft release, but was never published
**Root Cause**: `release.yml` creates Draft (`--draft` flag), workflow ends without publishing
**Impact**: Users never see the release in Discover; stuck waiting for manual `gh release edit --draft=false`
**Affected Versions**: v0.2.12, all prior releases

### Issue 2: Publish Workflow Fails on Version Mismatch
**Symptom**: v0.2.13 publish fails trying to download v0.2.12 artifacts (which don't exist as published)
**Root Cause**: 
- flatpak-state.json says "latest_tag: blueevolution-v0.2.12"
- But v0.2.12 was never actually published (stayed Draft)
- Publish script tries to build delta from non-existent v0.2.12 artifacts → fails
**Impact**: New release is blocked; users can't get updates
**Affected Version**: v0.2.13

### Issue 3: No Publication Status Verification
**Symptom**: We don't know if a release was successfully published
**Root Cause**: No post-release checklist or automated verification
**Impact**: We manually publish, assume it worked, don't catch failures
**Affected All Releases**: Every single release depends on luck

### Issue 4: Discover Can't See Updates
**Symptom**: Flatpak Discover shows no v0.2.13 available
**Root Cause**: Discover only sees published releases marked "Latest"
**Impact**: Users stuck on old crashing versions; can't update
**Affected Versions**: v0.2.12 (CRITICAL), v0.2.13 (current blocker)

### Issue 5: Version Display Missing in Discover
**Symptom**: Discover entry shows no version number despite release.json existing
**Root Cause**: TBD - could be missing metadata or Discover UI bug
**Impact**: Users can't see which version is available
**Affected All Releases**

## Release Process: Current vs. Required

### CURRENT (BROKEN) FLOW
```
1. Bump version in CMakeLists.txt
2. Create git tag: git tag blueevolution-v0.2.13
3. Push tag → release.yml triggered automatically
   - Builds Flatpak
   - Creates Release as DRAFT
   - STOPS (no publication)
4. [MANUAL STEP REQUIRED] gh release edit --draft=false
5. publish.yml triggered
   - Downloads previous release artifacts
   - Fails if previous release doesn't exist as published
6. [REQUIRES DEBUGGING] Check logs, retry manually
7. Discover eventually updates (2-4 hours later)
8. [VERIFICATION MISSING] No automated check for success
```

### REQUIRED (AUTOMATED) FLOW
```
1. Bump version in CMakeLists.txt (pre-commit check)
   - Verify version > all existing tags
   - Verify version matches semantic versioning
2. Create git tag: git tag blueevolution-v0.2.13
3. Push tag → release.yml triggered
   - Build Flatpak (same as today)
   - Create Release as PUBLISHED (remove --draft flag)
   - Mark as "Latest" if not pre-release
   - Output publication status
4. publish.yml triggered automatically (on [published] event)
   - Verify previous version exists and is published
   - Skip if already published (REDEPLOY_RELEASE check)
   - Build publication artifacts
   - Deploy to GitHub Pages
5. verify-publication.yml triggered
   - Check release is marked "Latest" (not Draft)
   - Check version in release.json is correct
   - Check Flatpak metadata is complete
   - Check pages deployment succeeded
   - Wait for Discover indexing
   - Validate version appears in Discover within timeout
6. create-release-summary.yml
   - Generate automatic release notes from commits
   - Cross-reference PRs and issues
   - Build version report
```

## Fixes Required (Priority Order)

### P0: IMMEDIATE FIX - Unblock v0.2.13
**Action**: Manually recover v0.2.13 publication
```bash
# 1. Manually update flatpak-state to v0.2.13
git fetch origin flatpak-state:flatpak-state
git checkout flatpak-state
# Edit state.json: change latest_tag to blueevolution-v0.2.13
git commit -am "Fix: point to v0.2.13 after publication"
git push origin flatpak-state

# 2. Trigger publish workflow manually for v0.2.13
gh workflow run publish.yml -f tag=blueevolution-v0.2.13

# 3. Monitor completion
gh run list --workflow=publish.yml --limit=1 --json name,status,conclusion

# 4. Verify Discover sees it (may take 2-4 hours)
```

### P1: Fix release.yml - Remove Draft Flag
**Change**: Delete `--draft` flag so releases auto-publish
**File**: `.github/workflows/release.yml` (line ~224)
**Before**:
```bash
gh release create "${args[@]}" --draft
```
**After**:
```bash
gh release create "${args[@]}"  # No --draft = auto-publish for non-pre-releases
```
**Verification**: Next release should appear in Discover within 2-4 hours automatically

### P2: Add Publication Status Check to release.yml
**Add**: Step after release creation to verify it's published
```yaml
- name: Verify release published
  env:
    TAG: ${{ steps.release.outputs.tag }}
    VERSION: ${{ steps.release.outputs.version }}
  run: |
    is_draft=$(gh release view "$TAG" --json isDraft --jq .isDraft)
    echo "Release $TAG isDraft=$is_draft"
    if [[ "$is_draft" == "true" && "$VERSION" != *"-"* ]]; then
      echo "ERROR: Release should not be draft for non-pre-release versions!"
      exit 1
    fi
```

### P3: Fix publish-flatpak.mjs - Handle Missing Previous Versions
**Issue**: Script fails if previous version doesn't exist
**Solution**: Add fallback to build from scratch if previous not found
**File**: `packaging/publish-flatpak.mjs`
**Change**: Wrap previous release download in try-catch
```javascript
if (previous && !alreadyPublished) {
  try {
    download(previous.latest_tag, previous.archive_name, root);
    assert.equal(await checksum(join(root, previous.archive_name)), previous.archive_sha256, 'Saved repository checksum mismatch');
    run('tar', ['-xzf', join(root, previous.archive_name), '-C', repo]);
  } catch (err) {
    console.warn(`Warning: Previous release ${previous.latest_tag} not found, building from scratch`);
    // Continue with empty repo - will create all objects fresh
  }
}
```

### P4: Create verify-publication.yml Workflow
**Purpose**: Post-release verification - automated checks for Discover visibility
**Triggers**: When release is published
**Checks**:
1. Release not in Draft mode
2. Release marked "Latest" (unless pre-release)
3. Version in release.json matches tag
4. Flatpak metadata present
5. GitHub Pages deployment successful
6. Waits 30 seconds then polls Discover for visibility

**Output**: Automatic comment on GitHub issue/PR with results

### P5: Create release-test.yml Workflow
**Purpose**: Comprehensive pre-release testing
**Triggers**: Manual workflow_dispatch
**Checks**:
- Version increment validation
- Tag format validation
- No conflicting existing releases
- CHANGELOG.md updated
- All commits signed (optional)
- Artifact checksum validation

## Version Display Issue (Discover)

**Current Problem**: Version shows as blank in Discover entry

**Investigation Needed**:
1. Check if version is in release.json metadata
2. Check if version is in Flatpak appdata
3. Check Discover catalog cache (clear & resync)
4. Check if it's a Discover UI bug vs. missing data

**Possible Fixes**:
- Add version to release.json: `{"version": "0.2.13", ...}`
- Update appdata.xml with release version
- Reindex Discover catalog

## Testing & Validation

### Release Checklist (to add to RELEASE.md)
- [ ] Version bumped in CMakeLists.txt
- [ ] Tag created and pushed
- [ ] Build workflow completes successfully
- [ ] Release appears (check: `gh release list`)
- [ ] Release is marked "Latest" and not "Draft"
- [ ] Run verify-publication workflow
- [ ] Discover shows version within 4 hours
- [ ] Help menu shows correct version (after update)

### Automated Testing
After implementing fixes, run:
```bash
# Create test release
git tag blueevolution-vTEST-0.2.0
git push origin blueevolution-vTEST-0.2.0

# Monitor workflows
gh run list --workflow=release.yml --limit=1
gh run list --workflow=publish.yml --limit=1
gh run list --workflow=verify-publication.yml --limit=1

# Verify chain completes without manual intervention
```

## Long-Term Improvements

1. **Automated version management**
   - Semantic versioning enforced by CI
   - Version bumps in separate commits with specific format
   - Automatic changelog generation from commits

2. **Release candidate process**
   - `-rc.1`, `-rc.2` pre-releases for testing
   - Beta channel separate from stable
   - Gradual rollout: beta → release candidate → stable

3. **Rollback capability**
   - Keep last 2 versions published and available
   - Quick downgrade procedure if critical issues found
   - Don't delete old releases

4. **Monitoring**
   - Alert when users still on old versions
   - Track crash reports by version
   - Monitor Discover visibility in real-time

## References
- GitHub release API: `gh release --help`
- Flatpak update mechanism: https://docs.flatpak.org/en/latest/
- Discover catalog: https://flathub.org/ 
- GPT data: Check release.json structure
