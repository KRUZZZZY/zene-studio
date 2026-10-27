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

(filled in after `submit_check.py`, below)
