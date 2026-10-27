FIXED — cluster new-p16-58: ZIP64 entry-count sentinel silently dropped entries

## Finding

`src/core/DawProjectZipRead.cpp` read the end record's 16-bit entry-count field as a plain count. When that field holds
`0xFFFF`, the value is the ZIP64 sentinel (the real count lives in a ZIP64 record this reader does not decode). The reader
scanned only the first 65,535 central-directory records and returned success with no deviation for the rest.

## Decision: REAL

Evidence on the pre-fix code (`git show HEAD:src/core/DawProjectZipRead.cpp`):

- `locateDirectory` (old lines 199-214) took `*count = readU16(bytes, endOffset + 10)` with no sentinel check.
- `dawProjectZipReadTolerant` (old lines 523-540) calls `scanDirectory(scan, centralOffset, count)`. The loop
  (`scanDirectory`, old lines 502-519) runs `count` times and returns; it never checks that the directory ends where the
  declared count says.
- A `0xFFFF` count with a real directory of more than 65,535 records therefore produced a successful read with no
  `report.deviations` entry. The RED run below confirms the success on the pre-fix code.

The writer side already knows about the sentinel: `src/core/DawProjectZip.cpp:145-154` (`MaxEntryCount = 0xFFFE`, with the
comment explaining that 0xFFFF is the ZIP64 sentinel). The reader did not mirror it.

## The fix

Reader only, at the cause (`locateDirectory`), placed after the spanned-archive refusal:

- `src/core/DawProjectZipRead.cpp:115` — `constexpr quint16 Zip64CountMarker = 0xFFFFu;`
- `src/core/DawProjectZipRead.cpp:207-214` — when the entry count equals the sentinel, `locateDirectory` refuses the
  whole container with `"<path> is a ZIP64 container (its end record carries the ZIP64 entry-count sentinel, 0xFFFF),
  which this reader does not support"`.
- `src/core/DawProjectZipRead.cpp:223` — `*count = entryCount;` (same value as before for every non-sentinel count).
- `src/core/DawProjectZipRead.cpp:20-21` — header comment: ZIP64 added to the list of archive-level refusals.

Callers and siblings checked:

- `rg` for end-record readers across `src` and `tests`: the only reader of the end record is `locateDirectory`
  (`src/core/DawProjectZipRead.cpp`). Both entry points go through it: `dawProjectZipReadTolerant` (used by
  `src/core/DawProjectRead.cpp:502`, the `.dawproject` import) and `dawProjectZipRead` (strict; used by
  `src/core/ProjectContainer.cpp:320` for `.mmpz` v2). Neither needs a change of its own; both now refuse by name.
- The writer never emits `0xFFFF` as a count (`MaxEntryCount = 0xFFFE`), so no archive this program writes is affected.
- Files with 65,535 entries that do not carry a ZIP64 record are malformed per the format, so they are refused. This is
  a judgement call and the one behaviour change: a foreign ZIP64 `.dawproject` now fails to import with an explicit
  error, where it used to import 65,535 entries silently and drop the rest.

## Regression test

`tests/src/core/DawProjectDeflateImportTest.cpp:632` — `aZip64EntryCountSentinelIsRefusedAndTheLargestClassicCountReads`
(next to the existing reader tests in the same class; no new file, so no registration change).

It builds a real 65,536-record classic directory (STORE, empty bodies, deterministic, no timing) with the end record's
count fields set to `0xFFFF`, and checks:

1. `dawProjectZipReadTolerant` refuses (returns false) with an error naming ZIP64, and returns no entries.
2. Strict `dawProjectZipRead` refuses the same way.
3. `dawproject.import` refuses (`ok == false`) — the user-visible path.
4. Boundary: 65,534 real records with count `0xFFFE` still read in full with no deviations.

Test matrix (step 3): normal path (item 4, plus the existing 11 tests in the class); the failure path (items 1-3); the
exhaustion boundary (item 4 at `0xFFFE`); audio-thread constraints (none — import and file reading only, no realtime
code touched); platform requirements (none beyond the existing test build).

## Commands and results

ccache note: the default `CCACHE_TEMPDIR` in this shell points at `dsh/home/ccache-tmp-r6`, which is outside the write
area and fails with `Permission denied`. Every build below set `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp/ccache-tmp-new-p16-58`
(in the build cache area); the shared ccache config is unchanged.

