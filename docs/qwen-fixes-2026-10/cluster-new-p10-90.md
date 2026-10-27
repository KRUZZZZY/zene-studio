FIXED

# Cluster new-p10-90 — Recovery accepts content nested under an unrelated element

Branch `bugs/qwen-new-p10-90` (base `050/m0-train-19`, merge-base `5c1c6715d9`).

## Finding

`isRecoverableDocument()` searched every descendant of the project root for the content element
(`root.elementsByTagName(typeName)`), so a `.new` shaped
`<zene-project type="song"><head/><wrapper><song/></wrapper></zene-project>` was recognised as a
document and adopted over a valid project.

## Evidence (file:line, current tree)

- Before the fix, `src/core/DataFile.cpp:395` — `return ! root.elementsByTagName( DataFile::typeName( type ) ).item( 0 ).toElement().isNull();` (a descendant search).
- After the fix, `src/core/DataFile.cpp:398` — `return ! root.firstChildElement( DataFile::typeName( type ) ).isNull();`, with a comment at `:395-397` stating the content element is the root's own child.
- Only caller of `isRecoverableDocument`: `src/core/DataFile.cpp:245` (`parsesAsRecoverableDocument`), which feeds `isCompleteDocument` (`:300`, `:321`, `:327`) and `recoverPendingWrite` (`:347`). No other caller (`rg -n isRecoverableDocument` over `src/` and `include/`).
- Writers always place the content element directly under the root: `src/core/DataFile.cpp:428-432` (`m_head` and `m_content` appended to `root`), so every legitimate `.new` passes the new check.

## Regression test

`tests/src/core/DataFileSaveIntegrityTest.cpp:601` — `newFileWithContentNestedUnderAnotherElementIsNotAdopted()`.
A valid project is saved, then a complete, well-formed `.new` with the misnested shape (`<song s5probe="nested"/>` under `<wrapper>`) is placed beside it. The test requires:
1. the report contains `Discarded incomplete` (the temp is refused, not adopted),
2. the `.new` is removed,
3. the loaded project's probe is still `old`, and the canonical file's bytes are unchanged.

Deterministic: `QTemporaryDir`, fixed literal XML, `MessageRecorder` for the qWarning text. No timing, sleeps or races.

## Commands and exit codes

Working directory: worktree root unless stated. `LOGS` = `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-90.r1/logs`.

| Step | Command | EXIT | Log |
| --- | --- | --- | --- |
| build (first try, original code) | `cmake --build build --target DataFileSaveIntegrityTest -j4` | 2 | `build-red-1.log` (ccache could not create its temp file under `~/.local/share/zene-bunny-fix/dsh/home/ccache-tmp-r6`; no compile error) |
| build (RED, original code) | `CCACHE_DISABLE=1 cmake --build build --target DataFileSaveIntegrityTest -j4` | 0 | `build-red-2.log` |
| **RED** | `cd build/tests && ctest -R '^DataFileSaveIntegrityTest$' --output-on-failure` | **8** | `test-red-2.log`: `FAIL! ... newFileWithContentNestedUnderAnotherElementIsNotAdopted() 'report.contains("Discarded incomplete")' returned FALSE. (a misnested .new was adopted; captured: [Recovered interrupted save: ...])`; `Totals: 16 passed, 1 failed` |
| build (GREEN, with fix) | `CCACHE_DISABLE=1 cmake --build build --target DataFileSaveIntegrityTest -j4` | 0 | `build-green-1.log` |
| **GREEN** | `cd build/tests && ctest -R '^DataFileSaveIntegrityTest$' --output-on-failure` | **0** | `test-green-1.log`: `100% tests passed out of 1` |
| build family | `CCACHE_DISABLE=1 cmake --build build --target zene DataFileFormatTest -j4` | 0 | `build-green-2.log` (overwrote an earlier attempt that named a non-existent target `AudioFileProcessorView`, EXIT 2, "No rule to make target") |
| **family run** | `cd build/tests && ctest -R '^(DataFileFormatTest\|DataFileSaveIntegrityTest)$' --output-on-failure` | **0** | `test-green-2.log`: `100% tests passed out of 2` (2 tests executed) |
| 4b save patch | `git diff -- src/core/DataFile.cpp > LOGS/fix.patch` | 0 | — |
| 4b revert | `git apply -R LOGS/fix.patch` | 0 | — |
| 4b build (reverted) | `CCACHE_DISABLE=1 cmake --build build --target zene DataFileSaveIntegrityTest -j4` | 0 | `build-4b-revert.log` |
| 4b **RED** | `ctest -R '^DataFileSaveIntegrityTest$' --output-on-failure` | **8** | `test-4b-revert.log`: same `FAIL!` assertion, `Totals: 16 passed, 1 failed` |
| 4b re-apply | `git apply LOGS/fix.patch` | 0 | — |
| 4b build (restored) | `CCACHE_DISABLE=1 cmake --build build --target zene DataFileSaveIntegrityTest -j4` | 0 | `build-4b-restore.log` |
| 4b **GREEN** | `ctest -R '^DataFileSaveIntegrityTest$' --output-on-failure` | **0** | `test-4b-restore.log`: `100% tests passed out of 1` |
| AudioFileProcessorView filter | `cd build/tests && ctest -R '^AudioFileProcessorView$'` | 0 | `test-afpv-1.log`: `No tests were found!!!` (zero tests; the target is not registered — see below) |

