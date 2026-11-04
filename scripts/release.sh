#!/bin/bash
# BlueEvolution Release Script
# Automates version bumping, tagging, and publishing

set -euo pipefail

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION_FILE="$PROJECT_ROOT/meson.build"
CHANGELOG_FILE="$PROJECT_ROOT/CHANGELOG.md"

echo -e "${BLUE}╔════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║   BlueEvolution Release Automation    ║${NC}"
echo -e "${BLUE}╚════════════════════════════════════════╝${NC}"
echo ""

# Function to print colored output
print_status() {
    echo -e "${GREEN}✅${NC} $1"
}

print_error() {
    echo -e "${RED}❌${NC} $1"
}

print_info() {
    echo -e "${BLUE}ℹ️${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}⚠️${NC} $1"
}

# Check prerequisites
check_requirements() {
    print_info "Checking prerequisites..."
    
    # Check git
    if ! command -v git &> /dev/null; then
        print_error "git not found"
        exit 1
    fi
    
    # Check gh CLI
    if ! command -v gh &> /dev/null; then
        print_error "gh CLI not found"
        exit 1
    fi
    
    # Check jq
    if ! command -v jq &> /dev/null; then
        print_warning "jq not found - some verification steps will be skipped"
    fi
    
    # Check git status is clean
    if ! git diff-index --quiet HEAD --; then
        print_error "Uncommitted changes detected. Please commit or stash all changes."
        exit 1
    fi
    
    print_status "All prerequisites met"
}

# Get current version
get_current_version() {
    grep "version:" "$VERSION_FILE" | head -1 | sed "s/.*version: '\([^']*\)'.*/\1/"
}

