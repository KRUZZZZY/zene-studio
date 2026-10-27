FIXED

# Cluster new-p10-90 — Recovery accepts content nested under an unrelated element

Branch `bugs/qwen-new-p10-90` (base `050/m0-train-19`, merge-base `5c1c6715d9`).

## Finding

`isRecoverableDocument()` searched every descendant of the project root for the content element
(`root.elementsByTagName(typeName)`), so a `.new` shaped
`<zene-project type="song"><head/><wrapper><song/></wrapper></zene-project>` was recognised as a
document and adopted over a valid project.

Round 1 (review) found a second shape still accepted: a nested same-named element *before* a direct
content child. The first version checked only that a direct child existed, so the temp was adopted and
the loader then selected the nested element.

## Evidence (file:line, current tree)

- `src/core/DataFile.cpp:382` — `DataFile::isRecoverableDocument()`.
- `src/core/DataFile.cpp:400-401` — the check: the first element of the content name in document order (the same `elementsByTagName(...).item(0)` call the loader uses) must exist and be a child of the root:
  `const QDomElement content = root.elementsByTagName( DataFile::typeName( type ) ).item( 0 ).toElement();`
  `return ! content.isNull() && content.parentNode() == root;`
- The loader's selection is unchanged: `src/core/DataFile.cpp:2830` — `m_content = root.elementsByTagName(typeName(m_type)).item(0).toElement();`. The check therefore accepts exactly the element loading would take, and only when that element is the root's own child.
- Only caller of `isRecoverableDocument`: `src/core/DataFile.cpp:245` (`parsesAsRecoverableDocument`), which feeds `isCompleteDocument` (`:300`, `:321`, `:327`) and `recoverPendingWrite` (`:347`). No other caller (`rg -n isRecoverableDocument src include`).
- Writers always place the content element directly under the root: `src/core/DataFile.cpp:428-432` (`m_head` and `m_content` appended to `root`), so every legitimate `.new` passes.

## Regression tests

Both in `tests/src/core/DataFileSaveIntegrityTest.cpp` (already a fork source, `tests/fork-sources.txt:2413`):

1. `newFileWithContentNestedUnderAnotherElementIsNotAdopted()` (`:601`) — `<head/><wrapper><song s5probe="nested"/></wrapper>`: no direct child at all.
2. `newFileWithNestedContentBeforeDirectContentIsNotAdopted()` (`:640`) — `<head/><wrapper><song s5probe="nested"/></wrapper><song s5probe="direct"/>`: a direct child exists, but the nested element comes first in document order (the round-1 blocker).

Each requires: the report contains `Discarded incomplete` (refused, not adopted); the `.new` is removed; the loaded project's probe is still `old`; the canonical file's bytes are unchanged.

Deterministic: `QTemporaryDir`, fixed literal XML, `MessageRecorder` for the qWarning text. No timing, sleeps or races.

## Merge with `050/m0-train-20` — NOT done (blocked)

The brief asked for `git merge 050/m0-train-20` first. The lane's `bunny-guard` hook refused it:

- `git merge 050/m0-train-20 -m …` → EXIT 97. Message: `bunny-guard: … is not allowed in a Bunny lane (AGENTS.md rules 1, 2, 11, 12). Stage explicit paths and commit on your own branch only.` (`merge-1.log`). I did not work around the hook (no plumbing merge, rebase or manual tree copy).
- Read-only `git merge-tree --write-tree --name-only HEAD 050/m0-train-20` → EXIT 1: the only conflict is `tests/upstream-modifications.txt` (`merge-tree-check.log`), as the brief said.

The rework below does not depend on train-20, so it is done on `bugs/qwen-new-p10-90` as it stands. The train-20 merge needs the owner or the merge train to run it (keep both sides of `tests/upstream-modifications.txt`).

The review's referenced fix commit `ff0d3b0c8a` exists in the object store but on no branch, so I did not cherry-pick it; the fix above is implemented here.

## Commands and exit codes

Working directory: worktree root unless stated. `LOGS` = `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-90.r2/logs`.