| Step | Command (run from the worktree unless noted) | EXIT | Log (run folder `logs/`) |
|---|---|---|---|
| Pre-check, default ccache temp | `cmake --build build --target DawProjectDeflateImportTest -j4` | 2 | `build-0-deflate-target.log` (ccache permission error) |
| Pre-check, with override | `CCACHE_TEMPDIR=… cmake --build build --target DawProjectDeflateImportTest -j4` | 0 | `build-0-deflate-target-b.log` |
| **RED** build | `CCACHE_TEMPDIR=… cmake --build build --target DawProjectDeflateImportTest -j4` | 0 | `build-1-red.log` |
| **RED** test | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R DawProjectDeflateImportTest --output-on-failure` | **8** | `test-1-red.log` — `FAIL! : …aZip64EntryCountSentinel… '!dawProjectZipReadTolerant(sentinel, …)' returned FALSE` at `DawProjectDeflateImportTest.cpp:644`; 0% passed |
| Fix applied | (edit `src/core/DawProjectZipRead.cpp`) | — | — |
| **GREEN** build | `CCACHE_TEMPDIR=… cmake --build build --target zene DawProjectDeflateImportTest -j4` | 0 | `build-2-green.log` |
| **GREEN** test | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R DawProjectDeflateImportTest --output-on-failure` | **0** | `test-2-green.log` — 100% passed |
| GREEN verbose | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R DawProjectDeflateImportTest -V` | **0** | `test-3-green-verbose.log` — `PASS : …aZip64EntryCountSentinel…()`; `Totals: 12 passed, 0 failed` |
| 4b: save fix | `git diff -- src/core/DawProjectZipRead.cpp > logs/fix.patch` | 0 | `fix.patch` (2518 bytes) |
| 4b: revert fix | `git apply -R logs/fix.patch` | **0** | — |
| 4b: rebuild reverted | `CCACHE_TEMPDIR=… cmake --build build --target zene DawProjectDeflateImportTest -j4` | 0 | `build-4b-reverted.log` |
| 4b: test reverted (must FAIL) | `ctest -R DawProjectDeflateImportTest -V` (from `build/tests`) | **8** | `test-4b-reverted.log` — `Totals: 11 passed, 1 failed` (the new test) |
| 4b: reapply fix | `git apply logs/fix.patch` | **0** | — |
| 4b: rebuild restored | `CCACHE_TEMPDIR=… cmake --build build --target zene DawProjectDeflateImportTest -j4` | 0 | `build-4b-restored.log` |
| 4b: test restored (must PASS) | `ctest -R DawProjectDeflateImportTest -V` (from `build/tests`) | **0** | `test-4b-restored.log` — `Totals: 12 passed, 0 failed` |
| Family build | `CCACHE_TEMPDIR=… cmake --build build --target DawProjectInterchangeRoundTripTest -j4` | 0 | `build-5-roundtrip.log` |
| Family build | `CCACHE_TEMPDIR=… cmake --build build --target ProjectContainerTest ProjectContainerEntriesTest ProjectContainerSaveTest -j4` | 0 | `build-6-container.log` |
| Family test | `ctest -R "DawProjectInterchangeRoundTripTest\|DawProjectDeflateImportTest\|ProjectContainerTest\|ProjectContainerEntriesTest\|ProjectContainerSaveTest" --output-on-failure` | **0** | `test-6-family.log` — 5/5 passed |
| Family build | `CCACHE_TEMPDIR=… cmake --build build --target ProjectContainerSaveAsGuiTest -j4` | 0 | `build-7-saveas.log` |
| Family test | `ctest -R "ProjectContainerSaveAsGuiTest\|MmpzV2CliRefusal\|MmpzGitDepthTest" --output-on-failure` | **8** | `test-7-family2.log` — SaveAsGui passed; MmpzV2CliRefusal passed; MmpzGitDepthTest failed (see below) |
| Baseline for MmpzGitDepthTest | (revert fix via `git apply -R`, `cmake --build build --target zene -j4`, `ctest -R MmpzGitDepthTest`, reapply) | 8 | `test-8-baseline-mmpzgit.log` — `Ran 51 tests`, `FAILED (failures=41, skipped=1)` |
| Restore after baseline | `git apply logs/fix.patch`; `cmake --build build --target zene -j4` | 0 / 0 | `build-9-restored-zene.log` |
| Gates (as specified) | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | **1** | Not a gate result: the script creates its log folder `<worktree>-feature-check/` next to the worktree, outside the write area (`PermissionError`). Run aborted before any gate ran. |
| Gates (log folder redirected) | `python3 -I logs/feature_check_redirected.py <worktree>` | **0** | `logs/gates-2-feature-check-table.txt` — `RESULT: PASS` |

The redirected gate script is a copy in the run folder (`feature_check_redirected.py`, next to `logs/`). The only changes from the
original are: `ROOT` points at the original qwen folder (so `FIX-BASE` and the cached base listings are read unchanged), and
the log folder points at the run folder. No limit, baseline or rule was changed. The base listings were already cached
(`gate-baselines/639e1d23e4b84a751f6ed90bd626d35d24727cc4.json`), so the copy wrote nothing shared.

Gate table (`logs/gates-2-feature-check-table.txt`):

```
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

## Evidence table

