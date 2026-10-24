#!/bin/sh
# X-19: the tree-wide clang-format gate.
#
# The tree was reformatted with clang-format 18.1.3 (see git log), and
# clang-format's output changes between major versions -- a runner
# shipping 20.x would rewrite the tree on a gate check that no commit
# caused. So the gate pins the version it was formatted with and refuses
# to judge with any other.
#
# Usage:
#   tools/format-check.sh          # list the files that drift, exit 1 if any
#   tools/format-check.sh --write  # reformat the first-party files in place
#
# Exit codes: 0 = clean (or --write completed), 1 = drift found,
#             2 = wrong clang-format version or the tool is missing.

set -u

# the version the tree is formatted with; keep in sync with .clang-format
PINNED="18.1.3"

if ! command -v clang-format >/dev/null 2>&1; then
    echo "format-check: clang-format not found" >&2
    exit 2
fi

have="$(clang-format --version)"
case "$have" in
*"$PINNED"*) ;;
*)
    echo "format-check: clang-format $PINNED is required, found: $have" >&2
    echo "format-check: clang-format output is not stable across versions," >&2
    echo "so the gate must run the pinned one" >&2
    exit 2
    ;;
esac

# the first-party file set: every .cpp/.hpp/.h tracked by git, minus
# third_party (vendored code is not ours to reformat) and the build dirs
# (gitignored, never tracked). Using git ls-files keeps generated files
# and local scratch out of the set for free.
files="$(git ls-files '*.cpp' '*.hpp' '*.h' | grep -v '^third_party/')"
if [ -z "$files" ]; then
    echo "format-check: no files matched (are you in the repository root?)" >&2
    exit 2
fi

if [ "${1:-}" = "--write" ]; then
    # shellcheck disable=SC2086
    clang-format -i $files
    echo "format-check: rewrote $(echo "$files" | wc -l) files with clang-format $PINNED"
    exit 0
fi

drift=0
for f in $files; do
    if ! clang-format "$f" | diff -q - "$f" >/dev/null 2>&1; then
        echo "$f"
        drift=$((drift + 1))
    fi
done

if [ "$drift" -ne 0 ]; then
    echo "format-check: $drift file(s) drift from .clang-format" >&2
    echo "format-check: run tools/format-check.sh --write (clang-format $PINNED)" >&2
    exit 1
fi
echo "format-check: clean ($(echo "$files" | wc -l) files, clang-format $PINNED)"
