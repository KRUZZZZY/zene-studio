Verdict: FIXED

new-p10-290: `pitchAt()` bounds check can wrap and allow out-of-bounds reads (finding "BUG 10", historical line 82; the check is now at `src/core/PitchTrack.cpp:87`).

## Evidence

**Real defect.** Before the fix, the check was `frame + static_cast<std::size_t>(size) > count` (`src/core/PitchTrack.cpp:86` at HEAD). `frame` is a `std::size_t`, so for a frame near `SIZE_MAX` the sum wraps to a small value, passes the check, and the reads `mono[frame + i]` and `mono[frame + i + tau]` (`src/core/PitchTrack.cpp:93`) land before the buffer. The RED run below shows the wrapped window producing a pitch (110.2548 Hz) where the correct answer is 0.

**Reachability (honest scope).** `pitchAt` is exported (`include/PitchTrack.h:53`, "Exposed for tests"). Both current callers pass bounded frames, so no production path reaches the wrap today:
- `detectNotes` calls it at `src/core/PitchTrack.cpp:235` only inside `for (frame = 0; frame + Window <= low.size(); frame += Hop)` (`src/core/PitchTrack.cpp:231`).
- `src/core/ControlCommandsTuner.cpp:83` passes `frame = 0`.

So this is a latent defect in the function's own contract (validate `frame` and `size` against `count`), fixed at the cause as the finding recommends.

**The fix** (`src/core/PitchTrack.cpp:86-87`):
```cpp
// frame + size is not compared directly: a large frame would wrap that sum past count.
if (maxLag < 4 || frame > count || static_cast<std::size_t>(size) > count - frame) { return 0.f; }
```
- `maxLag < 4` goes first, so `size >= 8` before the `static_cast<std::size_t>` (a negative `size` cannot reach it).
- `frame > count` is checked before `count - frame`, so the subtraction cannot underflow.
- For every non-wrapping input the accepted set is unchanged: with `frame <= count`, `frame + size > count` is exactly `size > count - frame`. Only the wrap case changes, and that case is the bug.

**Sibling check (reasoned, not separately tested).** `rms()` (`src/core/PitchTrack.cpp:48`) and `onsetInWindow()` (`src/core/PitchTrack.cpp:121`) also add `frame + size` unchecked. I left them as they are: `rms()` bounds its loop by `end = min(count, ...)` and returns 0 when `end <= frame`, which is what a wrapped sum yields; `onsetInWindow()` only reaches `rms()`, and `detectNotes` (its only caller) passes bounded frames. `periodicityError()` and `pitchBoundaryInWindow()` are reached only from `detectNotes` with bounded frames. None of this is changed by the fix.

