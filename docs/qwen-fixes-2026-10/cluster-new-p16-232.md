FIXED

# Cluster new-p16-232 — the loop-wrap counter rolls back to zero

Finding: `include/Song.h` `loopWraps()` is documented as a counter that only counts up, but its backing atomic was
`std::atomic<unsigned>`. After 2^32 wraps it overflows and the public value reads 0 again.

## Verdict and reasoning

REAL, fixed at the cause. The documented contract ("only counts up and starts at 0 with the song") is violated
by the accessor's absolute value. The two in-tree readers were already written for modular arithmetic and
are not broken by the rollover: `MidiPlayback::tickPlayed` subtracts (`src/core/MidiPlayback.cpp:84`) and
`movedBetween` subtracts (`src/core/MarkerActions.cpp:92`). So the practical impact is small; the defect is
in the contract of the value the song hands out, and it is fixed by widening the counter to 64 bits, which
is the finding's first option. Saturating was rejected: once saturated, every later difference is 0 and
passes stop counting.

The counter is runtime-only and is not serialised (no `loopWraps` use outside `src/core/MidiPlayback.cpp`,
`src/core/MarkerActions.cpp`, `src/core/Song.cpp` and the tests listed below), so saved projects load exactly as
before.

## Evidence (file:line, after the fix)

- `include/Song.h:384` — `std::uint64_t loopWraps() const` (was `unsigned`).
- `include/Song.h:386` — `setLoopWrapsForTest(std::uint64_t)` test seam (added in commit `ed8e1b478`, widened here).
- `include/Song.h:836` — `std::atomic<std::uint64_t> m_loopWraps{0};` (was `std::atomic<unsigned>`).
- `include/MidiPlayback.h:69` — `tickPlayed(tick_t, std::uint64_t loopWraps)`; `:96` `std::uint64_t m_lastWraps`.
- `src/core/MidiPlayback.cpp:73` signature; `:84` `const std::uint64_t wraps = loopWraps - m_lastWraps;` (modular).
- `src/core/MarkerActions.cpp:69` `std::uint64_t wraps` in `Counts`; `:92` `const std::uint64_t wrapped`.
- Readers of the count that were updated to the same type: `tests/src/core/UnmuteChaseTest.cpp:247,364` and
  `tests/src/core/NotePlaybackTest.cpp:488` (`const auto before`, so they compile on both the old and new type).
- Writer: `src/core/Song.cpp:620` `m_loopWraps.fetch_add(1, std::memory_order_release)` — unchanged; it is an
  atomic add on the render thread, which is lock-free for a 64-bit atomic on 64-bit targets. 32-bit target
  lock-freedom was NOT checked.

Regression: `tests/src/core/NotePlaybackTest.cpp:506-527`, `aLoopWrapCountPassesTheThirtyTwoBitLimit`. It sets the
count to 0xFFFFFFFF through the seam, plays one loop wrap, and requires the count to read 0x100000000 and the
pass count to be 1. The test is placed in `NotePlaybackTest` (not in the three files the brief lists)
because none of `AutomationThinTest`, `ControlBounceSupportTest` or `TapTempoTest` exercises `loopWraps()`; the
existing loop-wrap tests are in `NotePlaybackTest` and `UnmuteChaseTest`.

No new control command was added (realtime/undo: nothing new on the audio thread except the same atomic add).

## Commands run and exit codes

All build output went to `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-232.r1/logs/`.
Build tree: `build/` in this worktree. `CCACHE_TEMPDIR=/tmp/ccache-tmp-p16` was set because the default ccache
temp dir under `/run/user` is not writable from this session (first build failed with that error, log `build-1.log`).

