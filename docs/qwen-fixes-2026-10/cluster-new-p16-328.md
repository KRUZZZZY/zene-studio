# Cluster new-p16-328

**Verdict: FIXED** — `INT_MIN` tick amounts reached `std::abs(int)` (undefined) in the audio-loop tick conversion.

## What was real

`src/core/ClipAudioLoop.cpp` (before this fix, lines 50-56) took the tick count as `int` and called
`TimePos(std::abs(ticks))`. `std::abs(INT_MIN)` has no `int` result, so the helper was undefined for that input.
The finding's `withLoop()` and `rotated()` both reach that helper (`framesFor`, called at the old lines 111-112 and
169).

The defect is real at the C++ level, but the control surface cannot deliver `INT_MIN`:

- `clip.set_loop` bounds `start` and `length` to `integerProperty(0, 192 * 256)`
  (`src/core/ControlCommandsClipEnvelope.cpp:295-296`).
- `note.rotate` bounds `amount` to `integerProperty(-192 * 256, 192 * 256)` (`src/core/ControlCommandsNoteTools.cpp:472`).
- Schema validation runs before the handler (`src/core/ControlRegistry.cpp:277`), and enforces `minimum` and `maximum`
  (`src/core/ControlSchema.cpp:87-88` and `:93-94`).

So the reachable callers are direct C++ callers only: `audioloop::withLoop` (called from
`ControlCommandsClipEnvelope.cpp:211`) and `audioloop::rotated` (called from `ControlCommandsNoteTools.cpp:308`).

Observed on the unfixed tree (see RED below): `withLoop(clip, INT_MIN, 16)` **accepted** a loop at the in-point, and
`rotated(clip, INT_MIN)` reported **no turn** (`turned == false`). The correct behaviour is a refusal for the start, and
a turn by 2^31 ticks wrapped into the loop for the rotation.

## The fix

`src/core/ClipAudioLoop.cpp`:

- `framesFor` now takes `std::int64_t` ticks and takes the magnitude in 64 bits (`src/core/ClipAudioLoop.cpp:53-62`).
  The frame product and its guards are those of `TimePos::frames` (non-finite, negative rate, and frame-counter
  overflow all give 0). For every `int` input the result is identical to before.
- `elapsedFrames` widens its origin and tick difference to 64 bits (`src/core/ClipAudioLoop.cpp:86-93`). Same class of
  defect; identical results for in-range values.
- `withLoop` refuses a loop whose absolute start is before the clip's audio (`start < 0`,
  `src/core/ClipAudioLoop.cpp:122-125`). Before, a negative absolute start was stored as a negative loop start.

`include/ClipAudioLoop.h:70-72`: the comment now names the new refusal (comment only; declarations unchanged).

Choice taken: widen rather than reject `INT_MIN`. A rotation by `-2^31` ticks has a well-defined wrapped meaning, and
widening keeps every other value bit-identical. "Reject INT_MIN" was not taken.

## Regression test

`tests/src/core/AudioClipLoopTest.cpp` (existing file, registered in `tests/fork-sources.txt:2350`; no new file):

- `aLoopStartingAtIntMinIsRefusedNotTakenAtTheInPoint` — `withLoop(INT_MIN start, 16 length)` must return false, set
  an error, and leave the clip without a loop.
- `aRotationByIntMinTurnsByItsWidenedMagnitude` — on a 48-tick clip (loop of 48 ticks), `rotated(INT_MIN)` must turn, with
  offset `magnitude % span`, where `magnitude = 2^31 ticks` in frames at the clip's own rate.

Both call `audioloop::` directly because the control schema refuses `INT_MIN` before the handler. They use literal
inputs and the clip's own `clipFramesPerTick()`; no rendering, timers, sleeps or threads.

Two faults in my first draft of the rotation test were corrected before the proof: a 64-tick loop divides 2^31 ticks
exactly (2^31 mod 64 = 0), so the turn is zero even with the fix; and the offset sign for a negative amount was
inverted (a rotation by A plays `(t - A) mod loop`, so A = -2^31 gives offset `+magnitude`).

## Evidence (commands and exit codes)

All logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-328.r1/logs/`.

| Step | Command (run from the worktree or `build/tests` as noted) | EXIT | Log |
| --- | --- | --- | --- |
| Build, first try | `cmake --build build --target AudioClipLoopTest -j2` | 2 | build-1.log (ccache could not write its temp file under the sandbox, and the object step returned an error on the same run; the same source built cleanly with ccache disabled) |
| Build, ccache off | `CCACHE_DISABLE=1 cmake --build build --target AudioClipLoopTest -j2` | 0 | build-2.log |
| **RED** (fix not yet applied, test as first written) | `ctest -R '^AudioClipLoopTest$' --output-on-failure` in `build/tests` | 8 | test-1-red.log: 10 passed, 2 failed (the two new slots) |
| Intermediate: fix applied, rotation test on a 64-tick loop | same ctest | 8 | test-2-green.log: `turned` false (2^31 mod 64 = 0, so no turn is correct); test design error, corrected |
| Intermediate: 48-tick loop, offset sign wrong | same ctest | 8 | test-3-green.log: offset 12600 vs expected 6300; sign error in the test, corrected |
| GREEN, first full pass | same ctest | 0 | test-4-green.log |
| **Proof, RED**: `git diff -- src/core/ClipAudioLoop.cpp include/ClipAudioLoop.h > fix-final.patch && git apply -R fix-final.patch`, rebuild, run | `git apply -R` EXIT 0; build EXIT 0; ctest EXIT **8** | 8 | test-10-final-RED.log: `FAIL! aLoopStartingAtIntMinIsRefusedNotTakenAtTheInPoint … returned FALSE`; `FAIL! aRotationByIntMinTurnsByItsWidenedMagnitude … 'turned' returned FALSE`; Totals 10 passed, 2 failed |
| **Proof, GREEN**: `git apply fix-final.patch`, rebuild, run | `git apply` EXIT 0; build EXIT 0; ctest EXIT **0** | 0 | test-11-final-GREEN.log |
| Affected family, final tree | `ctest -R '^(AudioClipLoopTest\|ClipEditsTest\|LinkedClipEditsTest\|SampleClipWindowTest\|SampleClipWarpTest)$' --output-on-failure` after `cmake --build build --target zene AudioClipLoopTest ClipEditsTest LinkedClipEditsTest SampleClipWindowTest SampleClipWarpTest -j2` (EXIT 0) | 0 | test-9-final-family.log: 100% of 5 |
| Slot count, final tree, `AudioClipLoopTest` | `ctest -R '^AudioClipLoopTest$' -V` | 0 | test-12-final-verbose.log: 12 PASS lines (initTestCase, cleanupTestCase, 10 slots), 0 FAIL |

Affected-family test count: **5 executed, 5 passed**. The other family members are built only on demand in this tree,
so I built the four that exercise clip edits and windows. The other `ClipAudio…`/`SampleClip…` binaries were not built.

### Gates

`feature_check.py` (`python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>`) **could not run here**:
it writes its logs to `<worktree>-feature-check`, a sibling directory outside the write-jail
(`feature_check.py:71`, `PermissionError`). I ran each of its checks directly with the same command, from the worktree,
with logs in the LOG FOLDER:

| Check (from feature_check's lists) | Command | EXIT |
| --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 (my files listed as fork-NEW (allowed) / tests (allowed)) |
| check-namespace | `tests/scripted/check-namespace` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 — 6 REGRESSION lines; **none name `ClipAudioLoop` or `AudioClipLoopTest`**, so none are new in a file this branch changed |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 — 23 REGRESSION lines; **none name my files** |

Logs: gate-3-1.log … gate-3-8.log. The two ratchet rows are the red-on-train items that feature_check passes when
nothing it lists is new in a changed file; by name-match, nothing is.

Full `feature_check.py` table: **not produced** (see above). The loop should run it in a location where it can write.

## Behaviour outside the bug

- Control surface: unchanged. `start`/`length` are 0..49152 and `amount` is ±49152 by schema, so every value the
  control surface can deliver gives the same frames as before (the widened product is the same float product).
- Negative absolute loop start: only direct C++ callers can produce it. Those were accepted and stored a negative loop
  start; they are now refused. Saved projects are not affected: `withLoop` is called only from `setAudioLoop`
  (`ControlCommandsClipEnvelope.cpp:211`), and loading restores `ClipEdits` without calling it.
- No new control command, no undo or journal change, no new allocation, lock or log on any path.

## Self-check

1. **Regression FAILS with the change reverted, PASSES with it** — yes. RED: `git apply -R fix-final.patch` (EXIT 0), rebuild
   (EXIT 0), `ctest -R '^AudioClipLoopTest$'` **EXIT 8** with the two FAIL lines above. GREEN: `git apply fix-final.patch`
   (EXIT 0), rebuild (EXIT 0), same ctest **EXIT 0**. Details in the Evidence table.
2. **Deterministic** — yes. Inputs are literal ticks (`INT_MIN`, 16, 48, 64) on a clip built in memory; the expected
   offset uses the clip's own `clipFramesPerTick()`. No rendering, timers, sleeps, threads or randomness. The two slots
   do not call the render path.
3. **Fix at the cause; callers and siblings checked** — `framesFor` callers: `elapsedFrames`, `withLoop` (start and
   length), `rotated` (amount). `elapsedFrames` is called by `window()` (used from `SampleTrack.cpp:234`), `rotationLoop`,
   and `rotated`. All are int-range inputs and keep the same results. Sibling `origin`/tick difference in
   `elapsedFrames` widened in the same way. The other entry to this code is the audio thread (`SampleTrack.cpp:234`);
   the new code adds only float arithmetic and branches there, with no allocation, lock or logging.
4. **No collateral damage** — the new refusal (`start < 0`) only covers absolute loop starts before the audio, which the
   control schema forbids (`start` minimum 0) and which only direct C++ callers could produce. Saved projects load through
   `ClipEdits` restore, not `withLoop`, so they are unaffected. No other caller's limit changed.
5. **Verdict is FIXED**, so no ALREADY-FIXED or FALSE-POSITIVE evidence is needed.
