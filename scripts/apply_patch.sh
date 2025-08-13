#!/usr/bin/env bash
set -euo pipefail

# Read entire stdin into a variable
PATCH_CONTENT=$(cat)

# If Codex CLI apply_patch command exists, forward to it
if command -v apply_patch >/dev/null 2>&1; then
  printf "%s" "$PATCH_CONTENT" | apply_patch
  exit $?
fi

# If the input looks like a standard unified diff, try git apply
if printf "%s" "$PATCH_CONTENT" | grep -Eq '^(diff --git|--- |\+\+\+ )'; then
  printf "%s" "$PATCH_CONTENT" | git apply -p0 --whitespace=nowarn
  exit $?
fi

echo "scripts/apply_patch.sh: No native 'apply_patch' available and input is not a unified diff." >&2
echo "Provide a unified diff (git diff) or run inside the Codex CLI where 'apply_patch' exists." >&2
exit 2
