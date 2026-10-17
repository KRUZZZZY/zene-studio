# Cluster review-d589f2288 — Controller time scaling and joining narrow or overflow ticks

Verdict: **ALREADY-FIXED**

## Finding

`src/core/ControlCommandsClipArrange.cpp` (from Luna's patrol):

> New MIDI controller paths narrow `lround(point.tick*factor)` to tick_t and add
> `point.tick+delta` signed in join, without range preflight. Factor<=8 does not bound
> persisted controller ticks; values near INT32_MAX overflow or narrow incorrectly.
> Validate all final note/controller/clip ticks before checkpoint or writes, reject overflow
> with unchanged project and no undo action. Regress controller point near maximum tick in
> time_scale and join.

The previous attempt on this cluster was returned REWORK. That rework is what landed on this
branch, and the resulting guard is present at HEAD. The cluster is therefore already fixed;
no further product change is warranted.

## Evidence that the fix is on this branch (`de5a85a96`)

The fix commits are all ancestors of HEAD (`git merge-base --is-ancestor <c> HEAD` = true):

```
eff316eda fix(rework d589f228): controller points past the top tick wrapped when a clip was scaled or joined
7aa28d288 fix(rework d589f228): time scale wrapped note positions, note lengths and the clip length past the top tick
5a4e0597d fix(rework d589f228): join wrapped a later clip's note positions past the top tick
63388259e fix(rework 5a4e0597d): join's note move overflowed tick_t in its middle term
87b3dd21e docs(rework d589f228): say that scaleFits is the single guard for the scale's narrowing casts
```

The current code holds all of the requested validation, in wide arithmetic, before the
checkpoint and before any write:

- `src/core/ControlCommandsClipArrange.cpp:105` `fitsTick(long long)` — `0 <= tick <= INT32_MAX`.
- `src/core/ControlCommandsClipArrange.cpp:111` `scaledControllersFit` — every controller point, `std::llround((double)point.tick * factor)`.
- `src/core/ControlCommandsClipArrange.cpp:129` `scaleFits` — clip length, every note `pos` and `pos+length`, then `scaledControllersFit`.
- `src/core/ControlCommandsClipArrange.cpp:172` `timeScale` calls `scaleFits` **before** `addJournalCheckPoint()` / any write; refusal is `ControlErrorKind::Refused`.
- `src/core/ControlCommandsClipArrange.cpp:242` `joinedContentFits` — every later-clip note `pos+delta` and every controller point `tick+delta`, wide arithmetic.
- `src/core/ControlCommandsClipArrange.cpp:291` `joinClips` calls `joinedContentFits` **before** `addUndoStep()` / any move; refusal is `ControlErrorKind::Refused`.

`tick_t` is `std::int32_t` (`include/LmmsTypes.h:35`), so the `INT32_MAX` bound is the correct one.

## Regression tests already present (exactly the requested cases)

`tests/src/core/ClipJoinRippleTest.cpp`:

- `:185` `scalingRefusesAControllerPointPastTheLargestTick` — controller point at `INT32_MAX`, `clip.time_scale factor 2` → refused, point and clip length unchanged.
- `:250` `joiningRefusesAControllerPointPastTheLargestTick` — later clip's point at `INT32_MAX-100`, join moves it past the top → refused, clips/notes unchanged.
- plus `:141` note scale, `:157` note-length scale, `:171` clip-length scale, `:231` note join.

"No existing test includes this file's header" holds (the test drives the registry by command id,
not the header), but the area already has a test, so no new duplicate test was added.

## RED/GREEN proof (the tests catch the bug)

The product change for this cluster is the diff `eff316eda~1..HEAD` of the one product file.
Applying it in reverse leaves the tests in place but removes the guards.

RED — guards removed, tests must fail:

```
$ git diff eff316eda~1 HEAD -- src/core/ControlCommandsClipArrange.cpp > "$LOGS/fix.patch"
$ git apply -R "$LOGS/fix.patch"; echo APPLY_R_EXIT=$?
APPLY_R_EXIT=0
$ cmake --build build --target ClipJoinRippleTest > "$LOGS/build-2-red.log" 2>&1; echo BUILD_EXIT=$?
BUILD_EXIT=0
$ (cd build/tests && ctest -R ClipJoinRippleTest --output-on-failure > "$LOGS/test-2-red.log" 2>&1); echo CTEST_EXIT=$?
CTEST_EXIT=8
```

The failing assertions (RED log, 6 of 15 test functions):

```
FAIL!  : ClipJoinRippleTest::scalingRefusesANotePastTheLargestTick() '!scaled.ok' returned FALSE. ()
FAIL!  : ClipJoinRippleTest::scalingRefusesANoteLengthPastTheLargestTick() '!scaled.ok' returned FALSE. ()
FAIL!  : ClipJoinRippleTest::scalingRefusesAClipLengthPastTheLargestTick() '!scaled.ok' returned FALSE. ()
FAIL!  : ClipJoinRippleTest::scalingRefusesAControllerPointPastTheLargestTick() '!scaled.ok' returned FALSE. ()
FAIL!  : ClipJoinRippleTest::joiningRefusesANotePastTheLargestTick() '!joined.ok' returned FALSE. ()
FAIL!  : ClipJoinRippleTest::joiningRefusesAControllerPointPastTheLargestTick() '!joined.ok' returned FALSE. ()
```

GREEN — guards restored, tests pass:

```
$ git apply "$LOGS/fix.patch"; echo APPLY_EXIT=$?
APPLY_EXIT=0
$ cmake --build build --target ClipJoinRippleTest zene > "$LOGS/build-3-green.log" 2>&1; echo BUILD_EXIT=$?
BUILD_EXIT=0
$ (cd build/tests && ctest -R ClipJoinRippleTest --output-on-failure > "$LOGS/test-3-green.log" 2>&1); echo CTEST_EXIT=$?
CTEST_EXIT=0
```

Green log tail: `100% tests passed out of 1` / `Total Test time (real) = 1.48 sec`.

Affected family: `ctest -R ClipJoinRippleTest` — 1 executable, 15 test functions
(13 tests plus `initTestCase`/`cleanupTestCase`). 9 passed, 6 failed in RED; 15/15 pass in GREEN.
No other test in the family regressed.

## Gates (as observed; pre-existing red at this commit, not introduced here)

```
$ bash tests/complexity-gate.sh --check > "$LOGS/gate-complexity.log" 2>&1; echo EXIT=$?
EXIT=1
$ bash tests/file-length-gate.sh --check > "$LOGS/gate-file-length.log" 2>&1; echo EXIT=$?
EXIT=1
```

Both gates are already red at HEAD `de5a85a96` with **no change from this cluster**: the
regression lines name files this cluster never touches (`ControlCommandsMixerTools.cpp`,
`ControlCommandsMixerStrip.cpp`, `DawProjectReadTracks.cpp`, `XyPadPairTest.cpp`,
`SmfInterchangeReader.cpp`, `TakeLane.cpp`, …). This is repo drift in the ratchet baselines,
not a product regression from the finding:

- `git diff --stat 87b3dd21e HEAD -- tests/fork-sources.txt tests/complexity-baseline.tsv`
  shows `tests/fork-sources.txt` growing by ~550 entries and `tests/complexity-baseline.tsv`
  losing 2 lines, with `tests/file-length-baseline.tsv` untouched — so ~14 files crossed 500
  code lines without a corresponding baseline entry.
- The identical gates fail at the same commit in an independent checkout of this repo
  (`zene-cachyos` @ `de5a85a96`): `bash tests/file-length-gate.sh --check` → `EXIT=1`.
- Per the rules, a gate is never re-anchored and gate limits/baselines were not touched. The
  failure is unrelated to `ControlCommandsClipArrange.cpp`, which no gate flags.

## Files committed

- `docs/qwen-fixes-2026-10/cluster-review-d589f2288.md` (this report) — no product or test file
  was changed, because the fix already exists on the branch.

`git status` is clean after the commit.
