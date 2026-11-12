Verdict: FIXED (latent: no current production caller can reach the negative or non-finite input; the function contract was unguarded)

new-p16-128: Invalid frame timing can turn a negative delay into a huge unsigned delay

## Evidence

Before the fix, `MidiPlayback::delayFrames` added `amount * grid * framesPerTick` to a double with no check on `framesPerTick`, then did `static_cast<f_cnt_t>(std::lround(frames))` on the result. `f_cnt_t` is `std::uint64_t` ([include/LmmsTypes.h:43](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-128/include/LmmsTypes.h:43)), so a negative result wrapped. The finding's input (`delayFrames(12, -1.0f, 44100)` at full swing) gave −9 frames, which came back as 18446744073709551607 (2^64 − 9). That value is in `test-red-1.log`.

Fix at the cause, in [src/core/MidiPlayback.cpp:96-112](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-128/src/core/MidiPlayback.cpp:96):
- swing is added only when `framesPerTick` is finite and positive (line 103);
- a result that is not `> 0` (negative or NaN) returns 0 (line 109);
- a result at or above the top of `f_cnt_t` saturates to its maximum before the conversion (line 110);
- otherwise the value is rounded with `std::round`, which matches `lround`'s half-away-from-zero rounding for every in-range value (line 111).

The MIDI offset (clamped to [0, 500] ms at [src/core/MidiPlayback.cpp:68](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-128/src/core/MidiPlayback.cpp:68)) still applies when the tick length is unusable; the test checks this.

Reachability, checked against the production callers:
- The only caller is [src/tracks/InstrumentTrackNotes.cpp:272](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-128/src/tracks/InstrumentTrackNotes.cpp:272), which passes `Engine::framesPerTick()`.
- `framesPerTick` is `sampleRate * 60 * 4 / DefaultTicksPerBar / tempo` ([src/core/Engine.cpp:137](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-128/src/core/Engine.cpp:137), [:146](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-128/src/core/Engine.cpp:146)), and the tempo model is bounded to [MinTempo, MaxTempo] = [10, 999] ([include/Song.h:78-80](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-128/include/Song.h:78), [src/core/Song.cpp:122](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-128/src/core/Song.cpp:122)). So the production path yields a positive finite value.
- The sibling conversion in the same caller, the track's own delay, is clamped to [0, 500] at [include/InstrumentTrack.h:104](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-128/include/InstrumentTrack.h:104), so it cannot go negative either.
- The finding's trigger therefore needs a direct call with a negative or non-finite frames-per-tick. The fix is the guard the contract needs, and it changes nothing for valid inputs.
- The header ([include/MidiPlayback.h:72-77](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-128/include/MidiPlayback.h:72)) is unchanged, so no dependents rebuild.

Regression test: [tests/src/core/NotePlaybackTest.cpp:886-908](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-128/tests/src/core/NotePlaybackTest.cpp:886), slot `delayFramesStaysAFrameCount`. I put it here rather than in the three listed tests because `NotePlaybackTest` already tests `delayFrames` directly ([:784-790](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-128/tests/src/core/NotePlaybackTest.cpp:784), `swingDelaysTheOffBeatsOnly`). The three listed tests exercise transactions, chase and routing, not `delayFrames`. The test is deterministic: no timing, no sleeps, and it resets swing and offset at the end.

Matrix covered: valid input (100 frames per tick → 900); −1, −100, NaN and +inf frames per tick → 0; offset-only delay with a bad tick length (441 frames at 10 ms / 44.1 kHz, on an off-beat and an on-beat tick); offset undone to 0 afterwards. Not covered: the audio-thread path (`triggerNote`), which is unchanged. No new allocation, lock or log is added to the audio path (the guard is a few float comparisons).

## Commands and exit codes