**Regression location.** The regression is `TunerTapTest::pitchAtRejectsAWindowWhoseFrameWrapsPastTheEnd` (`tests/src/core/TunerTapTest.cpp:124-139`). The first attempt put it in `tests/src/core/AudioAnalysisTest.cpp`, the file the finding suggested. That pushed the file to 510 code lines (HEAD: 493; the gate's limit is 500), so `file-length` failed (`gate-1-feature-check.log`). I reverted that, and tried a new test file, but `cmake -S . -B build` is refused in this lane (`cmake-1-reconfigure.log`: "bunny-guard: configuring is the parent's job"), so the new target could not be built. The tuner test is the next-best fit: the tuner is `pitchAt`'s only production caller (`ControlCommandsTuner.cpp:79-83`). It is not an ideal home: a `PitchTrack` test file would be better, and the parent can move it when it reconfigures.

**Determinism.** The test uses no clock, sleep or thread. Its input is a synthetic 100-sample sine in a `std::vector` with 16 samples of padding before the span (`storage.data() + pad`), so a wrapped read (`frame + i` mod 2^64 maps to `i - 11`) still lands in allocated memory and sees the same periodic tone. The test itself never reads outside its allocation; only the buggy code does.

**Sanity assertions** in the test: `pitchAt(mono, count, 0, 1024, rate) > 0` and `pitchAt(mono, count, count - 1024, 1024, rate) > 0`. These show that the tone has a pitch and that a window ending exactly at `count` is still accepted, so the final `0` is meaningful.

## Commands and exit codes

Build environment note: ccache's temp dir (set by the environment) is not writable from this lane, so the build failed with `ccache: error: failed to create temporary file` (`build-1-red.log`, EXIT=2). Every later build redirected only the temp directory with `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp/ccache-tmp`. No ccache config was changed.

Attempt A (test in `AudioAnalysisTest.cpp`, later reverted): RED and GREEN were run as follows. The file was reverted for the file-length reason above.

| Step | Command | EXIT | Log |
|---|---|---|---|
| A-RED build | `CCACHE_TEMPDIR=... cmake --build build --target zene AudioAnalysisTest -j4` | 0 | `build-2-red.log` |
| A-RED test | `cd build/tests && ctest -R '^AudioAnalysisTest$' --output-on-failure` | 8 | `test-2-red.log` (`FAIL!` at `AudioAnalysisTest.cpp:259`, Actual 110.2548 Hz, Expected 0) |
| A-GREEN build | same build command, with the fix applied | 0 | `build-3-green.log` |
| A-GREEN test | `ctest -R '^AudioAnalysisTest$'` | 0 | `test-3-green.log` (24/24 at that point) |
| A-4b red | `git diff -- src/core/PitchTrack.cpp > fix.patch && git apply -R fix.patch`, rebuild, run binary | 1 | `build-4-4b-red.log`, `test-4-4b-red.log` (FAIL, 110.2548 Hz) |
| A-4b green | `git apply fix.patch`, rebuild, run binary | 0 | `build-5-4b-green.log`, `test-5-4b-green.log` (24 passed), `test-6-ctest-green.log` (ctest EXIT=0) |

Final regression (in `TunerTapTest.cpp`):

| Step | Command | EXIT | Log |
|---|---|---|---|
| RED build | `CCACHE_TEMPDIR=... cmake --build build --target zene TunerTapTest -j4` (product fix not applied) | 0 | `build-8-red-tuner.log` |
| RED | `cd build/tests && ctest -R '^TunerTapTest$' --output-on-failure` | 8 | `test-8-red-tuner.log` (`FAIL!` at `tests/src/core/TunerTapTest.cpp:139`: Actual `pitchtrack::pitchAt(mono, count, wrapped, 1024, rate)` = 110.2548, Expected 0) |
| GREEN build | `git apply fix.patch`, then build `zene TunerTapTest AudioAnalysisTest` | 0 | `build-9-green-tuner.log` |
| GREEN | `ctest -R '^(TunerTapTest\|AudioAnalysisTest)$'` | 0 | `test-9-green-tuner.log` (2/2 passed) |
| 4b red | `git diff -- src/core/PitchTrack.cpp > fix.patch`, `git apply -R fix.patch`, build, `ctest -R '^TunerTapTest$'` | 8 | `build-10-4b-red.log`, `test-10-4b-red.log` (FAIL at `TunerTapTest.cpp:139`) |
| 4b green | `git apply fix.patch`, build, `ctest -R '^TunerTapTest$'` | 0 | `build-11-4b-green.log`, `test-11-4b-green.log` (1/1 passed) |

Executed-case counts, with the final tree (`build/tests`, run directly so that QTest reports each case):
- `TunerTapTest`: EXIT=0, `Totals: 5 passed, 0 failed` (`count-TunerTapTest.log`).
- `AudioAnalysisTest`: EXIT=0, `Totals: 23 passed, 0 failed` (`count-AudioAnalysisTest.log`). The file is unchanged from HEAD.

Gates:
- `feature_check` as the stock script was invoked: `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` failed before running any gate with `PermissionError` on `mkdir` of `<worktree>-feature-check`, a directory outside the write-jail. That output was printed to the terminal and not saved to a log.
- The gate was then run from a verbatim copy of the script in a scratch directory (`~/.cache/zene-tmp/fc-new-p10-290/feature_check_relocated.py`). The only edits: the log directory was moved into this LOG FOLDER, and `ROOT` was pointed at the original tool folder so FIX-BASE and the baselines are still read from there. No check, limit or baseline was changed.
- Run 1 (with the test in `AudioAnalysisTest.cpp`): EXIT=1, `file-length` FAIL (`NEW: new file over 500 lines: tests/src/core/AudioAnalysisTest.cpp (510)`), `gate-1-feature-check.log`.
- Run 2 (final tree): `python3 -I .../feature_check_relocated.py <worktree>`, EXIT=0, `RESULT: PASS`, `gate-2-feature-check.log`. Rows: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length all PASS.

Other:
- `cmake -S . -B build`: EXIT=97 (`cmake-1-reconfigure.log`), refused by the lane guard as noted above.

## Files committed

- `src/core/PitchTrack.cpp`: the bounds check at line 87.
- `tests/src/core/TunerTapTest.cpp`: the regression (includes `<cmath>`, `<cstddef>`, `<limits>`, `"PitchTrack.h"`; slot at lines 124-139).
- `tests/fork-sources.txt`: the report entry (`docs/qwen-fixes-2026-10/cluster-new-p10-290.md`). The test file is an existing registered file, so no new source entry is needed.
- `docs/qwen-fixes-2026-10/cluster-new-p10-290.md` (this report).

Not committed: the logs (in the LOG FOLDER, outside git) and `fix.patch`.

## Self-check

- **The regression FAILS with the product change reverted and PASSES with it**: yes. RED (EXIT 8, `TunerTapTest.cpp:139`, Actual 110.2548 Hz) and GREEN (EXIT 0), plus the 4b revert/re-apply (`test-10-4b-red.log` EXIT 8, `test-11-4b-green.log` EXIT 0). The first attempt's RED/GREEN in `AudioAnalysisTest` shows the same result (`test-2-red.log`, `test-5-4b-green.log`).
- **Deterministic, no clock, sleep or race**: yes. The input is a generated sine with 16 samples of padding; no seam or hook is needed because `pitchAt` is a pure function of its arguments.
- **Fix at the cause; callers and siblings checked**: yes. Both callers (`PitchTrack.cpp:235`, `ControlCommandsTuner.cpp:83`) pass bounded frames, so the new check is consistent with them. Siblings `rms()` and `onsetInWindow()` were checked by reading (see above) and left unchanged.
- **No collateral damage**: the accepted set for non-wrapping inputs is unchanged (see the algebra above); the sanity assertions show a window at the very end of the span is still accepted. The check only refuses windows that cannot fit in `count`; no caller passes such a window (`detectNotes` loop condition, `frame = 0` in the tuner). Saved-project loading does not call `pitchAt` (the only callers are the two listed above). Not checked: I did not run the GUI or a saved-project load.
- **Verdict FIXED is a real defect, latent in production**: no current production path reaches the wrap (see Reachability above). The fix is kept because the function's own contract is violated and the cost is one comparison.
- **Open for the parent**: the regression would fit better in a `PitchTrack` test file. Moving it needs a new `tests/CMakeLists.txt` entry and a reconfigure, which this lane cannot do.
