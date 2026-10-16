# Cluster new-p12-141

Verdict: **BLOCKED**

The MIDI recovery behavior is fixed and the regression is proven red without the product patch and green with it. The required complexity and file-length ratchets still exit 1 because of unrelated branch-wide entries. Both gates were rerun with the product patch reversed and produced the same failures. No gate or baseline was changed.

## Finding and evidence

- `include/MidiHandoffQueue.h:204-218` documents overflow cut positions. `MidiChannelOverflow::stamp()` at line 246, `dueCut()` at line 290, `clear()` at line 316, and `recordImpl()` at line 362 now retain the earliest pending stamp while tracking a later stamp separately. `lower()` at line 409 keeps the earlier stamp from being advanced by a later refusal.
- `tests/src/core/MidiHandoffQueueTest.cpp:340` adds `repeatedOverflowKeepsEarlierRecoveryCut()`. It refuses a note-off at stamp 3, drains one event, accepts a channel 3 note-on, and refuses another note-off at stamp 4. With the original product code, the output was `E1, E2, E3, CUT3`; with the fix, it is `E1, E2, CUT3, E3, CUT3`.

## Build and regression evidence

Commands ran from the stated directories. Build and test output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-141.r1/logs/`.

| Purpose | Command | Log | Exit / result |
|---|---|---|---|
| Initial build | `cmake --build build --target zene MidiHandoffQueueTest` (worktree root) | `build-1.log` | 0 |
| RED on original product code | `ctest -R '^MidiHandoffQueueTest$' --output-on-failure` (`build/tests`) | `test-1.log` | 8; regression expected `CUT3` before `E3`, actual had `E3` first |
| Reversion proof build | `cmake --build build --target zene MidiHandoffQueueTest` (after applying `git apply -R .../fix.patch`) | `build-5.log` | 0 |
| Reversion proof RED | `ctest -R '^MidiHandoffQueueTest$' --output-on-failure` (`build/tests`) | `test-4.log` | 8; same regression assertion failed |
| Restored final build | `cmake --build build --target zene MidiHandoffQueueTest` | `build-6.log` | 0 |
| GREEN with product patch restored | `ctest -R '^MidiHandoffQueueTest$' --output-on-failure` (`build/tests`) | `test-5.log` | 0; affected CTest family count: 1 |

Patch proof commands both exited 0:

- `git diff -- include/MidiHandoffQueue.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-141.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-141.r1/logs/fix.patch`
- `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-141.r1/logs/fix.patch`

## Required ratchets

| Command | Final log / exit | Same check with product patch reversed |
|---|---|---|
| `bash tests/complexity-gate.sh --check` | `gate-1.log`, exit 1 | `gate-3.log`, exit 1 |
| `bash tests/file-length-gate.sh --check` | `gate-2.log`, exit 1 | `gate-4.log`, exit 1 |

The complexity log names six unrelated functions, including `dotCentreAlong` in `tests/src/gui/XyPadPairTest.cpp` and `parseNotes` in `src/core/DawProjectReadTracks.cpp`. The file-length log names 19 unrelated sources, including `tests/src/gui/ArrangerTimelineTest.cpp` and `src/core/SmfInterchangeReader.cpp`. Neither gate identifies the changed MIDI header or test. The existing baselines were left untouched.

`fff grep` was not available in the provided tool set, so log matches were inspected with `rg`.

## Files committed

- `include/MidiHandoffQueue.h`
- `tests/src/core/MidiHandoffQueueTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p12-141.md`
