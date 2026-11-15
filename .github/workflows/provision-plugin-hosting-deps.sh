#!/usr/bin/env bash
# provision-plugin-hosting-deps.sh — obtain the pinned VST3 SDK and CLAP headers.
#
# WHY THIS EXISTS
# ---------------
# The shipped v0.1.0-alpha was built with BOTH plugin hosts silently skipped on
# all seven jobs of its release run: every configure printed
#
#   -- VST3 hosting skipped: no SDK at '<runner>/build/vst3sdk' (...)
#   -- CLAP hosting skipped: no headers at '<runner>/build/clap/include/clap' (...)
#
# because nothing in CI ever fetched them and both options default to AUTO
# (AUTO + absent == skip with a STATUS line). The release notes advertised VST3
# and CLAP hosting anyway. This script is the fetch that was missing; the
# release-honesty guard (tests/release-honesty-gate.sh) is what makes a repeat
# fail loudly instead of shipping.
#
# ONE HOME FOR THE PINS
# ---------------------
# The tag and commit are NOT restated here. They are read out of
# cmake/modules/Vst3Sdk.cmake and cmake/modules/ClapHeaders.cmake, which already
# document them for a human; the workflow cache key hashes those same two files,
# so bumping a pin in the module invalidates the cache by itself.
#
# Usage:
#   bash .github/workflows/provision-plugin-hosting-deps.sh [BUILD_DIR]
#
# BUILD_DIR defaults to "build". The SDK lands in <BUILD_DIR>/vst3sdk and the
# CLAP headers in <BUILD_DIR>/clap — the default paths
# plugins/Vst3Effect/CMakeLists.txt and plugins/ClapEffect/CMakeLists.txt look
# at, so no extra -D is needed on the configure line.
#
# Cached runs: when <BUILD_DIR>/vst3sdk is already a checkout at the pinned
# commit with the submodule content the host needs, nothing is fetched and the
# script says so. The commit is re-verified on every run, so a stale cache
# cannot be used silently.
#
# Exit status: 0 only when both trees are present at the pinned commits with the
# licences the repository requires (VST3 SDK MIT, CLAP headers MIT, no VST2
# headers vendored). Any other outcome exits non-zero with the reason.

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"

BUILD_DIR="${1:-build}"
mkdir -p "$BUILD_DIR" || { echo "provision-plugin-hosting-deps: cannot create '$BUILD_DIR'" >&2; exit 1; }
BUILD_DIR="$(cd "$BUILD_DIR" && pwd)" || exit 1

fail() { echo "provision-plugin-hosting-deps: $*" >&2; exit 1; }

# --- the pins, read from the modules that own them --------------------------
pin_from() { # pin_from <repo-relative file> <CMAKE variable>
	local file="$ROOT/$1" var="$2" value
	[ -f "$file" ] || fail "no $1 — that file is where the pin lives"
	value="$(sed -n "s/^SET($var \"\\(.*\\)\")\$/\\1/p" "$file" | head -1)"
	[ -n "$value" ] || fail "could not read $var from $1"
	printf '%s' "$value"
}

VST3_TAG="$(pin_from cmake/modules/Vst3Sdk.cmake LMMS_VST3_SDK_TAG)"
VST3_COMMIT="$(pin_from cmake/modules/Vst3Sdk.cmake LMMS_VST3_SDK_COMMIT)"
VST3_URL="$(pin_from cmake/modules/Vst3Sdk.cmake LMMS_VST3_SDK_URL)"
CLAP_TAG="$(pin_from cmake/modules/ClapHeaders.cmake LMMS_CLAP_TAG)"
CLAP_COMMIT="$(pin_from cmake/modules/ClapHeaders.cmake LMMS_CLAP_COMMIT)"
CLAP_URL="$(pin_from cmake/modules/ClapHeaders.cmake LMMS_CLAP_URL)"

[ -n "$VST3_COMMIT" ] && [ -n "$CLAP_COMMIT" ] || fail "the pins are empty"
[ -n "${GITHUB_ACTIONS:-}" ] && echo "::group::provision the pinned plugin-hosting dependencies"

at_commit() { # at_commit <dir> <40-char commit>
	[ -d "$1/.git" ] || return 1
	[ "$(git -C "$1" rev-parse HEAD 2>/dev/null)" = "$2" ]
}

require_mit() { # require_mit <licence file> <what>
	[ -f "$1" ] || fail "$1 is missing: cannot verify that $2 is MIT licensed"
	grep -q "MIT License" "$1" \
		|| fail "$1 is not the MIT licence — $2 may not be used by this GPL-2.0-or-later product"
}

