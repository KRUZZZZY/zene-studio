FIXED

# cluster new-p8-211 - adjacent comp segments for the same lane cause an audible dip

## Verdict and evidence

REAL. The bug is reachable through `comp.rebuild`: `TakeLaneModel::rebuild()` clamps the
composite to the requested span and then calls `fillGaps()`, which fills each hole with the base
lane at slip 0. `fillGaps()` did not merge a filled gap into a base-lane segment that touched it,
so a composite could end up with two contiguous, same-lane, same-slip segments even though
`mergeAdjacentSegments()` exists for exactly that case (`src/core/TakeLane.cpp:81-103`).

Playback then snapshot every segment as its own span
(`src/core/SamplePlayHandle.cpp:375-384`) and `applyComp()` ramps at both edges of every span
(`src/core/SamplePlayHandle.cpp:387-406`, `CompRampFrames = 64` at
`include/SamplePlayHandle.h:265`). Two adjacent spans of one lane therefore fade down and back up
at the tick where nothing about the take changed - the audible dip.

The other authoring paths already normalise: `selectSegment()` calls `sortSegments()` +
`mergeSegments()` (`src/core/TakeLane.cpp:412-414`), `loadSettings()` does the same
(`src/core/TakeLanePersistence.cpp:161-162`), and `removeLane()` merges through
`segmentsAfterRemovingLane()` (`src/core/TakeLane.cpp:123`). Only `rebuild()`/`fillGaps()` was
left out. This is why the cluster's "needs context" reading was easy to miss: the invariant was
believed everywhere but not actually held after a span rebuild.

The fix is at the cause: `fillGaps()` now merges after filling (`src/core/TakeLane.cpp:460-467`).
It is a main/control-thread model edit only; `applyComp()` and the fixed-size render snapshot are
unchanged, so nothing on the audio thread allocates, locks or logs.

