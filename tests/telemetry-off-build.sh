#!/usr/bin/env bash
#
# telemetry-off-build.sh — the `-DZENE_TELEMETRY=OFF` configuration must BUILD.
#
# WHY THIS GATE EXISTS
# --------------------
# docs/TELEMETRY-V1.md section 5 records the first time the packager kill switch
# was built: the OFF link FAILED (an undefined reference to the consent dialog's
# slots) and the switch was repaired. Later the agent control surface merged
# src/core/ControlCommandsTelemetry.cpp - which names the client's types - and
# the OFF configuration stopped compiling (an unused static function, fatal under
# -DUSE_WERROR=ON). Nothing noticed, because nothing re-checks that
# configuration: the release-honesty gate reads the build OPTIONS a binary
# reports, and no gate built the OFF path at all. This script is that check.
#
# WHAT IT PROVES, AND WHAT IT DOES NOT
# ------------------------------------
# Proves, mechanically, that:
#   * `cmake -DZENE_TELEMETRY=OFF` configures;
#   * the tree builds in that configuration with -DUSE_WERROR=ON (without -Werror
#     the original defect is only a warning, so -Werror is load-bearing here);
#   * the resulting binary contains NO telemetry symbol and NO telemetry string:
#     `nm -C` and `strings` both count 0. That is the kill switch's promise - the
#     client, its consent screen and its networking code are not in the binary,
#     so no send path exists - measured rather than asserted.
# It does NOT run the suite in that configuration, and it does NOT prove the
# telemetry-ENABLED configuration is unchanged (docs/TELEMETRY-KILL-SWITCH.md
# records that half: the byte-identical object hash, ctest 86/86, the unchanged
# render, and a controlled before/after section comparison). The registry-count
# half (74 commands ON, 72 OFF) is asserted by ControlRegistryTest's OFF slot and
# by the run below when that test binary is present.
#
# Usage:
#   bash tests/telemetry-off-build.sh [BUILD_DIR] [--jobs N]
#
# BUILD_DIR defaults to "build-off". The configure uses the release flag set for
# this repository (-DCMAKE_BUILD_TYPE=RelWithDebInfo -DUSE_WERROR=ON
# -DTARGET_UARCH=official -DUSE_COMPILE_CACHE=ON -DWANT_DEBUG_CPACK=ON) plus
# -DZENE_TELEMETRY=OFF. -DWANT_VST3=ON/-DWANT_CLAP=ON are added when the pinned
# checkouts are already in BUILD_DIR, and -DWANT_QT6=ON when this machine has no
# Qt5 development files; each deviation is printed, never applied silently. The
# script fetches nothing: it is a configuration guard, not a provisioner.
#
# Exit status: 0 = configure OK, build OK, both counts 0 (and the OFF registry
# assertion passed, when its test binary was built). Non-zero otherwise; the
# failing step, its exit code and its log path are printed. Logs land in
# <BUILD_DIR>/*.log and every exit code is measured unpiped (workspace rule 6).
#
# Copyright (c) 2026 Zene Studio contributors
#
# This file is part of LMMS - https://lmms.io
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public
# License as published by the Free Software Foundation; either
# version 2 of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public
# License along with this program (see COPYING); if not, write to the
# Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
# Boston, MA 02110-1301 USA.

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="build-off"
JOBS="${JOBS:-4}"

usage() {
	cat <<'EOF'
telemetry-off-build.sh — build the -DZENE_TELEMETRY=OFF configuration and
prove no telemetry symbol and no telemetry string reaches the binary.

Usage:
  bash tests/telemetry-off-build.sh [BUILD_DIR] [--jobs N]

  BUILD_DIR   build directory (default: build-off); its logs are <BUILD_DIR>/telemetry-off-*.log
  --jobs N    compile parallelism (default 4; this workspace caps at 4)
  --help

Exit status: 0 only if configure, build and both differential counts pass.
EOF
}

while [ $# -gt 0 ]; do
	case "$1" in
		--jobs) JOBS="${2:?--jobs needs a value}"; shift 2 ;;
		-h|--help) usage; exit 0 ;;
		-*) echo "telemetry-off-build.sh: unknown argument: $1" >&2; usage >&2; exit 2 ;;
		*) BUILD_DIR="$1"; shift ;;
	esac
done

