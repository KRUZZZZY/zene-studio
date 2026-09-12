#!/usr/bin/env bash
# release-honesty-gate.sh — fail when a feature the release documents as present
# is compiled out of the build, or when a feature it documents as absent is
# compiled in.
#
# WHY THIS EXISTS
# ---------------
# The published v0.1.0-alpha shipped with no VST3 hosting and no CLAP hosting on
# any platform, while its release notes advertised both, because the two hosts
# are opt-in at configure time (WANT_VST3/WANT_CLAP default to AUTO) and AUTO
# with the dependency missing degrades to a STATUS line:
#
#   -- VST3 hosting skipped: no SDK at '<build>/vst3sdk' (...)
#   -- CLAP hosting skipped: no headers at '<build>/clap/include/clap' (...)
#
# Nothing failed; the packages were built and uploaded. This gate is what makes
# that fail instead. It does not carry a feature list: it reads
# tests/advertised-features.tsv, which is the single home for "what the release
# documents", so the gate and the release notes cannot drift apart silently.
#
# WHAT IT CHECKS AGAINST
# ----------------------
# The binary reports its build options itself. `lmms --version` prints a
# "Build options:" line, which is the C string LMMS_BUILD_OPTIONS
# (src/core/main.cpp:144) configured from the WANT_*/LMMS_HAVE_* CMake variables
# into src/lmmsversion.h (src/CMakeLists.txt:6-14, src/lmmsversion.h.in:3). Two
# inputs are accepted, and they carry the same text:
#
#   --dump   FILE   the output of `<lmms> --version` (what a user or a reviewer reads)
#   --header FILE   a build's generated lmmsversion.h
#
# A release job checks the header, because running the GUI binary's --version
# needs that platform's Qt runtime on PATH in seven different environments,
# while the header is written by configure and is already there. The local proof
# for this gate runs both and compares them.
#
# The option's value is the *requested* value, not the outcome: AUTO is not ON.
# A feature the release documents as present must be explicitly ON, and the
# module it builds must exist in the build tree (--artifacts), so
# "the option says AUTO and the SDK happened to be absent" cannot pass as
# presence. That is exactly the state the alpha shipped in.
#
# Usage:
#   bash tests/release-honesty-gate.sh --dump FILE [--artifacts DIR] [--manifest FILE]
#   bash tests/release-honesty-gate.sh --header FILE [--artifacts DIR] [--manifest FILE]
#
# Exit status: 0 when every documented feature matches the build, 1 when at
# least one does not, 2 on a usage or manifest error. A skip is not a pass:
# there is no way to run this gate that produces a green result without the
# features it names being present.
#
# THE PLATFORM COLUMN (6th field, optional; '*' or empty means every platform)
# ------------------------------------------------------
# A feature can ship on some platforms and not others, and the honest way to
# record that is a row that says where the claim holds rather than a row that is
# quietly not checked somewhere. On a platform the row does not list, the gate
# asserts the INVERSE: the option must not be ON and the module must not exist in
# the build tree. So `clap-hosting ... linux,macos` means CLAP is enforced present
# on Linux and macOS and enforced ABSENT on Windows — every platform is checked,
# in one direction or the other, and no run of this gate can be green while a
# platform ships a feature its documents say that platform does not have.
#
# The platform is detected from uname (Darwin→macos, Linux→linux, MINGW*/MSYS*/
# CYGWIN*→windows). RELEASE_HONESTY_PLATFORM overrides the detection, which is how
# the other platform's branch is tested on one machine.

set -uo pipefail

PLATFORM_OVERRIDE="${RELEASE_HONESTY_PLATFORM:-}"

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"

MANIFEST="$ROOT/tests/advertised-features.tsv"
SOURCE=""
SOURCE_KIND=""
ARTIFACTS=""

usage() {
	sed -n '2,60p' "$HERE/release-honesty-gate.sh" | sed -n 's/^# \{0,1\}//p' | sed '/^$/q'
}

while [ $# -gt 0 ]; do
	case "$1" in
		--dump)      SOURCE="${2:?--dump needs a file}";      SOURCE_KIND="dump";   shift 2 ;;
		--header)    SOURCE="${2:?--header needs a file}";    SOURCE_KIND="header"; shift 2 ;;
		--artifacts) ARTIFACTS="${2:?--artifacts needs a directory}";                 shift 2 ;;
		--manifest)  MANIFEST="${2:?--manifest needs a file}";                        shift 2 ;;
		-h|--help)   usage; exit 0 ;;
		*) echo "release-honesty-gate: unknown argument '$1'" >&2; exit 2 ;;
	esac