A different slip between two same-lane neighbours stays a seam and is intentionally not merged
(docs/COMPING.md:68-70: "Same lane, same slip, contiguous = one read (merged); a different slip
is a seam"). That is not this bug and is left exactly as it was.

## Regression test

`tests/src/tracks/TakeLaneCompPlaybackTest.cpp`:
`rebuildMergesFilledGapsIntoTheBaseLaneSoItDoesNotDip()`

It builds a lane-0 selection `[0,Q)`, a lane-1 selection `[2Q,3Q)`, and calls
`rebuild(0, clipLength)`. The rebuild fills `[Q,2Q)` with the base lane, which used to remain a
second base-lane span. The test renders the real `SamplePlayHandle` and measures the mean level
in a 256-frame window centred on tick `Q` (clear of the 64-frame ramps):

- without the fix: `REBUILD_DIP_EVIDENCE at Q(=56) 0.3770` - the dip;
- with the fix: `REBUILD_DIP_EVIDENCE at Q(=56) 0.5000`.

It also asserts the normalised model: `[0,2Q)` is one base-lane segment and the rebuild returns 3
segments instead of 4.

Test matrix:

- normal path: gap filled next to an existing base-lane span merges into one span; audio is flat
  across the former shared edge.
- failure path: the unmerged state produces the 0.3770 measured dip at the shared tick.
- exhaustion: the 256-span per-lane budget path is unchanged; the existing
  `refusesRebuildAndLaneRemovalBeyondThePlaybackSpanBudget()` still passes (the merge can only
  reduce a count, never hide an over-budget result).
- reconfiguration: `selectSegment`, `removeLane` and `loadSettings` already merged; they are
  covered by the passing `TakeLaneCompTest`/`TakeLaneTest` siblings.
- audio thread: no render-path code changed; `applyComp` remains allocation-free and bounded.
- platforms: the change is plain C++20 model code in `src/core`, with no platform-specific
  branch; built and tested on this Linux worktree.

## Commands and exit codes

All output is in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p8-211.r1/logs/`.

RED (test added, product code unchanged):

```
cmake --build build --target TakeLaneCompPlaybackTest -j4 > .../logs/build-1-red.log 2>&1
EXIT=0
cd build/tests && ctest -R '^TakeLaneCompPlaybackTest$' --output-on-failure > .../logs/test-1-red.log 2>&1
EXIT=8
```

`test-1-red.log`:

```
REBUILD_DIP_EVIDENCE at Q(=56) 0.3770
FAIL!  : TakeLaneCompPlaybackTest::rebuildMergesFilledGapsIntoTheBaseLaneSoItDoesNotDip() 'std::fabs(around - 0.5) < 1e-4' returned FALSE. (the base lane dips at the filled gap's shared edge: 0.376953)
Totals: 15 passed, 1 failed, 0 skipped, 0 blacklisted, 505ms
```

GREEN (fix in place):

```
cmake --build build --target TakeLaneCompPlaybackTest -j4 > .../logs/build-2-green.log 2>&1
EXIT=0
cd build/tests && ctest -R '^TakeLaneCompPlaybackTest$' --output-on-failure > .../logs/test-2-green.log 2>&1
EXIT=0
```

Sibling family, rebuilt and run together:

```
cmake --build build --target TakeLaneCompTest TakeLaneTest LanesUpconvertTest TakeLaneCompPlaybackTest -j4 > .../logs/build-3-family.log 2>&1
EXIT=0
cd build/tests && ctest -R '^(TakeLaneCompTest|TakeLaneTest|LanesUpconvertTest|TakeLaneCompPlaybackTest)$' --output-on-failure > .../logs/test-3-family.log 2>&1
EXIT=0
```

`test-3-family.log`: `100% tests passed out of 4` (TakeLaneCompTest, TakeLaneTest,
LanesUpconvertTest, TakeLaneCompPlaybackTest).

Step 4b proof (reverse only the product change, keep the test):

```
git diff -- src/core/TakeLane.cpp > .../logs/fix.patch          EXIT=0
git apply -R .../logs/fix.patch                                  EXIT=0
cmake --build build --target TakeLaneCompPlaybackTest -j4 > .../logs/build-4-red-without-fix.log 2>&1
EXIT=0
cd build/tests && ctest -R '^TakeLaneCompPlaybackTest$' --output-on-failure > .../logs/test-4-red-without-fix.log 2>&1
EXIT=8
git apply .../logs/fix.patch                                     EXIT=0
cmake --build build --target TakeLaneCompPlaybackTest -j4 > .../logs/build-5-green-restored.log 2>&1
EXIT=0
cd build/tests && ctest -R '^TakeLaneCompPlaybackTest$' --output-on-failure > .../logs/test-5-green-restored.log 2>&1
EXIT=0
```

`test-4-red-without-fix.log` repeats the 0.3770 dip and the failing assertion. The single-case
green run with the evidence line is `.../logs/test-6-green-case.log`:

```
QT_QPA_PLATFORM=offscreen ./TakeLaneCompPlaybackTest rebuildMergesFilledGapsIntoTheBaseLaneSoItDoesNotDip
EXIT=0
REBUILD_DIP_EVIDENCE at Q(=56) 0.5000
```

Gates:

```
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > .../logs/gates-1.log 2>&1
EXIT=0
RESULT: PASS
(fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology,
 doc-figures, complexity, file-length all PASS)
```

Registration: `src/core/TakeLane.cpp` is fork-NEW (`tests/fork-sources.txt`), so no upstream
ledger entry is needed. The changed regression file is already declared and gained a
cluster-specific reason in `tests/upstream-modifications.txt`.

## Completion check

See the `Self-check` section below for the `submit_check.py` result and each answer.

## Self-check

Answers to every `?` item printed by
`python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p8-211`
(saved at `.../logs/submit-1.log`; the only FAIL there was the uncommitted working tree, which the
commit below clears):

- **The regression test FAILS with the product change reverted and PASSES with it (exit codes).**
  Step 4b: `git apply -R fix.patch` then build EXIT=0 and
  `ctest -R '^TakeLaneCompPlaybackTest$'` EXIT=8 (`test-4-red-without-fix.log`, dip 0.3770);
  `git apply fix.patch` then build EXIT=0 and the same ctest EXIT=0
  (`test-5-green-restored.log`). The evidence line is `REBUILD_DIP_EVIDENCE at Q(=56) 0.5000` in
  `test-6-green-case.log` (EXIT=0).
- **The test is deterministic: no wall-clock timing, sleeps or races.** It reads rendered sample
  values from the real `SamplePlayHandle` at frame indices computed from ticks with
  `Engine::framesPerTick(m_rate)`; there is no timer, sleep, thread or wall-clock input. The only
  injected value is the fixed composite (`[0,Q)` lane 0, `[2Q,3Q)` lane 1, rebuild to clip end)
  and the constant 0.5 take sample created by `makeTake`.
- **The fix is at the cause and complete.** Cause: `fillGaps()` inserted base-lane gaps without
  merging; fixed at `src/core/TakeLane.cpp:460-467`. Its only caller is `rebuild()`
  (`src/core/TakeLane.cpp:470-502`), which now returns the merged count and budget-checks the
  merged list. The siblings were checked: `selectSegment` (`TakeLane.cpp:395-416`),
  `loadSettings` (`TakeLanePersistence.cpp:146-163`) and `removeLane`
  (`TakeLane.cpp:328-341` via `segmentsAfterRemovingLane`, `TakeLane.cpp:107-125`) already merge
  in the same `(lane, slip)` rule; no other writer of `m_segments` exists.
- **No collateral damage.** Project files are written and read by the untouched persistence code;
  `loadSettings()` already sorted and merged before this change, so old projects load exactly as
  before. No limit or refusal changed: the merge can only shrink a segment list, and
  `refusesRebuildAndLaneRemovalBeyondThePlaybackSpanBudget()` still refuses the 257-span result
  (it passes in `test-3-family.log`). Save format, defaults and every non-comp path are unchanged.
- **ALREADY-FIXED/FALSE-POSITIVE evidence.** Not applicable: the verdict is FIXED. (For contrast,
  the already-correct sibling behaviour is documented at `docs/COMPING.md:68-70` - different-slip
  neighbours are a deliberate seam and stay unmerged.)
- **Sibling commands in the command files you changed.** The diff touches no command file at all
  (`git diff --name-only` is `src/core/TakeLane.cpp`,
  `tests/src/tracks/TakeLaneCompPlaybackTest.cpp`, `tests/upstream-modifications.txt`, plus the
  report). The `app.*`/`arrangement.*` list in the ask comes from other lanes' changes already on
  this train branch (the gate reports 351 changed files against the base), not from this fix.
  `comp.rebuild` is the one command whose result improves: it reports `segments_after` from
  `model.rebuild()`, which is now the normalised count, and its single journal checkpoint and
  true-inverse path (`ControlCommandsCompEdits.cpp:235-258`) is unchanged.

Also: no GUI change (`src/core` only), so no screenshot/recording is required. `src/core/TakeLane.cpp`
is not matched by the `Song*`, `*Record*`, `*Undo*`, `*Transport*`, `Take*` or `AudioEngine*`
state-sequence rule, so no state-sequence artifact is required.

## Other findings

- Same-lane neighbours with a *different* slip remain two spans and still ramp at their seam.
  That is the documented seam (`docs/COMPING.md:68-70`), not this bug; left unchanged.

