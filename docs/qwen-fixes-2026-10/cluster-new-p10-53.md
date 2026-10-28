Verdict: FALSE-POSITIVE

new-p10-53: a negative note key cannot reach `scale.detect`'s weight array. Every writer of `Note::m_key` clamps to `0..NumKeys-1` (`0..127`), so the index `key % 12` is always in `0..11`. The finding's trigger, a malformed project that loads key `-1`, is floored to `0` at load time. No production change was made. Two guard tests were added to pin the invariant at the two places it can break: the load path and the `scale.detect` path.

## Evidence

- **The index is computed in `detectScale`, not at the cited line.** `src/core/ControlCommandsNoteMoves.cpp:174` does `weight[note->key() % 12] += ...`. The cited `:163` is the closing brace of `fit()` (`:155-165`). There `pc` runs over `0..11` (`:159`), so `fit()` is bounded. The only index that depends on a note key is `:174`.
- **Every writer of `m_key` clamps** (`logs/001-key-writers.log` in the round-1 run folder):
  - constructor: `src/core/Note.cpp:168` `m_key(std::clamp(key, 0, NumKeys - 1))`;
  - copy constructor `:193` and copy assignment `:225` copy an already-clamped value;
  - `Note::setKey`: `src/core/Note.cpp:351-354` clamps at `:353`;
  - project load: `src/core/Note.cpp:641-642` takes `max(legacyNoteKey, key attribute)`, then clamps to `[0, NumKeys-1]`, then calls `setKey`.
  - `NumKeys = 128` is at `include/Note.h:93`.
- **The load path is the one the finding describes.** A `note` element with `key="-1"` goes through `Note::restoreState` → `Note::loadSettings` (`src/core/Note.cpp:641`) and is clamped to `0`. The `max()` with `legacyNoteKey` keeps a negative only when the legacy `tone` attribute is also negative (`legacyNoteKey` returns `0` when `tone` and `oct` are absent). That is why the load test sets `tone="-1"`: without it, the `max()` floors the value before the clamp runs.
- **`key % 12` at other sites** (`logs/002-mod12.log`): `ControlCommandsNoteMoves.cpp:108` is a `QList::value` name lookup, and `:174` is the one above. Other `% 12` sites are guarded by `((x % 12) + 12) % 12` or use non-negative keys.

## Regression guards (pass on current code; they are guards, not RED tests)

1. `MidiProbabilityPersistenceTest::negativeProjectKeyLoadsAsKeyZero` (`tests/src/core/MidiProbabilityPersistenceTest.cpp`, end of class). Loads a `note` element with `key="-1"`, `tone="-1"`, `pos="0"`, `len="24"` through `Note::restoreState` and asserts key `0`. This file holds the malformed-attribute load tests, and it has no Engine setup.
2. `NoteToolsTest::aNegativeNoteKeyIsClampedAndDetectsInRange` (`tests/src/core/NoteToolsTest.cpp`, before `followingChordsRefusesAnEmptySelection`). Builds a clip with a `-1` note via `clipWith`, asserts its keys are `{0, 60}`, and asserts `scale.detect` returns ok.

Both files are already registered (`tests/fork-sources.txt`, `tests/CMakeLists.txt`), so no registration change was needed.

The first placement of the load test in `NoteToolsTest.cpp` took that file to 508 code lines, which failed the file-length gate (base: 489; limit 500). The load test was moved to `MidiProbabilityPersistenceTest.cpp` and the detect test was shortened, which brings `NoteToolsTest.cpp` to 496 code lines.

### Mutation probe (both tests must fail when all three clamps are removed)

Probe: `Note.cpp:168` (ctor), `:353` (`setKey`) and `:642` (loader) clamps removed. The patch was saved as `logs/mutation-probe.patch` (`git diff` of `src/core/Note.cpp`, 3 hunks).

| Step | Command | EXIT | Result |
| --- | --- | --- | --- |
| Apply the three clamp removals by in-place edits | Edit tool on `src/core/Note.cpp` | - | probe active |
| Save probe patch | `git diff -- src/core/Note.cpp > logs/mutation-probe.patch` | 0 | `logs/mutation-probe.patch` |
| Rebuild with probe | `CCACHE_DISABLE=1 cmake --build build --target zene NoteToolsTest MidiProbabilityPersistenceTest -j4` | 0 | `logs/080-build-mutated-all.log` |
| RED, detect guard | `./NoteToolsTest aNegativeNoteKeyIsClampedAndDetectsInRange` (in `build/tests`) | 1 | `FAIL! ... Compared lists differ at index 0. Actual -1, Expected 0`, `logs/081-red-notetools-fn.log` |
| RED, load guard | `./MidiProbabilityPersistenceTest negativeProjectKeyLoadsAsKeyZero` (in `build/tests`) | 1 | `FAIL! ... Actual (loaded.key()): -1, Expected (0): 0`, `logs/082-red-persistence-fn.log` |
| Revert probe | `git apply -R logs/mutation-probe.patch` | 0 | `git diff -- src/core/Note.cpp` is empty (0 lines) |
| Rebuild restored | `CCACHE_DISABLE=1 cmake --build build --target zene NoteToolsTest MidiProbabilityPersistenceTest -j4` | 0 | `logs/090-build-restored.log` |
| GREEN, detect guard | `./NoteToolsTest aNegativeNoteKeyIsClampedAndDetectsInRange` | 0 | `Totals: 3 passed, 0 failed`, `logs/091-green-notetools-fn.log` |
| GREEN, load guard | `./MidiProbabilityPersistenceTest negativeProjectKeyLoadsAsKeyZero` | 0 | `Totals: 3 passed, 0 failed`, `logs/092-green-persistence-fn.log` |
| Family | `ctest -R "^(NoteToolsTest|MidiProbabilityPersistenceTest)$" --output-on-failure` (in `build/tests`) | 0 | `100% tests passed out of 2` (2/2 executed), `logs/093-ctest-final.log` |