Ccache was disabled for every build: the ccache temp dir is not writable from this sandbox (`ccache: error: failed to create temporary file ... Permission denied`, see `build-red-1.log`, EXIT=2). Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-128.r1/logs/`.

| Step | Command (run from the worktree unless noted) | EXIT | Log |
|---|---|---|---|
| RED build (first try, ccache error) | `cmake --build build --target NotePlaybackTest -j2` | 2 | build-red-1.log |
| RED build | `CCACHE_DISABLE=1 cmake --build build --target NotePlaybackTest -j2` | 0 | build-red-2.log |
| RED test (product unfixed) | `cd build/tests && ctest -R '^NotePlaybackTest$' --output-on-failure` | 8 | test-red-1.log |
| GREEN build | `CCACHE_DISABLE=1 cmake --build build --target zene NotePlaybackTest NoteCommandTransactionsTest UnmuteChaseTest InstrumentRoutingTest -j2` | 0 | build-green-1.log |
| GREEN family | `cd build/tests && ctest -R '^(NotePlaybackTest\|NoteCommandTransactionsTest\|UnmuteChaseTest\|InstrumentRoutingTest)$' --output-on-failure` | 8 | test-green-1.log |
| 4b: revert fix | `git diff -- src/core/MidiPlayback.cpp > logs/fix.patch && git apply -R logs/fix.patch` | 0 | apply-revert.log |
| 4b: rebuild unfixed | `CCACHE_DISABLE=1 cmake --build build --target zene NotePlaybackTest -j2` | 0 | build-4b-unfixed.log |
| 4b: test unfixed | `cd build/tests && ctest -R '^NotePlaybackTest$' --output-on-failure` | 8 | test-4b-unfixed.log |
| 4b: reapply fix | `git apply logs/fix.patch` | 0 | apply-forward.log |
| 4b: rebuild fixed | `CCACHE_DISABLE=1 cmake --build build --target zene NotePlaybackTest -j2` | 0 | build-4b-fixed.log |
| 4b: test fixed | `cd build/tests && ctest -R '^NotePlaybackTest$' --output-on-failure` | 8 | test-4b-fixed.log |
| Gates via script | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 1 | feature-check-1.log |
| Gates, run by hand | the six simple gates and two ratchets, same commands and cwd as the script | see below | gate-*-3.log |

Test results:

| Test | Result (RED, unfixed) | Result (GREEN, fixed) |
|---|---|---|
| `delayFramesStaysAFrameCount` | FAIL: `playback.delayFrames(12, framesPerTick, 44100)` = 18446744073709551607, expected 0 (test-red-1.log:49-52) | pass |
| `monstroNoteStartDoesNotAllocate` | FAIL, `dummyinstrument` vs `monstro` (test-red-1.log:11-14) | FAIL, same (test-green-1.log:14) |
| `nesNoteStartDoesNotAllocate` | FAIL, `dummyinstrument` vs `nes` (test-red-1.log:16-19) | FAIL, same (test-green-1.log:19) |

The two `*NoteStartDoesNotAllocate` failures are present on the unmodified product code, before my change. They are unrelated: the instrument falls back to `dummyinstrument`. They are not caused or changed by this fix.

Family test counts (ctest `-R` family, 4 tests executed, not zero): InstrumentRoutingTest passed, NoteCommandTransactionsTest passed, UnmuteChaseTest passed, NotePlaybackTest 29 passed / 2 failed (the pre-existing two). RED: 28 passed / 3 failed (the new test plus the two pre-existing).

Gates:
- `feature_check.py` exited 1 without running its gates. Its `main()` calls `logs.mkdir()` on `<worktree>/../<name>-feature-check`, which is `projects/zene-studio-program/zene-qwen-new-p16-128-feature-check` (`feature-check-1.log`). That is outside my write area, so I did not run it. I did not change the script or its gate limits.
- Run by hand from the worktree: `fork-sources` EXIT=0, `all-sources-reproduce` EXIT=0, `no-upstream-regression` EXIT=0, `check-namespace` EXIT=0, `no-tautology` EXIT=0, `doc-figures` EXIT=0.
- Ratchets, run by hand: `tests/complexity-gate.sh --check` EXIT=1 and `tests/file-length-gate.sh --check` EXIT=1. Their red output lists items that were already in the base cache (complexity 7, file-length 21, `gate-baselines/7641e9493….json`). I reproduced the script's comparison with its own `key()`/`item_file()`/`REG` functions and its baseline cache: no new item in the changed files (`ratchet-compare-3.log`). The one file-length item in a changed file, `tests/src/core/NotePlaybackTest.cpp (831)`, is already in the base list. The feature_check verdict for these two rows is therefore PASS by the script's own rule, but I did not get that verdict from the script itself.

## Files committed

- `src/core/MidiPlayback.cpp` (product fix; both `<limits>` include and guard)
- `tests/src/core/NotePlaybackTest.cpp` (regression slot)
- `docs/qwen-fixes-2026-10/cluster-new-p16-128.md` (this report)

Not touched: headers, `tests/fork-sources.txt`, `tests/upstream-modifications.txt` (`MidiPlayback.cpp` is already listed as fork-new at `tests/fork-sources.txt:1805`), and no control command was added.

## Self-check

`submit_check.py` (log `submit-check-1.log`, EXIT=1): PASS commits on the branch; PASS working tree clean; PASS this report; PASS tests changed (`NotePlaybackTest.cpp`); **FAIL gates (feature_check.py)**. The FAIL is the write-jail, not a gate result: `feature_check.py` exits at `main()`'s `logs.mkdir()` with `PermissionError` on `projects/zene-studio-program/zene-qwen-new-p16-128-feature-check` (`feature-check-1.log`), which is outside this sandbox's writable area. The script resolves its worktree path, so a symlink does not move that folder, and I did not bypass the sandbox. The same six simple gates and two ratchets were run by hand from the worktree (see Gates above) and pass under the script's own rules. The loop should re-run `feature_check.py` where it can write its log folder.

- **Regression fails with the fix reverted and passes with it:** yes. Reverted: `delayFramesStaysAFrameCount` fails at `NotePlaybackTest.cpp:897`, actual 18446744073709551607, expected 0 (`test-4b-unfixed.log`, `git apply -R` EXIT=0, build EXIT=0, test EXIT=8). Reapplied: the function passes, and the test exit is 8 only because of the two pre-existing failures (`test-4b-fixed.log`, `git apply` EXIT=0, build EXIT=0, test EXIT=8, 29 passed / 2 failed). The `ctest` exit code alone does not separate my test from those two, so the per-function FAIL! lines are the evidence.
- **Deterministic:** yes. Inputs are literals (`songTick` 12 or 0; frames per tick −1, −100, NaN, +inf, 100; sample rate 44100; offset 10 ms through the `transport.midi_options` command and one `control.undo`). No clock, sleep or thread. Swing is set with `setSwing` and reset to 0 at the end, as `nonFiniteSwingLoadsBounded` does.
- **Fix at the cause, callers and siblings:** the only caller is `InstrumentTrackNotes.cpp:272`, which passes `Engine::framesPerTick()`, and that is bounded (`Engine.cpp:137,146`; tempo in [10, 999], `Song.h:78-80`). The sibling delay conversion in the same caller (`delayMs`) is clamped to [0, 500] (`InstrumentTrack.h:104`). The offset is clamped (`MidiPlayback.cpp:68`). The declaration is unchanged, so no header dependents needed rebuilding.
- **No collateral damage:** for in-range inputs the result is unchanged, because `std::round` and `lround` both round half away from zero and the only change is the guard. Evidence: the `swingDelaysTheOffBeatsOnly` function (`NotePlaybackTest.cpp:784`, which asserts 600, 1200 and 480) is not in the GREEN failure list, so it passed. No save or load code changed, so saved projects load as before. The saturation and the non-positive guard only affect values that were already invalid (negative, NaN, or past `f_cnt_t`'s range). No new control command, no undo change, nothing new on the audio thread beyond float comparisons.
- **ALREADY-FIXED / FALSE-POSITIVE evidence:** not applicable. The verdict is FIXED.
