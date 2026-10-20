# Cluster 5032 — Probability undo test does not check that probability was restored

Verdict: **FIXED** (the defect is in the test; the product code was already correct and is unchanged)

## Finding

`tests/src/core/ControlVerbInverseTest.cpp:261` (historical line; the test now lives at
`noteProbabilitySetIsOneUndoableStep()`, lines 290–315). After `note.probability_set` and
`control.undo`, the test checked only that one note remained. If undo left the note at
probability 0.5, the test still passed.

## Decision and evidence

The finding is real as a test gap. The product path is correct:

- `src/core/ControlCommandsNoteProbability.cpp:145-146`: the handler calls
  `clip->addJournalCheckPoint()` **before** `note->setProbability(...)`, so the checkpoint
  holds the pre-edit XML.
- `src/core/Note.cpp:469-471`: `prob` is written only when `m_probability != 1.f`. A
  checkpoint taken before a first edit therefore has no `prob` attribute.
- `src/core/Note.cpp:565`: `setProbability( loadMidiDepthValue( _this, "prob", 1.f ) )`
  restores the default on absence, so undo returns the note to 1.0.

The test never read the probability. Two things made that hard to see:

1. `roll.get_state` does **not** publish probability. `rollState()` builds each note with
   `noteState()` (`src/core/ControlEditSupport.cpp:324-343`), which has no `probability` key.
   Reading the field from the roll would have asserted on something that is never there.
2. The field is published by `note.list`, through `noteJson()`
   (`src/core/ControlCommandsNoteShared.h:58`, used by `listNotes()` at
   `src/core/ControlCommandsNoteShape.cpp:154`).

## Change

`tests/src/core/ControlVerbInverseTest.cpp` (test-only):

- new helper `noteProbability(clip, index)` (line 97) reads `note.list`; a missing note yields
  NaN so the comparison fails rather than passing on a default;
- `noteProbabilitySetIsOneUndoableStep()`: asserts the edit reached 0.5 before the undo
  (line 303), and after `REV_UNDO_OR_FAIL()` asserts the probability is the default 1.0
  (line 314);
- `#include <QJsonArray>` added for the helper;
- the comment at lines 270-274 is corrected. It previously said this test fails if the
  reset is guarded behind `hasAttribute()`. That claim is false for this fixture (see
  "Equivalent mutant" below), so the comment now says what the test does catch.

No product file is changed.

## Regression proof

The product code is correct, so the RED state is produced with a temporary mutation that
represents the bug the test must catch: the checkpoint moved **after** the write, so undo
restores the edited state. The mutation was saved as a patch
(`logs/mutation.patch`, one hunk in `src/core/ControlCommandsNoteProbability.cpp`).

| Step | Command | EXIT | Result |
|---|---|---|---|
| Baseline build (unchanged test) | `CCACHE_TEMPDIR=/tmp/ccache-tmp-5032 cmake --build build --target ControlVerbInverseTest -j2` | 0 | built |
| Baseline test | `cd build/tests && ctest -R '^ControlVerbInverseTest$' --output-on-failure` | 0 | 1/1 passed |
| New test, unmodified product | build, then ctest as above | 0 / 0 | 1/1 passed |
| **RED**: mutation applied | `git apply` of the mutation (checkpoint after write), then `cmake --build build --target zene ControlVerbInverseTest -j2`, then ctest | 0 / 8 | **FAIL** `noteProbabilitySetIsOneUndoableStep()`: `Actual (noteProbability(fixture.clip, 0)): 0.5`, `Expected (1.0): 1`, at `ControlVerbInverseTest.cpp:312` (line 314 after the later comment edit) |
| Mutation reversed | `git apply -R logs/mutation.patch` (diff vs HEAD is empty for `ControlCommandsNoteProbability.cpp`), rebuild zene + test, ctest | 0 / 0 | **GREEN**: 1/1 passed |
| Final GREEN (after comment fix) | rebuild ControlVerbInverseTest, ctest | 0 / 0 | 1/1 passed |

Test-function count: the family `^ControlVerbInverseTest$` is one ctest test (1 executed).
The binary has 8 test slots, plus `initTestCase` and `cleanupTestCase`. The RED run's QTest
totals are `9 passed, 1 failed` (10 counted). The GREEN ctest logs show only the ctest
result (1/1 passed), not per-function totals.

