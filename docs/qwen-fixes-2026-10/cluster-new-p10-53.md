Verdict: FALSE-POSITIVE

new-p10-53: a negative note key cannot reach `scale.detect`'s weight array. Every writer of `Note::m_key` clamps to `0..NumKeys-1` (`0..127`), so the index `key % 12` is always in `0..11`. The finding's trigger, a malformed project that loads key `-1`, is floored to `0` at load time. No production change was made. One guard test was added to pin the invariant.

## Evidence

- **The index is computed in `detectScale`, not at the cited line.** `src/core/ControlCommandsNoteMoves.cpp:174` does `weight[note->key() % 12] += ...`. The cited `:163` is the closing brace of `fit()` (`:155-165`). There `pc` runs over `0..11` (`:159`), so `fit()` is bounded. The only index that depends on a note key is `:174`.
- **Every writer of `m_key` clamps** (`logs/001-key-writers.log`):
  - constructor: `src/core/Note.cpp:168` `m_key(std::clamp(key, 0, NumKeys - 1))`;
  - copy constructor `:193` and copy assignment `:225` copy an already-clamped value;
  - `Note::setKey`: `src/core/Note.cpp:351-354` clamps at `:353`;
  - project load: `src/core/Note.cpp:641-642` takes `max(legacyNoteKey, key attribute)`, then clamps to `[0, NumKeys-1]`, then calls `setKey`.
  - `NumKeys = 128` is at `include/Note.h:93`.
- **The load path is the one the finding describes.** A `note` element with `key="-1"` goes through `Note::restoreState` → `Note::loadSettings` (`src/core/Note.cpp:641`). It is clamped to `0`. The `legacyNoteKey` path can produce a negative value only if the legacy `tone`/`oct` attributes are also negative, and the same clamp at `:642` still floors it.
- **`key % 12` at other sites** (`logs/002-mod12.log`): `ControlCommandsNoteMoves.cpp:108` is a `QList::value` name lookup, and `:174` is the one above. Other `% 12` sites in the tree are guarded by `((x % 12) + 12) % 12` or use non-negative keys.

## Regression guard (passes on current code; not a RED test)

Added `NoteToolsTest::aNegativeProjectKeyLoadsAsKeyZeroAndDetectsInRange` in `tests/src/core/NoteToolsTest.cpp`. The file is already registered in `tests/fork-sources.txt:2519`, so no registration change was needed.

It loads a `note` element with `key="-1"` and `tone="-1"` through `Note::restoreState` and asserts key `0`. It then runs `scale.detect` on a clip containing a constructor-clamped `-1` and checks the result is ok and `fit` is in `[0, 1]`.

The `tone` attribute is needed. Without it, `max(legacy=0, key=-1)` floors the value to 0 before the clamp runs, so the test could not reach the clamp.

Mutation probe (to show the guard is not vacuous): both clamps were removed, the constructor clamp was left in place, and the test was run.

| Step | Command | EXIT | Result |
| --- | --- | --- | --- |
| Probe applied: `Note.cpp:353` clamp removed (`git apply logs/fix.patch`), then `Note.cpp:642` clamp removed by an in-place edit | (edits) | - | probe active |
| Save probe patch (both clamps removed) | `git diff -- src/core/Note.cpp > logs/mutation-probe.patch` | 0 | `logs/mutation-probe.patch` |
| Rebuild with probe | `CCACHE_DISABLE=1 cmake --build build --target zene NoteToolsTest -j4` (the `tone` attribute was added to the test, then `NoteToolsTest` alone was rebuilt) | 0 | `logs/038-build-mutated-both.log`, `logs/040-build-test-tone.log` |
| Run probe | `./NoteToolsTest aNegativeProjectKeyLoadsAsKeyZeroAndDetectsInRange` (in `build/tests`) | 1 | `FAIL! ... Actual (loaded.key()): -1, Expected (0): 0`, `logs/041-red-mutated-both-tone.log` |
| Revert probe | `git apply -R logs/mutation-probe.patch` | 0 | `git diff -- src/core/Note.cpp` is empty |
| Rebuild | `CCACHE_DISABLE=1 cmake --build build --target zene NoteToolsTest -j4` | 0 | `logs/042-build-restored.log` |
| Run guard on original code | `./NoteToolsTest aNegativeProjectKeyLoadsAsKeyZeroAndDetectsInRange` | 0 | `Totals: 3 passed, 0 failed`, `logs/043-green-restored.log` |
| Family | `ctest -R NoteToolsTest --output-on-failure` (in `build/tests`) | 0 | `100% tests passed out of 1` (1/1 executed), `logs/044-ctest-final.log` |

A first probe that removed only the `setKey` clamp still passed, because the loader's own clamp at `:642` covers it. That is the expected redundancy, and it is why the probe removes both clamps.

## Gates

| Check | Command | EXIT | Result |
| --- | --- | --- | --- |
| Feature gates | `python3 <run>/gate/feature_check.py <worktree>` (see note below) | 0 | `logs/045-feature-check-final.log`: all eight rows PASS, `RESULT: PASS` |

Note on the gate run: `feature_check.py` writes its logs to `<worktree>/../<name>-feature-check`, which is the projects directory. That location is outside the write-jail, and the unmodified script stopped at `logs.mkdir` with `PermissionError: [Errno 13]` before running any gate. I ran a copy of the same script (`logs/../gate/feature_check.py`) that changes only two lines. `ROOT` points at the real `qwen/` directory so `FIX-BASE` and the existing `gate-baselines/` cache are read unchanged, and the log directory is the run's log folder. The gate checks are unchanged.

The build also needed `CCACHE_DISABLE=1`. ccache could not create its temporary file under the shared `dsh/home/ccache-tmp-r6` path (`logs/030-build-green-guard.log`, permission denied), so the build failed before any compile. Disabling the cache affects only build speed.

## Files committed

- `tests/src/core/NoteToolsTest.cpp` (new `QDomDocument` include and the guard test)
- `docs/qwen-fixes-2026-10/cluster-new-p10-53.md` (this report)

No product file is changed. `src/core/Note.cpp` is back to its committed content (verified: the diff is empty).