# Prompt for new version
prompt_version() {
    local current=$(get_current_version)
    echo ""
    print_info "Current version: $current"
    echo ""
    echo "Version format: MAJOR.MINOR.PATCH (e.g., 0.3.0)"
    echo ""
    read -p "Enter new version number: " new_version
    
    # Validate version format
    if ! [[ "$new_version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
        print_error "Invalid version format. Use MAJOR.MINOR.PATCH"
        exit 1
    fi
    
    echo "$new_version"
}

# Update version in meson.build
update_version() {
    local new_version=$1
    print_info "Updating version to $new_version..."
    
    sed -i "s/version: '[^']*'/version: '$new_version'/" "$VERSION_FILE"
    
    print_status "Version updated in meson.build"
}

# Edit CHANGELOG
edit_changelog() {
    local new_version=$1
    print_info "Opening CHANGELOG.md for editing..."
    echo ""
    echo "Add your release notes at the top of the file."
    echo "Format:"
    echo "## [$new_version] - $(date +%Y-%m-%d)"
    echo ""
    echo "### Added"
    echo "- Feature 1"
    echo ""
    echo "### Fixed"
    echo "- Bug fix 1"
    echo ""
    
    read -p "Press Enter to open CHANGELOG.md in your editor: "
    
    ${EDITOR:-nano} "$CHANGELOG_FILE"
}

# Create git commit and tag
create_commit_and_tag() {
    local new_version=$1
    
    print_info "Committing version changes..."
    cd "$PROJECT_ROOT"
    
    git add meson.build CHANGELOG.md
    git commit -m "Prepare v$new_version release"
    
    print_status "Commit created"
    
    print_info "Creating annotated tag..."
    
    read -p "Enter release description: " release_desc
    
    git tag -a "blueevolution-v$new_version" -m "BlueEvolution $new_version

$release_desc"
    
    print_status "Tag created: blueevolution-v$new_version"
}

# Push to GitHub
push_to_github() {
    local new_version=$1
    
    print_info "Pushing to GitHub..."
    
    git push origin main
    git push origin "blueevolution-v$new_version"
    
    print_status "Pushed to GitHub"
    echo ""
    print_info "Build will start automatically..."
}

# Wait for build
wait_for_build() {
    local new_version=$1
    
    echo ""
    print_info "Waiting for build workflow to start..."
    sleep 5
    
    local build_run=$(gh run list -L 1 --workflow=release.yml -q '.[0].databaseId')
    
    if [ -z "$build_run" ]; then
        print_warning "Could not find build run. Check manually with:"
        echo "  gh run list -L 1 --workflow=release.yml"
        return
    fi
    
    print_info "Build run: $build_run"
    echo ""
    
    read -p "Watch build? (y/n) " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        gh run watch "$build_run" --interval 10
    fi
}

# Publish release
publish_release() {
    local new_version=$1
    
    echo ""
    print_info "Checking if build completed..."
    
    local release_status=$(gh release view "blueevolution-v$new_version" --json isDraft -q '.isDraft' 2>/dev/null || echo "true")
    
    if [ "$release_status" = "true" ]; then
        print_info "Release is still in draft. Waiting a moment..."
        sleep 10
        release_status=$(gh release view "blueevolution-v$new_version" --json isDraft -q '.isDraft' 2>/dev/null || echo "true")
    fi
    
    if [ "$release_status" = "true" ]; then
        echo ""
        print_info "Ready to publish. Release is in draft status."
        read -p "Publish release? (y/n) " -n 1 -r
        echo
        if [[ $REPLY =~ ^[Yy]$ ]]; then
            gh release edit "blueevolution-v$new_version" --draft=false
            print_status "Release published"
            
            # Wait for publish workflow
            sleep 5
            local publish_run=$(gh run list -L 1 --workflow=publish.yml -q '.[0].databaseId')
            print_info "Publish run: $publish_run"
            
            read -p "Watch publish? (y/n) " -n 1 -r
            echo
            if [[ $REPLY =~ ^[Yy]$ ]]; then
                gh run watch "$publish_run" --interval 10
            fi
        fi
    else
        print_error "Release is not in draft status. Check manually with:"
        echo "  gh release view blueevolution-v$new_version"
    fi
}

# Verify release
verify_release() {
    local new_version=$1
    
    echo ""
    print_info "Verifying release..."
    
    # Check GitHub release
    local is_draft=$(gh release view "blueevolution-v$new_version" --json isDraft -q '.isDraft')
    if [ "$is_draft" = "false" ]; then
        print_status "Release is published (not draft)"
    else
        print_warning "Release is still in draft"
    fi
    
    # Check repository
    echo ""
    print_info "Checking Flatpak repository..."
    sleep 2  # Give pages time to update
    
    if curl -s https://themetalleg.github.io/BlueEvolution/index.html | grep -q "$new_version"; then
        print_status "Repository shows version $new_version"
    else
        print_warning "Repository may not have updated yet (DNS/CDN propagation)"
        echo "Check manually with:"
        echo "  curl https://themetalleg.github.io/BlueEvolution/index.html"
    fi
}

# Main flow
main() {
    check_requirements
    
    local new_version=$(prompt_version)
    
    echo ""
    print_info "Release plan: $(get_current_version) → $new_version"
    read -p "Continue? (y/n) " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        print_error "Release cancelled"
        exit 0
    fi
    
    echo ""
    echo -e "${BLUE}Step 1: Update version${NC}"
    update_version "$new_version"
    
    echo ""
    echo -e "${BLUE}Step 2: Update CHANGELOG${NC}"
    edit_changelog "$new_version"
    
    echo ""
    echo -e "${BLUE}Step 3: Create commit and tag${NC}"
    create_commit_and_tag "$new_version"
    
    echo ""
    echo -e "${BLUE}Step 4: Push to GitHub${NC}"
    push_to_github "$new_version"
    
    echo ""
    echo -e "${BLUE}Step 5: Wait for build${NC}"
    wait_for_build "$new_version"
    
    echo ""
    echo -e "${BLUE}Step 6: Publish release${NC}"
    publish_release "$new_version"
    
    echo ""
    echo -e "${BLUE}Step 7: Verify publication${NC}"
    verify_release "$new_version"
    
    echo ""
    echo -e "${GREEN}╔════════════════════════════════════════╗${NC}"
    echo -e "${GREEN}║   Release Complete: v$new_version${NC}               ${GREEN}║${NC}"
    echo -e "${GREEN}╚════════════════════════════════════════╝${NC}"
    echo ""
    echo "Users can now update with:"
    echo "  flatpak update io.github.themetalleg.BlueEvolution"
}

main "$@"
