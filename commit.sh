#!/bin/bash

clear

while true; do
  echo "Write the commit message (leave empty for 'update'):"
  read commit_msg

  # Default commit message
  if [ -z "$commit_msg" ]; then
    commit_msg="update"
  fi

  echo ""
  echo "Commit message: \"$commit_msg\""
  read -p "Proceed with commit? (Y/n): " confirm

  # Break if 'y' OR empty (Enter)
  if [ "$confirm" = "y" ] || [ -z "$confirm" ]; then
    break
  fi

  echo ""
  echo "↩ Re-enter commit message."
  echo ""
done

# Stage changes
git add .

# Show status
git status

# Commit and push
git commit -m "$commit_msg"
git push