cd "$REPO_ROOT" || exit 2
mkdir -p "$BUILD_DIR" || exit 2
LOG_PREFIX="$BUILD_DIR/telemetry-off"

# --- the release flag set, plus the kill switch -----------------------------
CMAKE_OPTS=(
	-DCMAKE_BUILD_TYPE=RelWithDebInfo
	-DUSE_WERROR=ON
	-DTARGET_UARCH=official
	-DUSE_COMPILE_CACHE=ON
	-DWANT_DEBUG_CPACK=ON
	-DZENE_TELEMETRY=OFF
)
DEVIATIONS=()

if [ -f "$BUILD_DIR/vst3sdk/LICENSE.txt" ] && [ -f "$BUILD_DIR/clap/include/clap/clap.h" ]; then
	CMAKE_OPTS+=(-DWANT_VST3=ON -DWANT_CLAP=ON)
else
	DEVIATIONS+=("the pinned VST3 SDK / CLAP checkouts are not in $BUILD_DIR: those two hosts are left at their AUTO default (the telemetry kill switch does not depend on them)")
fi

QT5_CONFIG=""
for d in "${Qt5_DIR:-}" \
	/usr/lib/x86_64-linux-gnu/cmake/Qt5 \
	/usr/lib/aarch64-linux-gnu/cmake/Qt5 \
	/usr/lib/cmake/Qt5 \
	/usr/local/lib/cmake/Qt5; do
	if [ -n "$d" ] && [ -f "$d/Qt5Config.cmake" ]; then
		QT5_CONFIG="$d/Qt5Config.cmake"
		break
	fi
done
if [ -z "$QT5_CONFIG" ]; then
	CMAKE_OPTS+=(-DWANT_QT6=ON)
	DEVIATIONS+=("no Qt5 development files on this machine: added -DWANT_QT6=ON")
fi

echo "=== telemetry-off-build: -DZENE_TELEMETRY=OFF must configure, build, and contain nothing telemetry ==="
echo "repo        : $REPO_ROOT"
echo "build dir   : $BUILD_DIR (jobs=$JOBS)"
echo "cmake opts  : ${CMAKE_OPTS[*]}"
for d in ${DEVIATIONS[@]+"${DEVIATIONS[@]}"}; do echo "  deviation : $d"; done
echo

# --- step 1: configure ------------------------------------------------------
echo "--- [1/3] configure ---"
cmake -S . -B "$BUILD_DIR" "${CMAKE_OPTS[@]}" > "$LOG_PREFIX-configure.log" 2>&1
RC_CONFIGURE=$?
echo "configure EXIT=$RC_CONFIGURE   (log: $LOG_PREFIX-configure.log)"
if [ "$RC_CONFIGURE" -ne 0 ]; then
	echo "--- configure failed; last 30 lines: ---"
	tail -n 30 "$LOG_PREFIX-configure.log"
	exit 1
fi
grep -E "^ZENE_TELEMETRY:BOOL=" "$BUILD_DIR/CMakeCache.txt" | sed 's/^/  /'

# --- step 2: build ----------------------------------------------------------
echo "--- [2/3] build (-j$JOBS) ---"
cmake --build "$BUILD_DIR" -j "$JOBS" > "$LOG_PREFIX-build.log" 2>&1
RC_BUILD=$?
echo "build EXIT=$RC_BUILD   (log: $LOG_PREFIX-build.log)"
if [ "$RC_BUILD" -ne 0 ]; then
	echo "--- build failed; last 30 lines: ---"
	tail -n 30 "$LOG_PREFIX-build.log"
	exit 1
fi

# --- step 3: the differential ----------------------------------------------
# The promise is not "the client is unused", it is "the client is not there".
# A symbol or a string with "telemetry" in it is the evidence that it is; the
# guard is that both counts are 0, not merely smaller than the ON build's.
#
# Why the strings count is taken on a debug-stripped copy: -g embeds the
# ABSOLUTE build path in every DWARF string, so `strings` on the unstripped
# binary matches this checkout's own directory name (this workspace's worktrees
# are named e.g. `zene-next-telemetry-off`) hundreds of times and can never
# reach 0 - in either configuration. That is a property of the machine, not of
# the code. Stripping the debug info removes exactly that and nothing else:
# every string the code carries (.rodata, .text, .symtab) survives, so a real
# telemetry string - a command id, a menu label, a payload key, a symbol name -
# is still caught. The raw unstripped count is printed for transparency.
BINARY="$BUILD_DIR/zene"
if [ ! -x "$BINARY" ]; then
	echo "telemetry-off-build: no executable at $BINARY after a successful build" >&2
	exit 1