Earlier round-1 probe, for the record: removing only the `setKey` clamp still passed, because the loader clamp covers it. That redundancy is why the probe removes all three.

## Gates

| Check | Command | EXIT | Result |
| --- | --- | --- | --- |
| Feature gates, log location redirected | `python3 <run>/gate/feature_check.py <worktree>` (copy of the script, see below) | 0 | `logs/100-feature-check.log`: all eight rows PASS, `RESULT: PASS` |
| Feature gates, official path | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 1 | `logs/101-feature-check-official-path.log`: `PermissionError: [Errno 13]` on `<worktree>-feature-check`, before any gate runs |

Note on the gate run: `feature_check.py` writes its logs to `<worktree>/../<name>-feature-check`, which is the projects directory. That location is outside the write-jail, so the official run stops at `logs.mkdir` with `PermissionError`. The directory mode is `drwxrwxrwx`, so the refusal comes from the jail rather than from file permissions. I did not work around the jail. I ran a copy of the same script that changes only two lines: `ROOT` points at the real `qwen/` directory (so `FIX-BASE` and the existing `gate-baselines/` cache are read unchanged), and the log directory is the run's `logs/gate`. The gate checks are unchanged.

Gate rows from the copy (`logs/100-feature-check.log`):

| Gate | Result |
| --- | --- |
| fork-sources | PASS |
| all-sources-reproduce | PASS |
| no-upstream-regression | PASS |
| check-namespace | PASS |
| no-tautology | PASS |
| doc-figures | PASS |
| complexity | PASS (no new item in changed files) |
| file-length | PASS (no new item in changed files; `NoteToolsTest.cpp` back under 500) |

The build also needed `CCACHE_DISABLE=1`. ccache could not create its temporary file under the shared `dsh/home/ccache-tmp-r6` path (permission denied), so the build failed before compiling. Disabling the cache affects only build speed.

## Self-check

Answers to the five ASK items, with evidence.

1. **The regression test fails with the product change reverted and passes with it.** There is no product change, so there is nothing to revert for step 4b. The equivalent proof is the mutation probe above. With all three clamps removed, both guards fail with EXIT 1 (`logs/081`, `logs/082`). With the clamps restored, both pass with EXIT 0 (`logs/091`, `logs/092`), and the family is 2/2 (`logs/093`). The guards are not RED tests on the current code: they pass without any change, which is the expected result for a FALSE-POSITIVE.
2. **Deterministic.** Both guards build their input in memory from fixed attributes (`key`, `tone`, `pos`, `len`) and fixed notes (`clipWith` with keys `-1`, `60` at ticks `0`, `24`). They use no clock, sleep, thread or random seed. `scale.detect` is a pure read, and `restoreState` is a pure parse.
3. **The fix is at the cause, and the cause is absent.** No production code was changed. The callers of `key % 12` on a note key were checked: `ControlCommandsNoteMoves.cpp:108` (name lookup through `QList::value`, safe) and `:174` (the weight index, bounded by the three clamps). The other `% 12` sites are listed in the Evidence section.
4. **No collateral damage.** No product code changed, so saved projects load as before. The guards add no limit or check that could affect other callers.
5. **FALSE-POSITIVE evidence.** `src/core/Note.cpp:168, :225, :353, :642` (the four writers of `m_key`), `include/Note.h:93` (`NumKeys = 128`), `src/core/ControlCommandsNoteMoves.cpp:174` (the index), `:159-162` (the bounded loop in `fit`). The probe in the table above shows that removing all three clamps makes the guards fail, so the guards depend on the clamps and are not vacuous.

**Gate row, official path: still FAIL, from the write-jail, not from the code.** The copy with the log location redirected passes all eight rows (`logs/100`). Closing the official row needs `feature_check.py` run from a session that can write the projects directory, or the log path allowed in the write-jail. I have not changed the gate script or its baselines.

## Files committed

- `tests/src/core/MidiProbabilityPersistenceTest.cpp` (new `negativeProjectKeyLoadsAsKeyZero`)
- `tests/src/core/NoteToolsTest.cpp` (new `aNegativeNoteKeyIsClampedAndDetectsInRange`; the round-1 `QDomDocument` include and round-1 test are removed)
- `docs/qwen-fixes-2026-10/cluster-new-p10-53.md` (this report)

No product file is changed. `src/core/Note.cpp` is back to its committed content (`git diff` empty).
