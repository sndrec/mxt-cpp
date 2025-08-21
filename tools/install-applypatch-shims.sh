#!/usr/bin/env bash
set -euo pipefail

echo '[codex] Installing applypatch/apply_patch shims for Git Bash...'

# Create target bin directory
BIN_DIR="$HOME/.local/bin"
mkdir -p "$BIN_DIR"

AP_SHIM="$BIN_DIR/applypatch"
APU_SHIM="$BIN_DIR/apply_patch"

cat > "$AP_SHIM" <<'EOS'
#!/usr/bin/env bash
set -euo pipefail
# Try Windows shim first (if codex installed via npm created applypatch.cmd)
if command -v applypatch.cmd >/dev/null 2>&1; then
  exec cmd.exe /c applypatch.cmd "$@"
fi
# Try near the codex binary
codex_bin="$(command -v codex || true)"
if [[ -n "$codex_bin" ]]; then
  codex_dir="$(cd "$(dirname "$codex_bin")" && pwd -P)"
  if [[ -f "$codex_dir/applypatch.cmd" ]]; then
    exec cmd.exe /c "\"$codex_dir\applypatch.cmd\"" "$@"
  fi
fi
# Fallback: implement a portable patch applier using git/patch
# Accept patch from file arg or stdin
patchfile=""
if [[ $# -gt 0 && -f "$1" ]]; then
  patchfile="$1"
else
  tmpdir="${TMPDIR:-${TEMP:-/tmp}}"
  patchfile="$(mktemp "$tmpdir/applypatch.XXXXXX.patch")"
  cat > "$patchfile"
fi
# Try multiple strategies
try_git_apply() {
  local p="$1"
  git apply --recount --whitespace=nowarn -p0 "$p" && return 0 || true
  git apply --recount --whitespace=nowarn -p1 "$p" && return 0 || true
  git apply --recount --whitespace=nowarn "$p" && return 0 || true
  return 1
}
try_posix_patch() {
  local p="$1"
  if command -v patch >/dev/null 2>&1; then
    patch -p0 -u -f -i "$p" && return 0 || true
    patch -p1 -u -f -i "$p" && return 0 || true
  fi
  return 1
}
if try_git_apply "$patchfile"; then
  exit 0
fi
if try_posix_patch "$patchfile"; then
  exit 0
fi
# Last resort: use Python apply_patch if present (some environments have it)
if command -v apply_patch >/dev/null 2>&1; then
  exec apply_patch "$@"
fi
echo "applypatch fallback failed: could not apply patch with git/patch, and no Windows shim found" >&2
exit 127
EOS

cat > "$APU_SHIM" <<'EOS'
#!/usr/bin/env bash
set -euo pipefail
exec "$(dirname "$0")/applypatch" "$@"
EOS

chmod +x "$AP_SHIM" "$APU_SHIM"

# Ensure ~/.local/bin is on PATH in future Git Bash sessions
profile_file="$HOME/.bashrc"
if ! grep -q 'PATH=.*/.local/bin' "$profile_file" 2>/dev/null; then
  echo 'export PATH="$HOME/.local/bin:$PATH"' >> "$profile_file"
  echo "[codex] Added ~/.local/bin to PATH in $profile_file"
fi

# Install helpful tools if available (MSYS2)
if command -v pacman >/dev/null 2>&1; then
  echo '[codex] Ensuring patch, ed, and dos2unix are installed (optional)...'
  pacman -S --noconfirm --needed patch ed dos2unix >/dev/null 2>&1 || true
fi

echo '[codex] Shims installed. Open a new Git Bash or run: export PATH="$HOME/.local/bin:$PATH"'