done

if [ -z "$SOURCE" ]; then
	echo "release-honesty-gate: one of --dump FILE or --header FILE is required" >&2
	echo "  (there is no default: the gate must be told which build to judge)" >&2
	exit 2
fi
[ -f "$SOURCE" ] || { echo "release-honesty-gate: no such file: $SOURCE" >&2; exit 2; }
[ -f "$MANIFEST" ] || { echo "release-honesty-gate: no manifest at $MANIFEST" >&2; exit 2; }
if [ -n "$ARTIFACTS" ] && [ ! -d "$ARTIFACTS" ]; then
	echo "release-honesty-gate: --artifacts is not a directory: $ARTIFACTS" >&2
	exit 2
fi

# Every occurrence of OPTION='value' in the build-options text. WANT_* options
# can appear more than once (a normal and a cache variable both match
# src/CMakeLists.txt's `^WANT|LMMS_(HAVE|DEBUG)` pattern); all of them must
# agree with the manifest, so a disagreement is a failure rather than a
# first-match accident.
option_values() { # option_values <file> <OPTION>
	tr ' ' '\n' < "$1" | sed -n "s/^$2='\\([^']*\\)'\$/\\1/p"
}

is_on() {
	case "$(printf '%s' "$1" | tr '[:lower:]' '[:upper:]')" in
		ON|TRUE|YES|1) return 0 ;;
		*)             return 1 ;;
	esac
}

is_off() {
	case "$(printf '%s' "$1" | tr '[:lower:]' '[:upper:]')" in
		OFF|FALSE|NO|0|"") return 0 ;;
		*)                 return 1 ;;
	esac
}

satisfies() { # satisfies <required> <value>
	case "$1" in
		ON)  is_on  "$2" ;;
		OFF) is_off "$2" ;;
		*)   [ "$1" = "$2" ] ;;
	esac
}

current_platform() { # linux | macos | windows | unknown
	if [ -n "$PLATFORM_OVERRIDE" ]; then printf '%s' "$PLATFORM_OVERRIDE"; return; fi
	case "$(uname -s 2>/dev/null || echo unknown)" in
		Darwin)                          printf 'macos' ;;
		Linux)                           printf 'linux' ;;
		MINGW*|MSYS*|CYGWIN*|Windows_NT) printf 'windows' ;;
		*)                               printf 'unknown' ;;
	esac
}

platform_listed() { # platform_listed <platforms-field> <platform>
	case "$1" in
		''|'*') return 0 ;;
	esac
	case ",$1," in
		*",$2,"*) return 0 ;;
	esac
	return 1
}

PLATFORM="$(current_platform)"

echo "=== release honesty: what the release documents vs what the build contains ==="
echo "manifest : ${MANIFEST#"$ROOT"/}"
case "$SOURCE_KIND" in
	dump)   echo "build    : $SOURCE  (lmms --version output: the \"Build options:\" line)" ;;
	header) echo "build    : $SOURCE  (generated header; the exact string the binary prints)" ;;
esac
[ -n "$ARTIFACTS" ] && echo "artifacts: $ARTIFACTS" || echo "artifacts: not given (module presence is not checked)"
echo

