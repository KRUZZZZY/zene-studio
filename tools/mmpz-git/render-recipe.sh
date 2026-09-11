#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# render-recipe.sh -- render an LMMS project headlessly, reproducibly, so two
# versions can be compared.  This is the CI/regression half of the mmpz-git
# audible-diff workflow (docs/MMPZ-GIT-DEPTH.md, "audible diff").
#
# Usage:
#   tools/mmpz-git/render-recipe.sh <project> -o <out.wav> [options]
#   tools/mmpz-git/render-recipe.sh <project> --tracks <dir> [options]
#
# Options:
#   -o, --output FILE      render one file (render)
#       --tracks DIR       render one file per track (rendertracks)
#       --build-dir DIR    directory holding the built `lmms` (default: build)
#       --renderer PATH    use this binary instead of looking in --build-dir
#       --samplerate HZ    pass -s to the renderer (44100..192000)
#       --jobs N           build parallelism if the binary has to be built
#       --build            build first (tools/local-ci.sh --build-dir DIR)
#       --quiet
#
# Exit status: 0 only if the binary was found (or built), every render
# succeeded and produced a non-empty WAV.  Each step's exit code is printed
# unpiped ("cmd > log 2>&1; echo EXIT=$?") because a pipeline reports the exit
# code of its last command, which launders a failed render into a green one.
#
# Everything runs with QT_QPA_PLATFORM=offscreen so no display is needed, and
# at a fixed samplerate when --samplerate is given, so two runs of the same
# project produce comparable audio (measured: identical to 0.000000 dB per bar;
# see docs/MMPZ-GIT-DEPTH.md).
#
# Build once, then reuse -- a second build directory on a shared disk is the
# thing this recipe exists to avoid:
#   JOBS=4 tools/local-ci.sh --build-dir build --jobs 4
#   tools/mmpz-git/render-recipe.sh song.mmpz -o /tmp/song.wav
#   tools/mmpz-git/mmpz_git.py audible-diff mine.mmpz theirs.mmpz

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"

BUILD_DIR="$ROOT/build"
RENDERER=""
MODE=""
OUTPUT=""
SAMPLERATE=""
JOBS="${JOBS:-4}"
DO_BUILD=0
QUIET=0

die() { printf 'render-recipe: %s\n' "$*" >&2; exit 2; }

while [ $# -gt 0 ]; do
	case "$1" in
		-o|--output)     OUTPUT="${2:?--output needs a path}"; MODE="render"; shift 2 ;;
		--tracks)        OUTPUT="${2:?--tracks needs a directory}"; MODE="rendertracks"; shift 2 ;;
		--build-dir)     BUILD_DIR="${2:?--build-dir needs a path}"; shift 2 ;;
		--renderer)      RENDERER="${2:?--renderer needs a path}"; shift 2 ;;
		--samplerate)    SAMPLERATE="${2:?--samplerate needs a value}"; shift 2 ;;
		--jobs)          JOBS="${2:?--jobs needs a value}"; shift 2 ;;
		--build)         DO_BUILD=1; shift ;;
		--quiet)         QUIET=1; shift ;;
		-h|--help)       sed -n '2,45p' "${BASH_SOURCE[0]}"; exit 0 ;;
		-*)              die "unknown option: $1" ;;
		*)               PROJECT="$1"; shift ;;
	esac
done

[ -n "${PROJECT:-}" ] || die "no project given (see --help)"
[ -n "$MODE" ] || die "give -o FILE or --tracks DIR"
[ -f "$PROJECT" ] || die "no such project: $PROJECT"

run() {                      # run <log> <cmd...> -- always reports the real exit code
	local log="$1"; shift
	if [ "$QUIET" -eq 1 ]; then
		"$@" > "$log" 2>&1
	else
		printf '\n$ %s\n' "$*"
		printf '  (log: %s)\n' "$log"
		"$@" > "$log" 2>&1
	fi
	local rc=$?
	printf '[exit %d] %s\n' "$rc" "$*"
	return $rc
}

if [ "$DO_BUILD" -eq 1 ]; then
	run "$BUILD_DIR/render-recipe-build.log" \
		env JOBS="$JOBS" bash "$ROOT/tools/local-ci.sh" \
		--build-dir "$(basename "$BUILD_DIR")" --jobs "$JOBS" \
		|| die "build failed; see $BUILD_DIR/render-recipe-build.log"
fi

if [ -z "$RENDERER" ]; then
	for cand in "$BUILD_DIR/lmms" "$BUILD_DIR/bin/lmms" "$ROOT/build/lmms"; do
		if [ -x "$cand" ]; then RENDERER="$cand"; break; fi
	done
fi
[ -n "$RENDERER" ] && [ -x "$RENDERER" ] || die \
	"no renderer found. Build once and reuse it:
    JOBS=4 tools/local-ci.sh --build-dir $BUILD_DIR --jobs 4
  or pass --renderer PATH."

printf 'render-recipe: %s\n' "$RENDERER"
printf '  project    : %s\n' "$PROJECT"
printf '  mode       : %s\n' "$MODE"
printf '  output     : %s\n' "$OUTPUT"
printf '  qt platform: offscreen\n'
[ -n "$SAMPLERATE" ] && printf '  samplerate : %s\n' "$SAMPLERATE"

if [ "$MODE" = "tracks" ]; then
	mkdir -p "$OUTPUT"
	ARGS=( "$RENDERER" rendertracks "$PROJECT" -o "$OUTPUT" -f wav )
else
	mkdir -p "$(dirname "$OUTPUT")"
	ARGS=( "$RENDERER" render "$PROJECT" -o "$OUTPUT" -f wav )
fi
[ -n "$SAMPLERATE" ] && ARGS+=( -s "$SAMPLERATE" )

LOG="$(dirname "$OUTPUT")/render-recipe.log"
if [ "$QUIET" -eq 1 ]; then
	QT_QPA_PLATFORM=offscreen "${ARGS[@]}" > "$LOG" 2>&1
else
	printf '\n$ QT_QPA_PLATFORM=offscreen %s\n' "${ARGS[*]}"
	printf '  (log: %s)\n' "$LOG"
	QT_QPA_PLATFORM=offscreen "${ARGS[@]}" > "$LOG" 2>&1
fi
rc=$?
printf '[exit %d] render\n' "$rc"
[ "$rc" -eq 0 ] || die "render failed; see $LOG"

if [ "$MODE" = "render" ]; then
	[ -s "$OUTPUT" ] || die "render produced no output: $OUTPUT"
	printf '\nrendered: %s (%s bytes)\n' "$OUTPUT" "$(wc -c < "$OUTPUT")"
	printf 'sha256  : %s\n' "$(sha256sum "$OUTPUT" | cut -d' ' -f1)"
	printf '\ncompare two versions:\n  %s audible-diff A.mmpz B.mmpz\n' \
		"$HERE/mmpz_git.py"
else
	n=$(find "$OUTPUT" -name '*.wav' -type f | wc -l)
	[ "$n" -gt 0 ] || die "rendertracks produced no WAVs in $OUTPUT"
	printf '\ntracks   : %d WAV(s) in %s\n' "$n" "$OUTPUT"
	find "$OUTPUT" -name '*.wav' -type f -printf '  %f (%s bytes)\n' | sort
fi
exit 0
