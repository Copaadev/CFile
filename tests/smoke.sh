#!/bin/sh
# Smoke test: drives the real cfile binary through stdin inside a private
# temporary directory (HOME is redirected there as well).
#
# Usage: sh tests/smoke.sh [path-to-cfile]
set -u

BIN=${1:-build/cfile}
case $BIN in /*) ;; *) BIN=$(pwd)/$BIN ;; esac

if [ ! -x "$BIN" ]; then
    echo "smoke: binary not found: $BIN" >&2
    exit 2
fi

SANDBOX=$(mktemp -d "${TMPDIR:-/tmp}/cfile-smoke-XXXXXX") || exit 2
case $SANDBOX in
    */cfile-smoke-*) ;;
    *) echo "smoke: refusing to use unexpected sandbox: $SANDBOX" >&2; exit 2 ;;
esac
trap 'rm -rf "$SANDBOX"' EXIT INT TERM

HOME=$SANDBOX
export HOME
PASS=0
FAIL=0

# run <command lines...>: feeds the lines to cfile started in the sandbox.
run() {
    printf '%s\n' "$@" | "$BIN" --no-color --no-banner "$SANDBOX" 2>&1
}

ok() { PASS=$((PASS + 1)); }
bad() { FAIL=$((FAIL + 1)); echo "FAIL: $1" >&2; }

expect_out() { # <label> <output> <fixed-string>
    if printf '%s' "$2" | grep -F -q -- "$3"; then ok; else
        bad "$1 (missing: $3)"; printf '%s\n' "$2" >&2; fi
}

expect_file() { if [ -e "$SANDBOX/$2" ]; then ok; else bad "$1 (expected: $2)"; fi; }
expect_gone() { if [ ! -e "$SANDBOX/$2" ]; then ok; else bad "$1 (still exists: $2)"; fi; }

# --- banner, menu, version ------------------------------------------------
out=$(printf 'quit\n' | "$BIN" --no-color "$SANDBOX" 2>&1)
expect_out "banner shows version" "$out" "Professional CLI file manager"
expect_out "menu lists cp" "$out" "cp <src> <dst>"
expect_out "quit says goodbye" "$out" "Goodbye."
out=$("$BIN" --version)
expect_out "--version" "$out" "cfile 1.0.0"
"$BIN" --bogus >/dev/null 2>&1
if [ $? -eq 2 ]; then ok; else bad "unknown option exits with 2"; fi
"$BIN" /definitely/not/here </dev/null >/dev/null 2>&1
if [ $? -eq 1 ]; then ok; else bad "bad start directory exits with 1"; fi

# --- mkdir / ls / permissions ----------------------------------------------
printf 'hello\n' > "$SANDBOX/a.txt"
chmod 640 "$SANDBOX/a.txt"
out=$(run "mkdir docs" "mkdir -p x/y/z" "mkdir docs" "ls")
expect_out "mkdir ok" "$out" "Created directory"
expect_file "mkdir created docs" docs
expect_file "mkdir -p created nested" x/y/z
expect_out "mkdir duplicate fails" "$out" "File exists"
expect_out "ls shows permissions" "$out" "-rw-r-----"
expect_out "ls shows directory" "$out" "docs/"

# --- cp, overwrite confirmation --------------------------------------------
out=$(run "cp a.txt b.txt")
expect_out "cp reports success" "$out" "Copied"
expect_file "cp created target" b.txt
printf 'other\n' > "$SANDBOX/c.txt"
out=$(run "cp c.txt b.txt" "n")
expect_out "cp asks before overwriting" "$out" "Overwrite?"
if [ "$(cat "$SANDBOX/b.txt")" = "hello" ]; then ok; else bad "cp kept target after 'n'"; fi
out=$(run "cp c.txt b.txt" "y")
if [ "$(cat "$SANDBOX/b.txt")" = "other" ]; then ok; else bad "cp overwrote after 'y'"; fi

# --- mv, rename -------------------------------------------------------------
out=$(run "mv b.txt docs" "rename docs/b.txt renamed.txt")
expect_file "mv moved into dir" docs/renamed.txt
expect_gone "mv removed source" b.txt
out=$(run "rename a.txt c.txt")
expect_out "rename refuses existing name" "$out" "File exists"
expect_file "rename kept source" a.txt
out=$(run "rename a.txt bad/name")
expect_out "rename rejects slash" "$out" "invalid name"

# --- find, info, cd, quoting -----------------------------------------------
out=$(run "find RENAMED")
expect_out "find is case-insensitive" "$out" "docs/renamed.txt"
out=$(run "find zzzz")
expect_out "find reports no match" "$out" "No entries matching"
out=$(run "info a.txt")
expect_out "info shows size" "$out" "6 B (6 bytes)"
expect_out "info shows octal mode" "$out" "(0640)"
mkdir "$SANDBOX/with space"
out=$(run "cd 'with space'" "pwd" "cd -" "pwd")
expect_out "cd handles quotes" "$out" "$SANDBOX/with space"

# --- destructive commands ask first ----------------------------------------
out=$(run "rm a.txt" "n")
expect_out "rm asks for confirmation" "$out" "Permanently remove 1 item?"
expect_file "rm kept file after 'n'" a.txt
out=$(run "rm a.txt" "")
expect_file "rm kept file on empty answer" a.txt
out=$(run "rm a.txt" "y")
expect_gone "rm removed file after 'y'" a.txt
out=$(run "rm docs")
expect_out "rm refuses directories" "$out" "is a directory"
expect_file "rm kept directory" docs
out=$(run "rmdir docs" "y")
expect_out "rmdir fails when not empty" "$out" "Directory not empty"
expect_file "rmdir kept non-empty directory" docs/renamed.txt
out=$(run "rmdir x/y/z" "y")
expect_gone "rmdir removed empty directory" x/y/z
out=$(run "rm ghost.txt")
expect_out "rm reports missing file" "$out" "No such file or directory"

# --- robustness ----------------------------------------------------------
out=$(run "frobnicate")
expect_out "unknown command" "$out" "unknown command"
out=$(run "rm 'unterminated")
expect_out "unterminated quote" "$out" "unterminated quote"
out=$(run "cp onlyone")
expect_out "wrong arity shows usage" "$out" "usage: cp <src> <dst>"
out=$(run "ls -z")
expect_out "unknown option" "$out" "unknown option"
long=$(head -c 5000 /dev/zero | tr '\0' 'a')
out=$(run "$long" "pwd")
expect_out "overlong line rejected" "$out" "input line too long"
expect_out "session survives overlong line" "$out" "$SANDBOX"
printf 'pwd\n' | "$BIN" --no-color --no-banner "$SANDBOX" >/dev/null 2>&1
if [ $? -eq 0 ]; then ok; else bad "EOF exits cleanly"; fi

# --- hostile file names cannot inject escape codes -------------------------
esc=$(printf '\033[31mevil')
: > "$SANDBOX/$esc"
out=$(run "ls")
if printf '%s' "$out" | grep -q "$(printf '\033')"; then bad "escape code leaked into ls"; else ok; fi

echo "smoke: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
