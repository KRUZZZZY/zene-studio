#!/usr/bin/env bash
# prove-clap-loader-unchanged.sh — the CLAP loader's local proof (task #667:
# CLAP hosting on Windows, the typed error path in the loader).
#
# WHAT THIS PROVES, AND HOW:
#
#   1. ClapLoader.cpp (the new translation unit), ClapHost.cpp and ClapLoader.h
#      still COMPILE under the fork's REAL flags. The flags are borrowed from a
#      sibling tree's compile_commands.json, because this lane has no build tree
#      of its own (a full build is ~25 GB; the owner directive for this pass
#      forbids spending the window on one). Generated headers (lmms_export.h,
#      lmmsconfig.h) are the one thing still read out of that tree, which is
#      exactly what the CODE-9 lane's proof does.
#
#   2. The POSIX loader is BIT-FOR-BYTE the one that shipped before the platform
#      boundary moved out of ClapHost.cpp: the dlopen/dlsym/dlclose/dlerror call
#      lines -- the call text AND the open mode flags -- are compared as whole
#      lines against the pre-move source. Not "the diff looks small": the five
#      lines that ARE the POSIX loader must be identical to the ones in
#      $BASE_REV.
#
#   3. ClapHost.cpp no longer touches the platform loader at all (0 hits for
#      dlopen/dlsym/dlclose/dlerror/dlfcn). The boundary exists in exactly one
#      file now, which is what makes claim 2 meaningful.
#
#   4. The Windows half is GUARDED: on POSIX none of its symbols survive
#      preprocessing, so it contributes nothing to a POSIX build. (What this
#      does NOT prove is that it compiles on Windows -- see CI-ONLY below.)
#
#   5. The one-fault fixture modules that the typed error path is tested against
#      BUILD, each with its own fault, so a fixture that stopped compiling
#      cannot leave tests/src/plugins/ClapLoaderErrorTest.cpp passing vacuously.
#
# CI-ONLY, and NOT claimed here: this box has no MinGW and no MSVC toolchain, so
# the `#ifdef _WIN32` branch of ClapLoader.cpp has never been COMPILED, let alone
# run. Its compile and its LoadLibraryW/GetProcAddress behaviour are the three
# Windows jobs' business (build.yml: mingw, msvc, msys2, all three of which pass
# -DWANT_CLAP=ON), and the release-honesty gate's windows direction (see
# tests/advertised-features.tsv, row clap-hosting) is what enforces presence
# there. This script says so out loud instead of implying coverage it has not got.
#
# Usage:
#   bash tests/prove-clap-loader-unchanged.sh [<foreign-build-tree> [<foreign-source-tree>]]
#     <foreign-build-tree>   a configured tree of this fork (default: the
#                            merge-train worktree beside this one, .../zene-030/build)
#     <foreign-source-tree>  its source tree (default: .../zene-030)
#   Environment: CLAP_LOADER_BASE_REV (default release/0.3.0) — the revision the
#   POSIX loader is compared against; LMMS_CLAP_PATH — a CLAP header checkout,
#   if the foreign build tree has none.
#
# Exit codes: 0 = every claim held, 1 = a claim failed, 2 = setup error.
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
cd "$ROOT" || exit 2

BASE_REV="${CLAP_LOADER_BASE_REV:-release/0.3.0}"
FOREIGN_SRC="${2:-$(dirname "$ROOT")}"
FOREIGN_BUILD="${1:-$FOREIGN_SRC/build}"
SCRATCH="$(mktemp -d "${TMPDIR:-/tmp}/clap-loader-proof.XXXXXX")"

LOADER_CPP=plugins/ClapEffect/ClapLoader.cpp
LOADER_H=plugins/ClapEffect/ClapLoader.h
HOST_CPP=plugins/ClapEffect/ClapHost.cpp
FILES=("$LOADER_CPP" "$LOADER_H" "$HOST_CPP")

# The five lines that ARE the POSIX loader. Compared as whole lines against
# $BASE_REV, which is where they lived before the boundary moved.
POSIX_LINES=(
	'return dlopen(QFile::encodeName(path).constData(), RTLD_NOW | RTLD_LOCAL);'
	'return dlsym(module, name);'
	'if (module) { dlclose(module); }'
	'const auto* message = dlerror();'
	'return QString::fromLocal8Bit(message ? message : "unknown error");'
)
# The Windows half's API surface. Checked for presence in the source and for
# ABSENCE in the POSIX preprocessed stream (claim 4).
WINDOWS_SYMBOLS='LoadLibraryW\|GetProcAddress\|FreeLibrary\|FormatMessageW\|WIN32_LEAN_AND_MEAN\|LPCWSTR'
WINDOWS_NEEDED=(LoadLibraryW GetProcAddress FreeLibrary FormatMessageW)