checked=0
failures=0
lineno=0
while IFS=$'	' read -r feature option required module claim platforms; do
	lineno=$((lineno + 1))
	case "$feature" in ''|'#'*) continue ;; esac
	if [ -z "${option:-}" ] || [ -z "${required:-}" ] || [ -z "${module:-}" ] || [ -z "${claim:-}" ]; then
		echo "release-honesty-gate: manifest line $lineno is incomplete (5 tab-separated fields required)" >&2
		exit 2
	fi
	checked=$((checked + 1))
	platforms="${platforms:-*}"

	# The platform field is validated, not trusted. A typo or a stray space ('Linux',
	# 'linux,macos ' with a trailing space) would move a row that documents a feature as
	# PRESENT into the inverse branch below and let it pass while the build does not contain
	# it -- the v0.1.0-alpha failure this gate exists to prevent, reintroduced by a typo. A
	# malformed field is a manifest error (exit 2), exactly like the other five fields.
	if [ "$platforms" != "*" ]; then
		case "$platforms" in
			*[[:space:]]*)
				echo "release-honesty-gate: manifest line $lineno: the platforms field contains whitespace ('$platforms')" >&2
				exit 2 ;;
		esac
		IFS=',' read -r -a _plats <<< "$platforms"
		for _p in "${_plats[@]}"; do
			case "$_p" in
				linux|macos|windows) : ;;
				*)	echo "release-honesty-gate: manifest line $lineno: unknown platform '$_p' (allowed: linux, macos, windows)" >&2
					exit 2 ;;
			esac
		done
	fi

	verdict="PASS"
	detail=""

	if ! platform_listed "$platforms" "$PLATFORM"; then
		# This row documents the feature as ABSENT on this platform, so assert that the
		# build does not have it — never skip the row (see the header's platform section).
		values="$(option_values "$SOURCE" "$option")"
		bad=""
		while IFS= read -r value; do
			[ -n "$value" ] || continue
			is_off "$value" || bad="$bad $value"
		done <<< "$values"
		if [ -n "$bad" ]; then
			verdict="FAIL"
			detail="$option='${bad# }' on $PLATFORM, but this release documents the feature as absent there (listed for: $platforms)"
		elif [ -z "$values" ]; then
			# Absence of the option is not evidence of the feature's absence. The present
			# branch fails on "not reported at all" for the same reason; without this, a row
			# whose option vanished from the build would pass by default here.
			verdict="FAIL"
			detail="$option is not reported by this build at all, so the absence this row documents on $PLATFORM cannot be demonstrated"
		else
			detail="$(printf '%s' "$values" | tr '\n' ',' | sed 's/,$//') — documented absent on $PLATFORM"
		fi

		if [ "$verdict" = "PASS" ] && [ -n "$ARTIFACTS" ] && [ "$module" != "-" ]; then
			found="$(find "$ARTIFACTS" -type f \
					\( -name '*.so' -o -name '*.dll' -o -name '*.dylib' \) \
					-name "*${module}*" 2>/dev/null | head -1)"
			if [ -n "$found" ]; then
				verdict="FAIL"
				detail="the build contains ${found#"$ROOT"/}, but this release documents the feature as absent on $PLATFORM"
			fi
		fi

		if [ "$verdict" = "PASS" ]; then
			printf '  [PASS] %-16s %s\n' "$feature" "$detail"
		else
			failures=$((failures + 1))
			printf '  [FAIL] %-16s %s\n' "$feature" "$detail"
			printf '         claim it must keep true: %s\n' "$claim"
		fi
		continue
	fi

	values="$(option_values "$SOURCE" "$option")"
	if [ -z "$values" ]; then
		verdict="FAIL"
		detail="$option is not reported by this build at all, so the feature cannot be in it"
	else
		while IFS= read -r value; do
			[ -n "$value" ] || continue
			if ! satisfies "$required" "$value"; then
				verdict="FAIL"
				detail="$option='$value', but the release documents this as present and requires $required"
			fi
		done <<< "$values"
		[ "$verdict" = "PASS" ] && detail="$(printf '%s' "$values" | tr '\n' ',' | sed 's/,$//') matches $required"
	fi

	if [ "$verdict" = "PASS" ] && [ -n "$ARTIFACTS" ] && [ "$module" != "-" ]; then
		found="$(find "$ARTIFACTS" -type f \
			\( -name '*.so' -o -name '*.dll' -o -name '*.dylib' \) \
			-name "*${module}*" 2>/dev/null | head -1)"
		if [ -n "$found" ]; then
			detail="$detail; module ${found#"$ROOT"/}"
		else
			verdict="FAIL"
			detail="$option says $required but no '*${module}*.so/.dll/.dylib' exists under $ARTIFACTS: the module was never built"
		fi
	fi

	if [ "$verdict" = "PASS" ]; then
		printf '  [PASS] %-16s %s\n' "$feature" "$detail"
	else
		failures=$((failures + 1))
		printf '  [FAIL] %-16s %s\n' "$feature" "$detail"
		printf '         claim it must keep true: %s\n' "$claim"
	fi
done < "$MANIFEST"

if [ "$checked" -eq 0 ]; then
	echo "release-honesty-gate: the manifest lists no features — that is an error, not a pass" >&2
	exit 2
fi

echo
if [ "$failures" -gt 0 ]; then
	echo "RESULT: FAIL — $failures of $checked documented feature(s) do not match this build on $PLATFORM"
	echo "Fix the build (provision the dependency; see .github/workflows/provision-plugin-hosting-deps.sh)"
	echo "or fix the documents, but do not ship this build with these documents."
	exit 1
fi
echo "RESULT: PASS — all $checked documented feature(s) match this build on $PLATFORM"
exit 0
