# Cluster review-d589f2288 — Controller time scaling and joining narrow or overflow ticks

Verdict: **FIXED**

Round 1 claimed ALREADY-FIXED and was returned DISPUTE by Luna's review
(`review-d589f2288.r1.md`): the join preflighted each moved note's **start** but not its
**end**. This round closes that gap.

## Finding

`src/core/ControlCommandsClipArrange.cpp` (from Luna's patrol):

> New MIDI controller paths narrow `lround(point.tick*factor)` to tick_t and add
> `point.tick+delta` signed in join, without range preflight. Factor<=8 does not bound
> persisted controller ticks; values near INT32_MAX overflow or narrow incorrectly.
> Validate all final note/controller/clip ticks before checkpoint or writes, reject overflow
> with unchanged project and no undo action. Regress controller point near maximum tick in
> time_scale and join. Do not assume factor bound proves output representable.

Luna's rework instruction: extend the join preflight to compute each moved note's start **and
end** in wide arithmetic and reject unrepresentable results before `addUndoStep`/writes, ensure
the final joined clip length/end is safe before the checkpoint, and add a regression whose moved
start fits but whose moved end does not.

## What was wrong (round-1 gap)

`Note::endPos()` (`include/Note.h:303`) returns `pos() + length()`, an `int` add of the position
and length. A note at `INT32_MAX-200` with length 12, moved by 192, has a representable start
(`INT32_MAX-8`) but its end (`INT32_MAX+4`) wraps. The old `joinedContentFits` checked only
`pos + delta`, so the join stored a wrapped note. `Clip::endPosition()` (`include/Clip.h:128`)
is the same `int` sum, so a later clip whose start+length crossed the top also fed a wrapped
value into the final `changeLength`.

## Fix

`src/core/ControlCommandsClipArrange.cpp`:

- `:242` `wideSongEnd(const Clip*)` — a clip's song end in 64-bit arithmetic.
- `:251` `joinedContentFits(clips, origin, long long* joinedEnd)` now, for every later clip:
  - `:262` checks the moved note's start **and** `moved + note->length()` (the same sum
    `endPos` computes in `int`);
  - keeps the existing moved controller-point check;
  - `:271` tracks the widest clip end and rejects it if unrepresentable.
- `:306` `joinClips` receives that end, and `:331` writes
  `changeLength(static_cast<tick_t>(wideEnd - first->startPosition().getTicks()))`, safe because
  the preflight held `wideEnd` to a storable tick that is `>=` the first clip's start.
- Refusal still happens before `addUndoStep`, so project and journal are untouched.

## Regression

`tests/src/core/ClipJoinRippleTest.cpp:252`
`joiningRefusesANoteEndPastTheLargestTick` — first clip at 0, later clip at 192 with a note at
`INT32_MAX-200` length 12 (moved start `INT32_MAX-8` fits, moved end `INT32_MAX+4` does not).
Asserts `!joined.ok`, `ControlErrorKind::Refused`, unchanged journal `undoDepth()`, two clips
still present, and the note's position/length untouched.

## Evidence table

| Step | Command | Exit |
|---|---|---|
| RED build (test added, fix absent) | `cmake --build build --target ClipJoinRippleTest > "$LOGS/build-1-red.log" 2>&1` | 0 |
| RED test | `ctest -R ClipJoinRippleTest --output-on-failure > "$LOGS/test-1-red.log" 2>&1` (from `build/tests`) | **8** |
| GREEN build | `cmake --build build --target ClipJoinRippleTest zene > "$LOGS/build-2-green.log" 2>&1` | 0 |
| GREEN test | `ctest -R ClipJoinRippleTest --output-on-failure > "$LOGS/test-2-green.log" 2>&1` | **0** |
| 4b product-only patch | `git diff -- src/core/ControlCommandsClipArrange.cpp > "$LOGS/fix.patch"` | 0 |
| 4b reverse product hunks | `git apply -R "$LOGS/fix.patch"` | 0 |
| 4b build reversed | `cmake --build build --target ClipJoinRippleTest > "$LOGS/build-3-reversed.log" 2>&1` | 0 |
| 4b test reversed (must FAIL) | `ctest -R ClipJoinRippleTest --output-on-failure > "$LOGS/test-3-reversed.log" 2>&1` | **8** |
| 4b restore product hunks | `git apply "$LOGS/fix.patch"` | 0 |
| 4b build restored | `cmake --build build --target ClipJoinRippleTest zene > "$LOGS/build-4-restored.log" 2>&1` | 0 |
| 4b test restored (must PASS) | `ctest -R ClipJoinRippleTest --output-on-failure > "$LOGS/test-4-restored.log" 2>&1` | **0** |
| Affected family build | `cmake --build build --target ClipLinkTest RangeEditsTest > "$LOGS/build-5-family.log" 2>&1` | 0 |
| Affected family test | `ctest -R "ClipJoinRippleTest\|ClipLinkTest\|RangeEditsTest" --output-on-failure > "$LOGS/test-5-family.log" 2>&1` | **0** (`100% tests passed out of 3`) |
| Complexity gate | `bash tests/complexity-gate.sh --check > "$LOGS/gate-complexity.log" 2>&1` | **1** |
| File-length gate | `bash tests/file-length-gate.sh --check > "$LOGS/gate-filelength.log" 2>&1` | **1** |

`$LOGS` = `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/review-d589f2288.r2/logs`.

RED assertion (`test-1-red.log:16`):

```
FAIL!  : ClipJoinRippleTest::joiningRefusesANoteEndPastTheLargestTick() '!joined.ok' returned FALSE. ()
   Loc: [../../tests/src/core/ClipJoinRippleTest.cpp(264)]
Totals: 15 passed, 1 failed
```

GREEN: `test-2-green.log` → `100% tests passed out of 1` (16 test functions: 14 tests +
`initTestCase`/`cleanupTestCase`).

4b reversed run (`test-3-reversed.log`) fails on the same assertion, proving the test catches the
product bug; the restored run (`test-4-restored.log`) passes.

## Gates (observed; pre-existing red, unrelated to this cluster)

Both gates are red at this branch, exactly as round 1 observed before any change here:

- `gate-complexity.log` reports `REGRESSION` on `ControlCommandsMixerTools.cpp`,
  `ControlCommandsMixerStrip.cpp`, `DawProjectReadTracks.cpp`, `XyPadPairTest.cpp`,
  `ProjectRevisions.cpp`, `ControlCommandsShortcuts.cpp` — none touched by this cluster.
  `ControlCommandsClipArrange.cpp` (the only product file changed) is **not** listed, so the
  edited `joinedContentFits` stays within the CCN <= 10 target.
- `gate-filelength.log` reports new >500-line files (`SmfInterchangeReader.cpp`, `TakeLane.cpp`,
  `DeviceDspTest.cpp`, …) — all unrelated to this cluster; the changed files
  (`ControlCommandsClipArrange.cpp`, `ClipJoinRippleTest.cpp`) are not listed.

The ratchet baselines were not re-anchored and no gate limit was changed.

## Files committed

- `src/core/ControlCommandsClipArrange.cpp`
- `tests/src/core/ClipJoinRippleTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-review-d589f2288.md` (this report)

Both changed source files are already registered in `tests/fork-sources.txt:1329` and `:2225`.