Affected-family test count: **2** (`DataFileFormatTest`, `DataFileSaveIntegrityTest`). `DataFileSaveIntegrityTest` holds 17 QTest functions (16 pre-existing + the new one; the RED run shows 16 passed + 1 failed).

### Gates

| Command | EXIT | Log |
| --- | --- | --- |
| `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (as specified) | 1 | crashed before any gate ran: `PermissionError` on `mkdir` of `<worktree>/../zene-qwen-new-p10-90-feature-check` (that directory is outside my write-jail). The traceback was in the terminal, not kept; the later run below overwrote `feature-check-1.log`. |
| same gate logic, run from a scratch copy (`$TMPDIR/fc-p10-90/feature_check_logdir.py`, run with `python3 -I`) | **0** | `feature-check-1.log`: all 8 rows PASS, `RESULT: PASS` |

The scratch copy differs from `feature_check.py` only in two lines: `ROOT` points at the shared qwen directory (so `FIX-BASE` and the cached base listing for `5c1c6715d9` are read unchanged, no git writes), and the per-gate logs go to this run's log folder instead of `<worktree>/..`. Gate rules, commands and limits are unchanged; the complexity and file-length rows show "no new item in your files".

## Files committed

- `src/core/DataFile.cpp` — `isRecoverableDocument()` (the one-line check at `:398`, plus a 3-line comment).
- `tests/src/core/DataFileSaveIntegrityTest.cpp` — new test `newFileWithContentNestedUnderAnotherElementIsNotAdopted` (`:601`). The file is already registered as a fork source (`tests/fork-sources.txt:2413`).
- `tests/upstream-modifications.txt` — one line declaring the inherited `src/core/DataFile.cpp` change with its reason.
- `docs/qwen-fixes-2026-10/cluster-new-p10-90.md` — this report.

## Self-check

- **RED with the fix reverted, GREEN with it** — yes. `git apply -R` → build EXIT 0 → ctest EXIT 8 with the misnested temp adopted; `git apply` → build EXIT 0 → ctest EXIT 0. Commands and EXIT codes in the table above.
- **Deterministic** — yes. `QTemporaryDir` (unique temp dir, no shared state), fixed XML literals, the recovery message captured with `MessageRecorder`. No wall-clock, sleep, thread or race is involved.
- **At the cause; callers and siblings checked.**
  - The check is the only place the content element is looked up for recovery (single caller, `DataFile.cpp:245`).
  - Writers place the content directly under the root (`DataFile.cpp:428-432`), so the stricter check accepts every document this program writes.
  - Sibling **not** changed: the document loader still uses a descendant search, `src/core/DataFile.cpp:2827` (`m_content = root.elementsByTagName(typeName(m_type)).item(0)`), and the head lookup at `:2770`. This is deliberate: changing it changes what a directly opened file loads, which is outside the recovery bug. **Residual, for a reviewer decision:** a crafted temp with a same-named element *before* the real content element would now be adopted (the direct child exists) and the loader would then pick the earlier nested element. The finding's scenario (nested only, no direct child) is closed.
- **No collateral damage** — the change is one predicate in the recovery path. No size or depth limit was added or changed. Existing adoption of a valid complete `.new` (`orphanedNewFileIsAdoptedWithReport`) and the other 16 `DataFileSaveIntegrityTest` functions still pass, and `DataFileFormatTest` passes (`test-green-2.log`).
- **Verdict evidence** — not applicable (verdict is FIXED, not ALREADY-FIXED or FALSE-POSITIVE).
- **Realtime / undo / commands** — `isRecoverableDocument` runs only from the file constructor (`recoverPendingWrite`, `DataFile.cpp:339`, documented as never on audio paths). No allocation/lock/log added; no undo or control command touched.
- **Gate `feature_check.py` as specified exits 1** (write-jail `PermissionError`, see Gates). `submit_check.py` calls the same script, so its "gates" row will fail for the same reason until the script's log location is writable from a worktree; the same gate logic passes (EXIT 0, `feature-check-1.log`). Reported, not worked around in the product tree.
- **Unregistered family name** — the brief's `^AudioFileProcessorView$` matches no test (`test-afpv-1.log`). The relevant tests are the two named above.