| Item | Value |
|---|---|
| RED command / EXIT | `ctest -R DawProjectDeflateImportTest` from `build/tests` / **8** (`test-1-red.log`) |
| GREEN command / EXIT | same command / **0** (`test-2-green.log`, `test-3-green-verbose.log`) |
| Patch proof | reverted: EXIT 8, 11 pass / 1 fail; restored: EXIT 0, 12 pass / 0 fail (`test-4b-*.log`) |
| Affected family, tests executed | 5 in the reader/container family (ProjectContainerTest, ProjectContainerEntriesTest, ProjectContainerSaveTest, DawProjectInterchangeRoundTripTest, DawProjectDeflateImportTest) — all pass; plus ProjectContainerSaveAsGuiTest and MmpzV2CliRefusal pass; MmpzGitDepthTest (51 tests) fails identically with and without the fix |
| Gate exit code (feature_check, redirected copy) | **0** (`RESULT: PASS`) |
| Gate exit code (feature_check, as specified) | **1** — log-folder write outside the write area; no gate ran |
| Files committed | `src/core/DawProjectZipRead.cpp`, `tests/src/core/DawProjectDeflateImportTest.cpp`, `docs/qwen-fixes-2026-10/cluster-new-p16-58.md` |

## Pre-existing failure, not this fix

`MmpzGitDepthTest` fails with and without the fix, with the same 41 failures out of 51 tests (1 skipped). Logs:
`test-7-family2.log` (with fix) and `test-8-baseline-mmpzgit.log` (without). The failing checks are Python byte-identity
tests on `zlib.compress` output (`tools/mmpz-git/tests/test_mmpz_git.py:99`, `Container.test_recompress_byte_identical`)
and an audible-diff test that expects a rendering difference and gets none (`test_mmpz_git.py:1018`). Neither reaches
`DawProjectZipRead.cpp`. Not investigated further, since the baseline is the same.

## Not run

- `ctest` for the full suite: the change is confined to the ZIP reader and its direct callers, which are all in the family
  above.
- GUI or audio paths: none touched. No control command added, so the ControlCommandsSnapshot canary is unaffected.

## Self-check

- **Regression test fails with the product change reverted, passes with it.** RED: `build-1-red.log` (EXIT 0), then
  `test-1-red.log` (ctest EXIT 8, assertion at `tests/src/core/DawProjectDeflateImportTest.cpp:644`). Patch proof
  (`git apply -R` / `git apply` of `logs/fix.patch`): reverted `test-4b-reverted.log` EXIT 8 with 11 pass / 1 fail;
  restored `test-4b-restored.log` EXIT 0 with 12 pass / 0 fail.
- **Deterministic.** The test uses no clock, sleep, thread or race. Its input is a fixed in-memory 65,536-record directory
  built by `stored()` and `buildZip()` (the file's existing helpers), with a fixed count override (`0xFFFF`, `0xFFFE`).
  It writes into the test's `QTemporaryDir`. The result is a function of the bytes only.
- **Fix at the cause, and complete.** `locateDirectory` (`src/core/DawProjectZipRead.cpp:207-223`) is the only reader of the
  end record. Both entry points call it (`dawProjectZipReadTolerant` via `src/core/DawProjectRead.cpp:502`;
  `dawProjectZipRead` via `src/core/ProjectContainer.cpp:320`), so both refuse the sentinel. The writer already refuses
  to write a count of 0xFFFF (`src/core/DawProjectZip.cpp:145-154`, `MaxEntryCount = 0xFFFE`), so the reader and writer now
  agree on the same value.
- **No collateral damage.** For every count other than `0xFFFF` the value stored in `*count` is unchanged
  (`src/core/DawProjectZipRead.cpp:223`). The boundary is tested: 65,534 real records with count `0xFFFE` still read in
  full with no deviations (test item 4). The existing family passes: `test-6-family.log` (5/5) and
  `test-7-family2.log` (SaveAsGui and MmpzV2CliRefusal pass). The only file type changed is a ZIP64 container (count
  sentinel `0xFFFF`), which this program never writes. Such a file used to import partially and silently; it now refuses
  with an error that names ZIP64. `MmpzGitDepthTest` fails identically before and after (`test-8-baseline-mmpzgit.log`).
- **Verdict FIXED** — the ALREADY-FIXED and FALSE-POSITIVE evidence rule does not apply.

### Gate row not passing under the pre-submit check

`submit_check.py` runs `<tools>/feature_check.py` on the worktree. That script creates its log folder
`<worktree>-feature-check/` next to the worktree, which is outside the write area for this lane, so it exits 1 with
`PermissionError` before any gate runs (`logs/gates-1-feature-check.log` is the failing run; `submit-check-1.txt` the check).
The same gate code, with only the log folder changed (`logs/feature_check_redirected.py`), passes all 8 rows with EXIT 0
(`logs/gates-2-feature-check-table.txt`). The shared gate script and its baselines were not edited. The gate row needs a
run where the sibling log folder can be created, and this lane could not do that.
