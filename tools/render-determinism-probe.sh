#!/usr/bin/env bash
#
# render-determinism-probe.sh - the reproduction recipe for Zene Studio's render
# non-determinism (docs/RENDER-DETERMINISM.md).
#
# Renders the same project N times with the same binary in fresh processes, hashes
# each WAV, and reports per project whether the runs were bit-identical and, when
# they were not, how big the difference is and where it sits. Nothing here compares
# with a tolerance: the point is to *measure* the spread, so a future fix has
# something to be measured against.
#
#   bash tools/render-determinism-probe.sh                      # bundled projects, 5 runs
#   bash tools/render-determinism-probe.sh --runs 10 --rate 48000
#   bash tools/render-determinism-probe.sh --project data/projects/shorties/DirtyLove.mmpz
#   bash tools/render-determinism-probe.sh --all                # + the 38 upstream demos (hours)
#   bash tools/render-determinism-probe.sh --ld-preload /tmp/ncpu.so   # instrumented runs
#
# Output: a per-project table on stdout, the raw per-render WAVs, one log per render
# and the full pairwise comparison in <out>/ (default: a fresh mktemp -d, so two lanes
# running this concurrently cannot collide - see the TwoTrackRecordingHarness fix).

set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BINARY="$ROOT/build/lmms"
RUNS=5
RATE=44100
PERIOD=256
ALL_PROJECTS=0
OUT=""
LD_PRELOAD_PATH=""
PROJECTS=()

usage() {
	sed -n '3,22p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
	exit "${1:-0}"
}

while [ $# -gt 0 ]; do
	case "$1" in
		--binary)      BINARY="$2"; shift 2 ;;
		--runs)        RUNS="$2"; shift 2 ;;
		--rate)        RATE="$2"; shift 2 ;;
		--period)      PERIOD="$2"; shift 2 ;;
		--out)         OUT="$2"; shift 2 ;;
		--project)     PROJECTS+=("$2"); shift 2 ;;
		--all)         ALL_PROJECTS=1; shift 1 ;;
		--ld-preload)  LD_PRELOAD_PATH="$2"; shift 2 ;;
		-h|--help)     usage 0 ;;
		*) echo "unknown option '$1'" >&2; usage 2 ;;
	esac
done

[ -x "$BINARY" ] || { echo "ERROR: no executable at $BINARY (build it first, or pass --binary)" >&2; exit 1; }
[ "$RUNS" -ge 2 ] || { echo "ERROR: --runs must be at least 2" >&2; exit 1; }
COMPARE="$ROOT/tools/render-determinism-compare.py"
[ -f "$COMPARE" ] || { echo "ERROR: missing $COMPARE" >&2; exit 1; }

if [ "${#PROJECTS[@]}" -eq 0 ]; then
	# The projects this repo bundles as its own (data/projects/shorties, tutorials and
	# the demo that ships with the product). The upstream demo library under
	# data/projects/demos is ~38 further projects with renders of minutes each; --all
	# adds them, deliberately, because a full sweep takes hours.
	if [ "$ALL_PROJECTS" -eq 1 ]; then
		GLOB=(data/projects)
	else
		GLOB=(data/projects/shorties data/projects/tutorials data/projects/demos/StrictProduction-DearJonDoe.mmp)
	fi
	# Bundled projects, stable order so two readers compare the same list.
	while IFS= read -r project; do PROJECTS+=("$project"); done < <(
		cd "$ROOT" && find "${GLOB[@]}" -name '*.mmp' -o -name '*.mmpz' | LC_ALL=C sort
	)
fi

if [ -z "$OUT" ]; then
	OUT="$(mktemp -d -t render-determinism-XXXXXX)"
fi
mkdir -p "$OUT"

echo "binary      : $BINARY"
echo "sha256      : $(sha256sum "$BINARY" | cut -d' ' -f1)"
echo "runs/proj   : $RUNS"
echo "rate        : $RATE Hz, format wav (16-bit PCM unless -a)"
echo "period      : $PERIOD frames"
echo "scratch     : $OUT"
[ -n "$LD_PRELOAD_PATH" ] && echo "LD_PRELOAD  : $LD_PRELOAD_PATH"
echo

SUMMARY="$OUT/summary.tsv"
printf 'project\truns\tdistinct_sha\tstable\tdiffering\tfirst\tmax_lsb\tmax_dbfs\tlevel_delta_db\tlag\tdirty_periods\tperiods\n' > "$SUMMARY"

failures=0
for project in "${PROJECTS[@]}"; do
	name="$(basename "$project" | sed 's/\.[^.]*$//')"
	dir="$OUT/$name"
	mkdir -p "$dir"

	shas=""
	for run in $(seq 1 "$RUNS"); do
		# Render from the repo root so sample lookup matches the documented recipe.
		( cd "$ROOT" && QT_QPA_PLATFORM=offscreen LD_PRELOAD="$LD_PRELOAD_PATH" \
			"$BINARY" render "$project" -o "$dir/run$run.wav" -f wav -s "$RATE" \
			> "$dir/run$run.log" 2>&1 )
		code=$?
		if [ "$code" -ne 0 ]; then
			echo "  [FAIL] $name run $run exit=$code (see $dir/run$run.log)"
			failures=$((failures + 1))
		fi
		shas="$shas $(sha256sum "$dir/run$run.wav" | cut -d' ' -f1)"
	done

	distinct="$(printf '%s\n' $shas | sort -u | wc -l)"
	if [ "$distinct" -eq 1 ]; then
		printf '%-32s stable   (all %s runs sha256 %s)\n' "$name" "$RUNS" "$(printf '%s\n' $shas | head -1 | cut -c1-16)"
		printf '%s\t%s\t%s\tyes\t0\t-\t0\t-inf\t0.000000\t0\t0\t-\n' "$project" "$RUNS" "$distinct" >> "$SUMMARY"
		continue
	fi

	# Pair every run against run 1; the widest delta is the project's spread.
	python3 "$COMPARE" "$dir"/run1.wav $(for r in $(seq 2 "$RUNS"); do echo "$dir/run$r.wav"; done) \
		--period "$PERIOD" --tsv > "$dir/pairs.tsv"
	row="$(python3 - "$dir/pairs.tsv" <<'PY'
import sys
rows = [l.rstrip("\n").split("	") for l in open(sys.argv[1]) if l.strip()][1:]
worst = max(rows, key=lambda r: int(r[3]))
# differing, first, max_lsb, max_dbfs, level_delta_db, lag, dirty_periods, periods
print("	".join([worst[3], worst[4], worst[5], worst[6], worst[8], worst[9],
                 worst[11], worst[12]]))
PY
)"
	# shellcheck disable=SC2086 # $row is deliberately word-split into printf arguments
	printf '%-32s UNSTABLE (%s distinct sha256 of %s runs): differing=%s first=%s max=%s LSB (%s dBFS) level=%s dB lag=%s dirty_periods=%s of %s\n' \
		"$name" "$distinct" "$RUNS" $row
	printf '%s\t%s\t%s\tno\t%s\n' "$project" "$RUNS" "$distinct" "$row" >> "$SUMMARY"
done

echo
echo "per-project summary : $SUMMARY"
echo "per-render logs     : $OUT/<project>/run<N>.log"
echo "pairwise detail     : $OUT/<project>/pairs.tsv"
echo "(render exit codes: $failures failure(s) out of $(( ${#PROJECTS[@]} * RUNS )))"
exit "$(( failures > 0 ? 1 : 0 ))"