fi
echo "--- [3/3] differential on $BINARY ---"
NM_COUNT="$(nm -C "$BINARY" | grep -ci telemetry)"
RC_NM=$?
STRIPPED="$BUILD_DIR/zene-no-debug"
objcopy --strip-debug "$BINARY" "$STRIPPED" > /dev/null 2>&1
RC_STRIP=$?
STRIPPED_COUNT=0
if [ "$RC_STRIP" -eq 0 ]; then
	STRIPPED_COUNT="$(strings "$STRIPPED" | grep -ci telemetry)"
	RC_STRINGS=$?
else
	RC_STRINGS=0
	echo "  note: objcopy --strip-debug failed (exit $RC_STRIP); counting on the unstripped binary instead"
	STRIPPED_COUNT="$(strings "$BINARY" | grep -ci telemetry)"
	RC_STRINGS=$?
fi
RAW_COUNT="$(strings "$BINARY" | grep -ci telemetry)"
RAW_PATH_COUNT="$(strings "$BINARY" | grep -i telemetry | grep -cF "$REPO_ROOT")"
echo "  nm -C $BINARY | grep -ci telemetry        = $NM_COUNT"
echo "  strings (debug-stripped) | grep -ci telemetry = $STRIPPED_COUNT"
echo "  raw strings (DWARF build paths included)  = $RAW_COUNT  (of which on this checkout's path: $RAW_PATH_COUNT)"

OVERALL=0
if [ "$RC_NM" -gt 1 ]; then
	echo "  FAIL: nm itself failed (exit $RC_NM)"
	OVERALL=1
elif [ "$NM_COUNT" -ne 0 ]; then
	echo "  FAIL: $NM_COUNT telemetry symbol(s) in a -DZENE_TELEMETRY=OFF binary"
	nm -C "$BINARY" | grep -i telemetry | head -n 20 | sed 's/^/    /'
	OVERALL=1
fi
if [ "$RC_STRINGS" -gt 1 ]; then
	echo "  FAIL: strings itself failed (exit $RC_STRINGS)"
	OVERALL=1
elif [ "$STRIPPED_COUNT" -ne 0 ]; then
	echo "  FAIL: $STRIPPED_COUNT telemetry string line(s) in a -DZENE_TELEMETRY=OFF binary"
	strings "$STRIPPED" | grep -i telemetry | head -n 20 | sed 's/^/    /'
	OVERALL=1
fi

# The registry half: 72 commands, not 74, and no telemetry.* id. Optional,
# because it needs the test binary from the same build (it is registered by the
# default target, so a full build above produces it).
REGISTRY_TEST="$BUILD_DIR/tests/ControlRegistryTest"
if [ "$OVERALL" -eq 0 ] && [ -x "$REGISTRY_TEST" ]; then
	echo "--- registry assertion ($REGISTRY_TEST) ---"
	QT_QPA_PLATFORM=offscreen "$REGISTRY_TEST" > "$LOG_PREFIX-registry-test.log" 2>&1
	RC_REG=$?
	echo "ControlRegistryTest EXIT=$RC_REG   (log: $LOG_PREFIX-registry-test.log)"
	if [ "$RC_REG" -ne 0 ]; then
		tail -n 20 "$LOG_PREFIX-registry-test.log" | sed 's/^/  /'
		OVERALL=1
	fi
elif [ ! -x "$REGISTRY_TEST" ]; then
	echo "note: no $REGISTRY_TEST in this build; the registry count (72, not 74) was not asserted here"
fi

echo
if [ "$OVERALL" -eq 0 ]; then
	echo "PASS: -DZENE_TELEMETRY=OFF configures, builds, and its binary carries"
	echo "      $NM_COUNT telemetry symbol(s) and $STRIPPED_COUNT telemetry string line(s)"
	echo "      once the debug info (which embeds this checkout's own path) is stripped."
	echo "      (docs/TELEMETRY-KILL-SWITCH.md has the ON-side evidence: byte-identical"
	echo "      object hash, ctest 86/86, unchanged render.)"
	exit 0
fi
echo "FAIL: the -DZENE_TELEMETRY=OFF configuration is not sound (see above)."
exit 1