if [[ ! -f "$FOREIGN_BUILD/compile_commands.json" ]]; then
	echo "error: no $FOREIGN_BUILD/compile_commands.json (pass the FOREIGN BUILD tree as \$1)" >&2
	exit 2
fi
git rev-parse --verify --quiet "$BASE_REV^{commit}" >/dev/null || {
	echo "error: '$BASE_REV' is not a commit in this repo (set CLAP_LOADER_BASE_REV)" >&2
	exit 2
}

failures=0
skips=0
claim() { printf '\n== %s\n' "$1"; }
verdict() { # verdict <ok|failed|skipped> <line>
	case "$1" in
		ok)      echo "PASS    $2" ;;
		failed)  echo "FAIL    $2"; failures=$((failures + 1)) ;;
		skipped) echo "SKIP    $2"; skips=$((skips + 1)) ;;
	esac
}

# --- the fork's real flags, re-pointed at THIS worktree -----------------------
python3 - "$ROOT" "$FOREIGN_SRC" "$FOREIGN_BUILD" > "$SCRATCH/flags.sh" <<'PY'
import json, shlex, sys
root, foreign, foreign_build = sys.argv[1:4]
db = json.load(open(foreign_build + "/compile_commands.json"))
entry = next(e for e in db if e["file"].endswith("plugins/ClapEffect/ClapHost.cpp"))
parts, out, i = shlex.split(entry["command"]), [], 0
while i < len(parts):
    p = parts[i]
    if p in ("-o", "-c"):
        i += 2
        continue
    # Source-tree paths point at this worktree. BUILD-tree paths (generated
    # headers) stay where they are: they only exist in a configured tree.
    if (p.startswith("-I") or p.startswith("-isystem")) and foreign in p \
            and not p.startswith("-I" + foreign_build) and not p.startswith("-isystem" + foreign_build):
        p = p.replace(foreign, root)
    out.append(p)
    i += 1
print("FLAGS=(" + " ".join(shlex.quote(x) for x in out[1:]) + ")")
PY
# shellcheck disable=SC1090
. "$SCRATCH/flags.sh" || exit 2

# --- 1. the three files compile ----------------------------------------------
claim "1. compile check (borrowed flags, -fsyntax-only; -Werror is in the flags)"
for f in "${FILES[@]}"; do
	if c++ "${FLAGS[@]}" -fsyntax-only "-I$ROOT/plugins/ClapEffect" "$ROOT/$f" 2> "$SCRATCH/compile.err"; then
		verdict ok "COMPILES   $f (EXIT=0)"
	else
		verdict failed "$f does not compile"
		sed -n '1,25p' "$SCRATCH/compile.err"
	fi
done

# --- 2. the POSIX loader is the same five lines -------------------------------
claim "2. the POSIX loader vs $BASE_REV (whole lines, call text and mode flags)"
git show "$BASE_REV:$HOST_CPP" > "$SCRATCH/base-ClapHost.cpp" 2> "$SCRATCH/show.err" || {
	echo "cannot extract $HOST_CPP from $BASE_REV:" >&2
	sed -n '1,5p' "$SCRATCH/show.err" >&2
	exit 2
}
for line in "${POSIX_LINES[@]}"; do
	old_count="$(grep -F -c "$line" "$SCRATCH/base-ClapHost.cpp")"
	new_count="$(grep -F -c "$line" "$LOADER_CPP")"
	if [[ "$old_count" == "1" && "$new_count" == "1" ]]; then
		verdict ok "IDENTICAL  $line"
	else
		verdict failed "not carried over verbatim (in $BASE_REV: $old_count, in $LOADER_CPP: $new_count): $line"
	fi
done

# --- 3. the boundary lives in exactly one file --------------------------------
claim "3. ClapHost.cpp no longer touches the platform loader"
hits="$(grep -c -E 'dlopen|dlsym|dlclose|dlerror|dlfcn|LoadLibraryW|GetProcAddress|FreeLibrary' "$HOST_CPP")"
if [[ "$hits" -eq 0 ]]; then
	verdict ok "ONE-PLACE  $HOST_CPP has 0 loader calls (they are all in $LOADER_CPP)"
