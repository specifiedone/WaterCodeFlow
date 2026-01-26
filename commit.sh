#!/bin/bash

# Colors for better UX
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

clear

# Check if we're in a git repository
if ! git rev-parse --git-dir > /dev/null 2>&1; then
    echo -e "${RED}Error: Not a git repository${NC}"
    exit 1
fi

# Show current branch
current_branch=$(git branch --show-current)
echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${BLUE}Current branch:${NC} ${GREEN}$current_branch${NC}"
echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo ""

# Show git status before staging
echo -e "${YELLOW}📊 Current git status:${NC}"
git status --short
echo ""

# Check if there are any changes
if [ -z "$(git status --porcelain)" ]; then
    echo -e "${YELLOW}⚠️  No changes to commit${NC}"
    exit 0
fi

# Ask if user wants to stage all files
read -p "Stage all changes? (Y/n): " stage_all
echo ""

if [ "$stage_all" = "n" ] || [ "$stage_all" = "N" ]; then
    echo -e "${BLUE}💡 Use 'git add <file>' manually, then run this script again${NC}"
    exit 0
fi

# Stage all changes
git add .
echo -e "${GREEN}✓ All changes staged${NC}"
echo ""

# Show what will be committed
echo -e "${YELLOW}📋 Files to be committed:${NC}"
git status --short
echo ""

# Commit message loop
while true; do
    echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo "Write the commit message (leave empty for 'update'):"
    read commit_msg

    # Default commit message
    if [ -z "$commit_msg" ]; then
        commit_msg="update"
    fi

    echo ""
    echo -e "${GREEN}Commit message:${NC} \"$commit_msg\""
    read -p "Proceed with this message? (Y/n): " confirm

    # Break if 'y', 'Y' OR empty (Enter)
    if [ "$confirm" = "y" ] || [ "$confirm" = "Y" ] || [ -z "$confirm" ]; then
        break
    fi

    echo ""
    echo -e "${YELLOW}↩ Re-enter commit message.${NC}"
    echo ""
done

echo ""

# Commit
echo -e "${BLUE}📝 Committing changes...${NC}"
if git commit -m "$commit_msg"; then
    echo -e "${GREEN}✓ Commit successful${NC}"
else
    echo -e "${RED}✗ Commit failed${NC}"
    exit 1
fi

echo ""

# Ask before pushing
echo -e "${YELLOW}📤 Ready to push to remote:${NC} $current_branch"
read -p "Push to remote? (Y/n): " push_confirm

if [ "$push_confirm" = "n" ] || [ "$push_confirm" = "N" ]; then
    echo -e "${YELLOW}⏸  Commit created but not pushed${NC}"
    echo -e "${BLUE}💡 Run 'git push' manually when ready${NC}"
    exit 0
fi

echo ""

# Push to remote
echo -e "${BLUE}🚀 Pushing to remote...${NC}"
if git push; then
    echo ""
    echo -e "${GREEN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo -e "${GREEN}✓ Successfully pushed to $current_branch${NC}"
    echo -e "${GREEN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
else
    echo ""
    echo -e "${RED}✗ Push failed${NC}"
    echo -e "${YELLOW}💡 You may need to pull first: git pull${NC}"
    exit 1
fi