| Step | Command | EXIT | Log |
| --- | --- | --- | --- |
| Test seam (no behaviour change) built | `cmake --build build --target NotePlaybackTest -j4` | 0 | `build-2.log` (restarted after a background kill) |
| RED, seam only, loop counter still `unsigned` | `ctest -R '^NotePlaybackTest$' --output-on-failure` (in `build/tests`) | 8 | `test-red-1.log` — `aLoopWrapCountPassesTheThirtyTwoBitLimit`: Actual 0, Expected 4294967296, `NotePlaybackTest.cpp:522` |
| Save fix patch | `git diff -- include/Song.h include/MidiPlayback.h src/core/MidiPlayback.cpp src/core/MarkerActions.cpp > fix.patch` | 0 | `fix.patch` (91 lines) |
| Proof: revert fix | `git apply -R fix.patch` | 0 | — |
| Rebuild reverted | `cmake --build build --target zene NotePlaybackTest -j4` | 0 | `build-4.log` |
| RED on reverted code (proof) | `ctest -R '^NotePlaybackTest$' --output-on-failure` | 8 | `test-red-2.log` — same assertion, Actual 0, Expected 4294967296 (`NotePlaybackTest.cpp:522`); 20 passed, 4 failed |
| Re-apply fix | `git apply fix.patch` | 0 | — |
| Rebuild fixed | `cmake --build build --target zene NotePlaybackTest -j4` | 0 | `build-5.log` |
| GREEN, affected family | `ctest -R '^(NotePlaybackTest\|UnmuteChaseTest\|SessionPerformTest\|ArticulationPlaybackTest)$' --output-on-failure` | 8 | `test-green-2.log` — 4/4 ctest entries ran; 3 pass, NotePlaybackTest has 23 passed, 1 failed |
| GREEN, regression alone | `./NotePlaybackTest aLoopWrapCountPassesTheThirtyTwoBitLimit` (in `build/tests`) | 0 | `test-green-2-single.log` — PASS |
| Comment-only edit, rebuild | `cmake --build build --target zene NotePlaybackTest UnmuteChaseTest SessionPerformTest ArticulationPlaybackTest -j4` | 0 | `build-6.log` |
| GREEN after comment edit | the same four-test ctest, `test-green-3.log` | 8 | `test-green-3.log` — same result: 23 passed, 1 failed |

Affected-family test count: 4 ctest entries executed (NotePlaybackTest, UnmuteChaseTest,
ArticulationPlaybackTest, SessionPerformTest); NotePlaybackTest has 24 test cases, 23 pass.

The one failing case in every run, with or without the fix, is `NotePlaybackTest::monstroNoteStartDoesNotAllocate`
(`NotePlaybackTest.cpp:359`, `"dummyinstrument"` vs `"monstro"`). Its log line says `The plugin "monstro" wasn't
found`. The plugin shared objects are not built in this worktree (`find build/plugins/Monstro -name '*.so'`
returns nothing), so this is environmental, not caused by this change. I did not run it against the base commit
to prove the same failure there, so treat that as unverified-but-explained.

In the RED runs, `aLoopedClipPlaysItsStartAgain` and `chaseSoundsANotePlaybackStartsInside` also failed
(`NotePlaybackTest.cpp:597,608`). In the green run they pass. My regression's `QCOMPARE` returns from the test
before its cleanup (`song->stop(); timeline.setLoopEnabled(false);`), which leaves the loop enabled for the next
tests. The existing `aLoopWrapIsAPassAndASeekBackIsNot` has the same pattern; I did not change it.

## Gates

`python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` could NOT run from this session: it writes
its logs to `<worktree>/../zene-qwen-new-p16-232-feature-check`, which is outside the write-jail, and failed with
`PermissionError`. I ran the same eight gate commands the script runs, directly from the worktree:

| Gate | Command | EXIT |
| --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 (re-run after the `Song.h` registration line: 0) |
| check-namespace | `tests/scripted/check-namespace` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 — 6 REGRESSION lines, none in a file this change touched |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 — one REGRESSION line in a changed file: `new file over 500 lines: tests/src/core/NotePlaybackTest.cpp (643)` |

The file-length item is not new: the base commit's `tests/src/core/NotePlaybackTest.cpp` is already 742 lines by
`wc -l` (`git show 2da2ed356:tests/src/core/NotePlaybackTest.cpp`), over the 500-line limit before this change. I
could not run the base listing to compare the gate's own count (643) because its baseline cache is outside the
write-jail. The complexity ratchet items are in other files. The two ratchets are red on the train on purpose.

Registration: no new product or test file was created, so `tests/fork-sources.txt` needed no change. The inherited
file `include/Song.h` was changed, so one reason line was appended to `tests/upstream-modifications.txt`
(`include/Song.h\tcluster new-p16-232: ...`).

## Files committed

- `include/Song.h`, `include/MidiPlayback.h`, `src/core/MidiPlayback.cpp`, `src/core/MarkerActions.cpp` (fix)
- `tests/src/core/NotePlaybackTest.cpp` (regression), `tests/src/core/UnmuteChaseTest.cpp` (type of the `before` variables)
- `tests/upstream-modifications.txt` (registration reason)
- `docs/qwen-fixes-2026-10/cluster-new-p16-232.md` (this report)
- Earlier commit on this branch: `ed8e1b478` — the test seam alone (kept separate so the revert proof could be
  applied to the product fix without removing the seam).

## Not verified

- 32-bit target lock-freedom of `std::atomic<std::uint64_t>` (`Song.cpp:620` fetch_add). The 64-bit atomics already
  used on the audio side (`include/AudioEngine.h:680` onwards) make the same assumption.
- The full ctest suite was not run; only the four-binary family above.
