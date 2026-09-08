#!/bin/bash
# Reproduces the upstream-reference verification for the NeuralAmp engine.
#
# What it does:
#   1. builds the nam_harness target in $NAM_BUILD (default: build-rel)
#   2. generates the deterministic 2000-block test signal
#   3. builds the upstream NeuralAmpModelerCore `render` tool in a pristine
#      worktree pinned at $NAM_UPSTREAM_COMMIT (default: 2563c0f)
#   4. renders the reference WAVs with that tool
#   5. runs nam_harness --wav-run ... --compare-ref ... for each model
#
# Prereqs: a NeuralAmpModelerCore clone at $NAM_UPSTREAM_SRC including
# submodules (network access the first time).
#
# Env overrides: NAM_BUILD NAM_UPSTREAM_SRC NAM_UPSTREAM_COMMIT NAM_WORK NAM_MODELS
# Exit: 0 if every model's compare passes, 1 otherwise.
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"        # plugins/NeuralAmp
REPO="$(cd "$ROOT/../.." && pwd)"               # lmms-nam
BUILD="${NAM_BUILD:-$REPO/build-rel}"
UPSTREAM_SRC="${NAM_UPSTREAM_SRC:-/tmp/nam-src/NeuralAmpModelerCore}"
UPSTREAM_COMMIT="${NAM_UPSTREAM_COMMIT:-2563c0f}"
WORK="${NAM_WORK:-/tmp/nam-verify}"
MODELS="${NAM_MODELS:-BossWN-nano BossWN-standard}"
HARNESS="$BUILD/plugins/nam_harness"
RENDER="$WORK/upstream-build/tools/render"

fail() { echo "FAIL: $*" >&2; exit 1; }

mkdir -p "$WORK"

# 1. harness
cmake --build "$BUILD" -j4 --target nam_harness > "$WORK/harness-build.log" 2>&1 \
  || fail "harness build (see $WORK/harness-build.log)"
[ -x "$HARNESS" ] || fail "harness binary not found at $HARNESS"

# 2. deterministic input signal (also exercises the model + prints timing)
"$HARNESS" "$ROOT/models/BossWN-nano.nam" --blocks 2000 --warmup 20 \
  --write-input-wav "$WORK/in.wav" > "$WORK/geninput.log" 2>&1 \
  || fail "input generation (see $WORK/geninput.log)"

# 3. upstream render tool from a pristine pinned worktree
if [ ! -x "$RENDER" ]; then
  [ -d "$UPSTREAM_SRC/.git" ] || fail "upstream clone not found at $UPSTREAM_SRC"
  if [ ! -e "$WORK/upstream/.git" ]; then
    git -C "$UPSTREAM_SRC" worktree add --detach "$WORK/upstream" "$UPSTREAM_COMMIT" \
      || fail "git worktree add"
  fi
  git -C "$WORK/upstream" submodule update --init --recursive || fail "submodules"
  cmake -B "$WORK/upstream-build" -S "$WORK/upstream" -DCMAKE_BUILD_TYPE=Release \
    > "$WORK/upstream-config.log" 2>&1 || fail "upstream configure"
  cmake --build "$WORK/upstream-build" -j4 --target render \
    > "$WORK/upstream-build.log" 2>&1 || fail "upstream render build"
fi

# 4./5. render + compare
rc=0
for m in $MODELS; do
  echo "== $m"
  "$RENDER" "$ROOT/models/$m.nam" "$WORK/in.wav" "$WORK/ref_$m.wav" \
    > "$WORK/render_$m.log" 2>&1 || { echo "  FAIL render (see $WORK/render_$m.log)"; rc=1; continue; }
  out=$("$HARNESS" "$ROOT/models/$m.nam" --wav-run "$WORK/in.wav" \
        --compare-ref "$WORK/ref_$m.wav" --block 512 2>&1)
  hrc=$?
  echo "$out" | grep -E "correlation|max abs|PASS|FAIL" | sed 's/^/  /'
  [ "$hrc" -eq 0 ] || { echo "  FAIL compare exit=$hrc"; rc=1; }
done
exit $rc