| Step | Command | EXIT | Log |
| --- | --- | --- | --- |
| merge (requested) | `git merge 050/m0-train-20 -m …` | 97 | `merge-1.log` (bunny-guard refusal) |
| conflict check (read-only) | `git merge-tree --write-tree --name-only HEAD 050/m0-train-20` | 1 | `merge-tree-check.log` (conflict only in `tests/upstream-modifications.txt`) |
| build (RED, validator not yet fixed) | `CCACHE_DISABLE=1 cmake --build build --target DataFileSaveIntegrityTest -j4` | 0 | `build-red-1.log` |
| **RED** | `cd build/tests && ctest -R '^DataFileSaveIntegrityTest$' --output-on-failure` | **8** | `test-red-1.log`: `FAIL! … newFileWithNestedContentBeforeDirectContentIsNotAdopted() 'report.contains("Discarded incomplete")' returned FALSE. (a nested-before-direct .new was adopted; captured: [Recovered interrupted save: …])`; `Totals: 17 passed, 1 failed` |
| build (GREEN) | `CCACHE_DISABLE=1 cmake --build build --target zene DataFileSaveIntegrityTest DataFileFormatTest -j4` | 0 | `build-green-1.log` |
| **GREEN, family** | `cd build/tests && ctest -R '^(DataFileFormatTest\|DataFileSaveIntegrityTest)$' --output-on-failure` | **0** | `test-green-1.log`: `100% tests passed out of 2` |
| 4b save patch | `git diff -- src/core/DataFile.cpp > LOGS/fix.patch` | 0 | `fix.patch` |
| 4b revert | `git apply -R LOGS/fix.patch` | 0 | — |
| 4b build (reverted) | `CCACHE_DISABLE=1 cmake --build build --target zene DataFileSaveIntegrityTest -j4` | 0 | `build-4b-revert.log` |
| 4b **RED** | `ctest -R '^DataFileSaveIntegrityTest$' --output-on-failure` | **8** | `test-4b-revert.log`: the same nested-before-direct `FAIL!`; `Totals: 17 passed, 1 failed` |
| 4b re-apply | `git apply LOGS/fix.patch` | 0 | — |
| 4b build (restored) | `CCACHE_DISABLE=1 cmake --build build --target zene DataFileSaveIntegrityTest -j4` | 0 | `build-4b-restore.log` |
| 4b **GREEN** | `ctest -R '^DataFileSaveIntegrityTest$' --output-on-failure` | **0** | `test-4b-restore.log`: `100% tests passed out of 1` |

Affected-family test count: **2** (`DataFileFormatTest`, `DataFileSaveIntegrityTest`). `DataFileSaveIntegrityTest` now holds 18 QTest functions (16 pre-existing + the two new ones; the RED run shows 17 passed + 1 failed).

`^AudioFileProcessorView$` matches no registered test (round 1, `test-afpv-1.log`: `No tests were found!!!`), so the focused family is the two `DataFile*` tests above.

### Gates

| Command | EXIT | Log |
| --- | --- | --- |
| `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (as specified) | 1 | `feature-check-official.log`: `PermissionError` creating `<worktree>/../zene-qwen-new-p10-90-feature-check` (outside my write-jail) |
| same gate logic from a scratch copy, run with `python3 -I`, logs to `LOGS/feature-check` | **0** | `feature-check-1.log`: all 8 rows PASS, `RESULT: PASS` |

The scratch copy differs from `feature_check.py` in two lines only: `ROOT` points at the shared qwen directory (so `FIX-BASE` and the cached base listing for `5c1c6715d9` are read unchanged; no git writes), and the per-gate logs go to this run's log folder. Gate rules, commands and limits are unchanged. The complexity and file-length rows show "no new item in your files".

## Files committed

- `src/core/DataFile.cpp` — `isRecoverableDocument()` (`:382-401`): first-match direct-child check.
- `tests/src/core/DataFileSaveIntegrityTest.cpp` — two regressions (`:601`, `:640`).
- `tests/upstream-modifications.txt` — the one-line declaration of the inherited `src/core/DataFile.cpp` change, with its reason.
- `docs/qwen-fixes-2026-10/cluster-new-p10-90.md` — this report.

## Self-check

- **RED with the fix reverted, GREEN with it** — yes, for both regressions. Round 2 (nested-before-direct): `git apply -R` → build EXIT 0 → ctest EXIT 8 with the temp adopted; `git apply` → build EXIT 0 → ctest EXIT 0. Round 1 (nested-only) RED/GREEN is in the previous report's table and was re-run in the family GREEN above (2/2).
- **Deterministic** — yes. `QTemporaryDir`, fixed XML literals, the recovery message captured with `MessageRecorder`. No wall-clock, sleep, thread or race.
- **At the cause; callers and siblings checked.**
  - The check is the only recovery-side lookup of the content element (single caller, `DataFile.cpp:245`).
  - It uses the same first-match selection the loader uses (`DataFile.cpp:2830`), so an accepted temp always loads the direct child. The round-1 gap (nested element before a direct child) is closed by `newFileWithNestedContentBeforeDirectContentIsNotAdopted`.
  - The loader is unchanged, so directly opened files load exactly as before.
  - The head lookup (`DataFile.cpp:2770`) is also a descendant search; it is not part of this bug and is unchanged. Recovery does not check `head`, so a temp with a nested `head` is not refused by this change.
- **No collateral damage** — one predicate in the recovery path; no size or depth limit added or changed. Adoption of a valid complete `.new` (`orphanedNewFileIsAdoptedWithReport`) and all other `DataFileSaveIntegrityTest` functions still pass, and `DataFileFormatTest` passes.
- **Verdict evidence** — not applicable (FIXED, not ALREADY-FIXED or FALSE-POSITIVE).
- **Realtime / undo / commands** — `isRecoverableDocument` runs only from the file constructor via `recoverPendingWrite` (`DataFile.cpp:339`, documented as never on audio or realtime paths). No allocation, lock or log added; no undo or control command touched.
- **Gate `feature_check.py` as specified exits 1** (write-jail `PermissionError`, see Gates). `submit_check.py` calls the same script, so its gates row fails for the same reason; the same gate logic passes from the redirected copy (EXIT 0). Reported, not worked around in the product tree.
- **Train-20 merge not performed** — blocked by the bunny-guard hook (see the merge section). The conflict is confined to `tests/upstream-modifications.txt`.