else
	verdict failed "$HOST_CPP still has $hits loader call(s)"
	grep -n -E 'dlopen|dlsym|dlclose|dlerror|dlfcn|LoadLibraryW|GetProcAddress|FreeLibrary' "$HOST_CPP" | head -10
fi
# ... and the Windows half is actually present, not deleted along with it.
missing=""
for symbol in "${WINDOWS_NEEDED[@]}"; do
	grep -q "$symbol" "$LOADER_CPP" || missing="$missing $symbol"
done
if [[ -z "$missing" ]]; then
	verdict ok "PRESENT    the Windows half carries LoadLibraryW/GetProcAddress/FreeLibrary/FormatMessageW"
else
	verdict failed "the Windows half is missing:$missing"
fi

# --- 4. the Windows half is guarded on POSIX ----------------------------------
claim "4. the Windows half on POSIX (preprocessed token stream)"
( cd "$ROOT" && c++ "${FLAGS[@]}" -fsyntax-only -E "$LOADER_CPP" 2>/dev/null ) |
	grep -v '^#' | grep -v '^[[:space:]]*$' > "$SCRATCH/loader.pre"
hits="$(grep -c -i "$WINDOWS_SYMBOLS" "$SCRATCH/loader.pre")"
if [[ "$hits" -eq 0 ]]; then
	verdict ok "GUARDED    $LOADER_CPP contributes 0 Windows-only lines to a POSIX build"
else
	verdict failed "$hits Windows-only line(s) survive POSIX preprocessing"
	grep -in "$WINDOWS_SYMBOLS" "$SCRATCH/loader.pre" | head -10
fi

# --- 5. the one-fault fixtures build ------------------------------------------
claim "5. the typed-error fixtures build (tests/data/clap-test-plugin)"
CLAP_INCLUDE="${LMMS_CLAP_PATH:-$FOREIGN_BUILD/clap}/include"
FIXTURES="$ROOT/tests/data/clap-test-plugin"
if [[ ! -f "$CLAP_INCLUDE/clap/clap.h" ]]; then
	verdict skipped "no CLAP headers at $CLAP_INCLUDE (pass LMMS_CLAP_PATH=<checkout>)"
elif ! command -v gcc >/dev/null; then
	verdict skipped "no gcc on PATH"
else
	for fault in no-symbol old-version init-fails no-factory; do
		macro="$(echo "$fault" | tr '-' '_' | tr '[:lower:]' '[:upper:]')"
		if gcc -std=c11 -fPIC -shared -Wall -Wextra -Werror -I"$CLAP_INCLUDE" \
				-D"CLAP_TEST_FAULT_${macro}=1" \
				"$FIXTURES/clap-test-broken.c" -o "$SCRATCH/clap-test-broken-$fault.clap" \
				2> "$SCRATCH/fixture.err"; then
			verdict ok "BUILDS     clap-test-broken-$fault.clap (fault: $fault, EXIT=0)"
		else
			verdict failed "clap-test-broken-$fault does not build"
			sed -n '1,20p' "$SCRATCH/fixture.err"
		fi
	done
	# The one-fault source must refuse to compile with no fault selected: a
	# module that silently carries none would make the test pass vacuously.
	if gcc -std=c11 -fPIC -shared -I"$CLAP_INCLUDE" "$FIXTURES/clap-test-broken.c" \
			-o "$SCRATCH/clap-test-broken-none.clap" > "$SCRATCH/nofault.log" 2>&1; then
		verdict failed "clap-test-broken.c compiled without a fault macro; the #error guard is gone"
	else
		verdict ok "REFUSES    clap-test-broken.c without CLAP_TEST_FAULT_<MODE> (EXIT=$?)"
	fi
fi

rm -rf "$SCRATCH"

echo
echo "CI-ONLY (not measured by this script): the #ifdef _WIN32 branch of $LOADER_CPP"
echo "compiles and loads a module on Windows. No MinGW/MSVC toolchain exists on this"
echo "box; the mingw, msvc and msys2 jobs of .github/workflows/build.yml are the"
echo "proof, and tests/advertised-features.tsv's clap-hosting row is what fails if"
echo "they do not carry -DWANT_CLAP=ON."
echo
if [[ "$failures" -eq 0 ]]; then
	echo "CLAP-LOADER PROOF: PASS ($skips skipped) EXIT=0"
	exit 0
fi
echo "CLAP-LOADER PROOF: FAIL — $failures claim(s) did not hold EXIT=1"
exit 1