The `hasAttribute()` guard was also tried as a mutation (`logs/mutation-guard.patch`,
`src/core/Note.cpp:565`): build EXIT 0, ctest EXIT 0, **test passes**. Reversed with
`git apply -R` (EXIT 0), rebuilt (EXIT 0).

### Equivalent mutant (why the old comment was wrong)

With the guard, `Note::loadSettings` skips the read for an absent `prob`. The note is still
not at 0.5 after undo, because `MidiClip::loadSettings` builds each note with `new Note`
(`src/tracks/MidiClip.cpp:535-536`), and the default member initialiser
`float m_probability = 1.f;` (`include/Note.h:407`) makes it 1.0. The guard is therefore
equivalent for this fixture and the test cannot distinguish it. That is a limit of this test
and it is recorded in the comment; it is not a reason to change the product code.

## Commands and EXIT codes

Every build and test log is in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5032.r1/logs/`.

- `build-1-baseline.log`: `cmake --build build --target ControlVerbInverseTest -j2`, **EXIT=2**.
  Cause: ccache could not write to `/run/user/1000/ccache-tmp` (sandbox permission), not the
  code. Retried with `CCACHE_TEMPDIR=/tmp/ccache-tmp-5032`, **EXIT=0**. Every build after
  that uses the same environment variable.
- Gates, run one command at a time from the worktree (see "Gates" for why the wrapper did not run):

| Gate | Command | EXIT |
|---|---|---|
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 |
| check-namespace | `tests/scripted/check-namespace` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 |

## Gates

`python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` could
not run in this sandbox. It exits at `feature_check.py:71` with `PermissionError` creating
`<worktree>/../zene-qwen-5032-feature-check`, which is outside the write-jail. It would also
write a baseline cache under `~/.local/share/zene-bunny-fix/qwen/gate-baselines` and run
`git worktree add` in `zene-cachyos`. I did not edit the wrapper or the gate. Instead I ran
the eight commands the wrapper runs, one by one, from the worktree.

The two ratchets exit 1, as the wrapper's own description says they do on the train. The
wrapper fails a ratchet only on a `REGRESSION:` line that names a file the branch changed.
The complexity log has 6 `REGRESSION:` lines and the file-length log has 15. None names
`ControlVerbInverseTest` or `ControlCommandsNoteProbability`, so under the wrapper's filter
both rows would be PASS. Those lines are in `final-gate-complexity.log` and
`final-gate-file-length.log`.

## Files committed

- `tests/src/core/ControlVerbInverseTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-5032.md`

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.** The
  "product change" here is the injected checkpoint-ordering bug, because the product code
  is correct. RED: EXIT 8, `Actual 0.5`, `Expected 1.0`, `ControlVerbInverseTest.cpp:312`
  (line 314 after the later comment edit). GREEN: EXIT 0, 1/1 passed. Both recorded above.
- **Deterministic.** No wall-clock, sleep or race. The test uses the registry's own
  `note.probability_set`, `note.list` and `control.undo` on an in-memory fixture.
- **Fix at the cause, and complete.** The cause is the test not reading the field. The
  sibling assertions in the same function (note count, velocity) are unchanged. The other
  `note.probability_set` tests (`noteProbabilitySetRefusesOutsideTheRange`, and the link
  test `ClipLinkTest::probabilityAndSlideEditsAreSeenByTheOtherMember`) do not undo, so they
  are unaffected. `ClipLinkTest.cpp:431` says there is "no read-back verb for the field". That
  comment is stale: `note.list` reports it (`ControlCommandsNoteShared.h:58`). I did not edit it.
- **No collateral damage.** The product code is byte-identical to HEAD. No saved data,
  limit or check changed.
- **ALREADY-FIXED / FALSE-POSITIVE evidence:** not applicable. The verdict is FIXED.

## Open for the owner

- The header comment in `src/core/ControlCommandsNoteProbability.cpp:27-29` says a
  negative control in `tests/control-verb-inverses.py` neuters the reset and shows the undo
  fail. I did not check that script. The `hasAttribute()` mutation above passes the C++ test,
  so the Python negative control, if it exists, needs its own check.
- `feature_check.py` cannot run inside this sandbox (see "Gates"). It should write its logs and cache to
  a path the lane may write to.