# --- VST3 SDK ---------------------------------------------------------------
VST3_DIR="$BUILD_DIR/vst3sdk"
# The subset the host compiles: one file from each submodule the hosting sources
# live in. A cache restored from a half-written or filtered archive fails here
# and is re-cloned rather than used.
vst3_complete() {
	at_commit "$VST3_DIR" "$VST3_COMMIT" \
		&& [ -f "$VST3_DIR/LICENSE.txt" ] \
		&& [ -f "$VST3_DIR/pluginterfaces/vst/ivstcomponent.h" ] \
		&& [ -f "$VST3_DIR/base/source/fobject.cpp" ] \
		&& [ -f "$VST3_DIR/cmake/modules/SMTG_VST3_SDK.cmake" ] \
		&& [ -f "$VST3_DIR/public.sdk/source/vst/hosting/module.cpp" ]
}

if vst3_complete; then
	echo "VST3 SDK: already at ${VST3_TAG} (${VST3_COMMIT:0:9}) in $VST3_DIR — nothing to fetch"
else
	echo "VST3 SDK: fetching ${VST3_TAG} (${VST3_COMMIT:0:9}) from $VST3_URL into $VST3_DIR"
	rm -rf "$VST3_DIR"
	git clone --depth 1 --branch "$VST3_TAG" "$VST3_URL" "$VST3_DIR" \
		|| fail "git clone of $VST3_URL at tag $VST3_TAG failed"
	git -C "$VST3_DIR" submodule update --init --depth 1 base cmake pluginterfaces public.sdk \
		|| fail "the VST3 SDK submodules needed by the host could not be checked out"
fi

at_commit "$VST3_DIR" "$VST3_COMMIT" \
	|| fail "VST3 SDK is at '$(git -C "$VST3_DIR" rev-parse HEAD 2>/dev/null)' but the pin is $VST3_COMMIT"
require_mit "$VST3_DIR/LICENSE.txt" "the VST3 SDK"
[ -f "$VST3_DIR/pluginterfaces/vst/ivstcomponent.h" ] \
	|| fail "no pluginterfaces/vst/ivstcomponent.h in $VST3_DIR"
if [ -e "$VST3_DIR/pluginterfaces/vst2.x" ]; then
	fail "$VST3_DIR/pluginterfaces/vst2.x exists: VST2 headers must never be vendored (Vestige is the only VST2 path)"
fi

# --- CLAP headers -----------------------------------------------------------
CLAP_DIR="$BUILD_DIR/clap"
clap_complete() {
	at_commit "$CLAP_DIR" "$CLAP_COMMIT" \
		&& [ -f "$CLAP_DIR/LICENSE" ] \
		&& [ -f "$CLAP_DIR/include/clap/clap.h" ] \
		&& [ -f "$CLAP_DIR/include/clap/version.h" ]
}

if clap_complete; then
	echo "CLAP: already at ${CLAP_TAG} (${CLAP_COMMIT:0:9}) in $CLAP_DIR — nothing to fetch"
else
	echo "CLAP: fetching ${CLAP_TAG} (${CLAP_COMMIT:0:9}) from $CLAP_URL into $CLAP_DIR"
	rm -rf "$CLAP_DIR"
	git clone --depth 1 --branch "$CLAP_TAG" "$CLAP_URL" "$CLAP_DIR" \
		|| fail "git clone of $CLAP_URL at tag $CLAP_TAG failed"
fi

at_commit "$CLAP_DIR" "$CLAP_COMMIT" \
	|| fail "CLAP is at '$(git -C "$CLAP_DIR" rev-parse HEAD 2>/dev/null)' but the pin is $CLAP_COMMIT"
require_mit "$CLAP_DIR/LICENSE" "the CLAP headers"
[ -f "$CLAP_DIR/include/clap/clap.h" ] || fail "no include/clap/clap.h in $CLAP_DIR"

# --- report -----------------------------------------------------------------
echo "plugin-hosting dependencies provisioned into $BUILD_DIR:"
echo "  VST3 SDK ${VST3_TAG} (${VST3_COMMIT:0:9}, MIT)  -> $VST3_DIR"
echo "  CLAP     ${CLAP_TAG} (${CLAP_COMMIT:0:9}, MIT)  -> $CLAP_DIR"
if [ -n "${GITHUB_ACTIONS:-}" ]; then echo "::endgroup::"; fi
exit 0
