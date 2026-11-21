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

## Self-check

- **Regression fails with the product change reverted, passes with it.** Yes. Reverted (`git apply -R fix.patch`,
  EXIT 0), rebuilt (`build-4.log`, EXIT 0), `ctest -R '^NotePlaybackTest$'` EXIT 8 (`test-red-2.log`): the new case
  fails at `NotePlaybackTest.cpp:522` with Actual 0, Expected 4294967296. Re-applied (`git apply`, EXIT 0), rebuilt
  (`build-5.log`, EXIT 0), `./NotePlaybackTest aLoopWrapCountPassesTheThirtyTwoBitLimit` EXIT 0 (`test-green-2-single.log`).
- **Deterministic.** Yes. No sleeps and no wall-clock time. The test sets the count to 0xFFFFFFFF through the test seam
  `Song::setLoopWrapsForTest` (`include/Song.h:386`), then renders periods (bounded to 2000 iterations, the same pattern
  as the existing loop-wrap tests) until one loop wrap is played. The render is driven by the test, not by a timer.
- **Fix at the cause, every caller and sibling checked.** The writer (`src/core/Song.cpp:620` fetch_add) and the
  accessor (`include/Song.h:384`) use the same type. The readers are `src/core/Song.cpp:563` (passes the count to
  `MidiPlayback::tickPlayed`, now `std::uint64_t`), `src/core/MidiPlayback.cpp:73,84` (subtraction), and
  `src/core/MarkerActions.cpp:69,81,92` (`Counts`, `countsNow`, `movedBetween`). The tests that read the count are
  `tests/src/core/UnmuteChaseTest.cpp:247,364` and `tests/src/core/NotePlaybackTest.cpp:488`; they use `const auto`, so
  they compile on both the old and new type. A grep for `loopWraps|m_loopWraps|tickPlayed` in `src` and `tests` found no
  other caller. `tests/src/core/SessionPerformTest.cpp` includes `MarkerActions.h` but does not read the count.
- **No collateral damage.** The count is runtime state and is not written to any project file (no serialisation of
  `loopWraps`; `grep -rn loopWraps src include` shows only the readers and writer above). Old projects load as before.
  The change adds no limit or check. The seam is a test-only setter and is not a control command. Realtime: the render
  thread still does only `fetch_add` on an atomic (`Song.cpp:620`); no allocation, lock, or log was added. Undo: the
  count is not part of undo.

## Gate status (honest)

`submit_check.py` reports `gates (feature_check.py)` as FAIL because `feature_check.py` could not write its logs
outside the write-jail (`PermissionError` on `<worktree>/../zene-qwen-new-p16-232-feature-check`, see the Gates
section above). The eight gate commands the script runs all exit 0 except the two ratchets, which exit 1 as the brief
expects: `complexity` (no item in a changed file) and `file-length` (`NotePlaybackTest.cpp`, already over 500 lines
on the base). The gates were therefore run directly, not through the script; the script itself was not re-run to
a PASS.

## Round 2 — review REWORK (cleanup on early return)

The review (`qa/reviews/new-p16-232.r1.md`) found that when the regression failed without the fix, its `QCOMPARE`
returned before the cleanup at the end of the test. The loop stayed enabled and two later tests
(`aLoopedClipPlaysItsStartAgain`, `chaseSoundsANotePlaybackStartsInside`) failed in the RED run.

**Change (test only).** `tests/src/core/NotePlaybackTest.cpp:508` `aLoopWrapCountPassesTheThirtyTwoBitLimit` now
declares a local RAII guard `RestoreLoop` (line 515; constructed at line 526). Its destructor, which runs on every
return path, calls `song->stop()`, `timeline.setLoopEnabled(false)`, and `setLoopWrapsForTest(<count captured at
construction>)`. The end-of-test cleanup lines were removed because the guard does them. The fix is unchanged.

**Evidence (all logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-232.r2/logs/`).**

| Step | Command | EXIT | Log |
| --- | --- | --- | --- |
| Build test with guard, fix in place | `cmake --build build --target NotePlaybackTest -j4` | 0 | `build-1.log` |
| GREEN, NotePlaybackTest | `ctest -R '^NotePlaybackTest$' --output-on-failure` (in `build/tests`) | 8 | `test-green-1.log` — 23 passed, 1 failed (`monstroNoteStartDoesNotAllocate`, environmental) |
| GREEN, regression alone | `./NotePlaybackTest aLoopWrapCountPassesTheThirtyTwoBitLimit` | 0 | `test-green-1-single.log` — PASS |
| Save fix diff | `git diff ed8e1b478 527d072b4 -- include/Song.h include/MidiPlayback.h src/core/MidiPlayback.cpp src/core/MarkerActions.cpp > fix.patch` | 0 (91 lines) | `fix.patch` |
| Proof: revert fix | `git apply -R fix.patch` | 0 | — |
| Rebuild reverted | `cmake --build build --target zene NotePlaybackTest -j4` | 0 | `build-2.log` |
| RED, reverted code, with guard | `ctest -R '^NotePlaybackTest$' --output-on-failure` | 8 | `test-red-1.log` — `aLoopWrapCountPassesTheThirtyTwoBitLimit` fails at `NotePlaybackTest.cpp:536`: Actual 0, Expected 4294967296. The two clip tests now PASS (the cleanup bug is gone). Totals 22 passed, 2 failed (the regression and the environmental monstro case). |
| Re-apply fix | `git apply fix.patch` | 0 | — |
| Rebuild fixed | `cmake --build build --target zene NotePlaybackTest UnmuteChaseTest SessionPerformTest ArticulationPlaybackTest -j4` | 0 | `build-3.log` |
| GREEN, affected family | `ctest -R '^(NotePlaybackTest\|UnmuteChaseTest\|SessionPerformTest\|ArticulationPlaybackTest)$' --output-on-failure` | 8 | `test-green-2.log` — 4 of 4 ctest entries ran; 3 pass; NotePlaybackTest 23 passed, 1 failed (monstro only) |
| GREEN, regression alone | `./NotePlaybackTest aLoopWrapCountPassesTheThirtyTwoBitLimit` | 0 | `test-green-2-single.log` — PASS |

Gates re-run on the round-2 tree (same eight commands as before; logs `gate-*.log`):
fork-sources 0, all-sources-reproduce 0, no-upstream-regression 0, check-namespace 0, no-tautology 0,
doc-figures 0; complexity 1 (6 REGRESSION lines, none in a file this change touches); file-length 1 (one line in a
changed file: `new file over 500 lines: tests/src/core/NotePlaybackTest.cpp (653)`).

**Correction to the file-length note above.** Earlier I quoted `wc -l` for the base commit (742). The gate counts
code lines. The review measured the base `NotePlaybackTest.cpp` at 624 code lines, so the item was already over 500
before this change; it is not new. The gate's count rose from 643 to 653 because of the guard.

**Monstro.** The reviewer also observed `monstroNoteStartDoesNotAllocate` fail in the reverted run. It fails
with and without the fix, so it is independent of this change. Its plugin is not built in this worktree.

Line numbers in the round-1 sections (for example the regression at `NotePlaybackTest.cpp:506-527` and the RED
assertion at `:522`) are from round 1; the round-2 numbers above are current.
