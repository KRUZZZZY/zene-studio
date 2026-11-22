FIXED

# Cluster new-p10-151 — overlong groove names can overwrite distinct templates

## Finding and decision

Luna finding: `src/core/GrooveTemplate.cpp:109` (historical line) — `isWritable` checked the *normalised*
name, so a name longer than `MaxNameLength` (64) was accepted, then truncated by the constructor. Two
distinct names that share their first 64 characters collided in the pool, and the second replaced the first.

Verdict: **REAL**, and **FIXED** on this branch. Evidence on the unfixed tree (before this change):

- `GrooveTemplate::isWritable` tested `normalisedName(name).isEmpty()` (the truncated name), so
  `isWritable(64x + "-one", 48, 12)` returned true. The constructor then kept `normalisedName(name)`.
- Load path: `GrooveTemplate::loadXml` (`src/core/GrooveTemplate.cpp`, the `isWritable(element.attribute("name"), …)` call)
  and `GroovePool::loadSettings` → `set` replaced by name, so a project holding two overlong names
  sharing a 64-character prefix kept only the second. Measured: `pool.size()` 2, expected 1.
- Command path: `groove.set` (`readGrooveBody`) and `groove.extract` (`readExtractGeometry`) normalised the name
  *before* validating it; `groove.rename` normalised `to` silently. The wire schema for `name` is a plain
  `stringProperty()` (`src/core/ControlGrooveSupport.cpp`, the groove arg schemas) with no length bound, so the
  overwrite was reachable over the control socket as well.

The contract already said the right thing: the header's `isWritable` doc and `docs/GROOVE-POOL.md:19`
both say a name is "1..64 characters, trimmed", and the command error message says "the name is 1..64
characters". The code did not follow its own contract. This is therefore a defect, not a design choice.

## The fix (cause, not symptom)

1. `include/GrooveTemplate.h:149` — new `static bool nameFits(const QString& name)`; doc says an overlong name
   is refused, never cut down. `normalisedName` doc now says it is the stored form, not an admission test.
2. `src/core/GrooveTemplate.cpp:111` — `isWritable` judges the name **before** normalising:
   `if (!nameFits(name) || normalisedName(name).isEmpty()) { return false; }`.
   `nameFits` is defined at `src/core/GrooveTemplate.cpp:118` as `name.trimmed().size() <= MaxNameLength`
   (same UTF-16 unit count as `left()`, so the two agree).
3. Every caller that admits a name now passes the **raw** name to `isWritable`:
   - load: `GrooveTemplate::loadXml` already passed the raw attribute; now it refuses overlong names
     (`GroovePool::loadSettings` skips them, same as any other invalid groove element).
   - `extractGroove` (`src/core/GrooveTemplate.cpp:383`) already passed the raw caller name.
   - `groove.extract`: `readExtractGeometry` (`src/core/ControlCommandsGroove.cpp:92,105`).
   - `groove.set`: `readGrooveBody` (`src/core/ControlCommandsGroovePool.cpp:122,137`).
   - Refusal messages show `rawName.trimmed()`, so the user sees what was refused.
4. Rename target: `GroovePool::rename` (`src/core/GroovePool.cpp:91`) and `groove.rename`
   (`src/core/ControlCommandsGroovePool.cpp:259-261`) refuse an overlong `to` before normalising. The
   rename's own duplicate check (against the truncated name) could otherwise report a misleading
   "already holds" for a name the user never stored.

No command added, no schema changed. No realtime path touched: `include/GrooveTemplate.h:111-115` states that
nothing here runs on the audio thread, and the change only adds validation in command readers, the pool and
`isWritable`. No file added, so no `tests/fork-sources.txt` / `upstream-modifications.txt` registration is needed.

## Behaviour outside the bug

- A name of 64 characters or fewer, after trimming, behaves exactly as before, including on load.
- Projects written by this product never contain an overlong name: the constructor truncates, so
  `m_name` is always ≤ 64 (`GrooveTemplate.cpp:94-95` construction; the load at `:264` now refuses overlong names). Existing saved data loads as before.
- Only a hand-edited project file with a name of 65+ characters changes: it used to load truncated (and could
  replace a neighbour); it is now skipped, like any other invalid groove element.
- `normalisedName` is unchanged, and `GrooveTemplateTest.cpp:121-124` still asserts its truncation.

## Not fixed here (separate defect, reported)

Lookups by an overlong name still resolve to the 64-character groove it begins with:
`GroovePool::find` / `remove` (`src/core/GroovePool.cpp` lookup helpers), `groove.apply`, `groove.remove`
and the `from` of `groove.rename` all normalise the name before finding it. So `groove.remove` with
`name = <64 chars> + "-x"` would remove the 64-character groove. This is the same aliasing class, but
it is a *destructive lookup*, not an overwrite on save or load, so I kept this change to the write paths
the finding describes. It should get its own cluster: lookups should refuse an overlong name with NotFound.

## Tests (regression, all deterministic, no timing)

- `tests/src/core/GrooveTemplateTest.cpp`:
  - `overlongNamesAreRefusedNotTruncated` (line 55): `isWritable` refuses 65+ characters and accepts 64 and a
    trimmed name; this is the reported line.
  - `overlongNamesDoNotRenameOrLoadOntoOtherGrooves` (line 71): `GroovePool::rename` refuses an overlong target
    and creates no truncated alias.
  - `overlongGroovesDoNotLoadOverEachOther` (line 87): a project with two overlong names sharing 64 characters and
    a valid groove loads only the valid one (`pool.size()` 1).
  - Comment at `GrooveTemplateTest.cpp:119-120` corrected: it described the old "bounded, not rejected" behaviour of `normalisedName`.
- `tests/src/core/ControlGrooveCommandsTest.cpp`:
  - `overlongNamesAreRefusedOnTheWire` (line 447): `groove.rename` to an overlong name → `InvalidArgs`, no truncated name stored.
  - `overlongNamesAreRefusedOnSetAndExtract` (line 466): `groove.set` and `groove.extract` under an overlong name → `InvalidArgs`,
    and the 64-character groove beside it is byte-identical afterwards (`groove.list` result compared).

## Evidence — commands and EXIT codes

Build environment: ccache's default temp dir is not writable from this sandbox, so every build below ran with
`CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-new-p10-151` (a directory on disk under `TMPDIR`). The first attempt
(`build-1-red.log`, EXIT 2) failed on that before compiling anything; it is not counted as evidence.
Logs are in the LOG FOLDER `…/qwen/runs/new-p10-151.r1/logs/`.

| Step | Command (from `build/`) | Log | EXIT |
|---|---|---|---|
| RED build (tests only, original product code) | `cmake --build build --target GrooveTemplateTest ControlGrooveCommandsTest -j4` | `build-4-red-split3.log` | 0 |
| **RED** | `cd build/tests && ctest -R '^(GrooveTemplateTest\|ControlGrooveCommandsTest)$' -V` | `test-3-red-split3.log` | **8** |
|  | …GrooveTemplateTest: 17 passed, **3 failed** (`overlongNamesAreRefusedNotTruncated` at test line 62: `!isWritable(first)` returned FALSE; `overlongNamesDoNotRenameOrLoadOntoOtherGrooves` at 79: rename accepted; `overlongGroovesDoNotLoadOverEachOther` at 101: `pool.size()` actual 2, expected 1) | | |
|  | …ControlGrooveCommandsTest: 16 passed, **2 failed** (`overlongNamesAreRefusedOnTheWire` at 457: `groove.rename` errorKind 0, expected 3/InvalidArgs; `overlongNamesAreRefusedOnSetAndExtract` at 486: `groove.set` errorKind 0, expected 3) | | |
| Fix build | `cmake --build build --target zene GrooveTemplateTest ControlGrooveCommandsTest -j4` | `build-6-green.log` | 0 (0 `error:` lines) |
| **GREEN** | `cd build/tests && ctest -R '^(GrooveTemplateTest\|ControlGrooveCommandsTest)$' -V` | `test-5-green-verbose.log` | **0** |
|  | …GrooveTemplateTest: 20 passed, 0 failed; ControlGrooveCommandsTest: 18 passed, 0 failed (38 tests executed) | | |
| **4b, step 1 — save + reverse** | `git diff -- include/GrooveTemplate.h src/core/GrooveTemplate.cpp src/core/GroovePool.cpp src/core/ControlCommandsGroove.cpp src/core/ControlCommandsGroovePool.cpp > logs/fix.patch`; `git apply -R logs/fix.patch` | `fix.patch` | 0; 0 |
| 4b, step 2 — rebuild reverted | `cmake --build build --target zene GrooveTemplateTest ControlGrooveCommandsTest -j4` | `build-7-reverted.log` | 0 |
| **4b RED (reverted)** | `cd build/tests && ctest -R …` | `test-6-red-reverted.log` | **8** (same 5 failures as above) |
| 4b, step 3 — reapply | `git apply logs/fix.patch` | | 0 |
| 4b, step 4 — rebuild | `cmake --build build --target zene GrooveTemplateTest ControlGrooveCommandsTest -j4` | `build-8-reapplied.log` | 0 |
| **4b GREEN (reapplied)** | `cd build/tests && ctest -R …` | `test-7-green-reapplied.log` | **0** (20/20 and 18/18) |

Affected test family: `GrooveTemplateTest` 20/20 and `ControlGrooveCommandsTest` 18/18 pass with the fix, and the
same 20 and 18 are the RED/GREEN population (5 new tests, all failing without the fix).

Gates (`feature_check.py` logic, see the Self-check note on the log folder):
`python3 …/qwen/runs/new-p10-151.r1/feature-check-run/feature_check_logdir.py <worktree>` → **EXIT 0**, `gate-1-feature-check.log`:
fork-sources PASS, all-sources-reproduce PASS, no-upstream-regression PASS, check-namespace PASS, no-tautology PASS,
doc-figures PASS, complexity PASS (no new item in your files), file-length PASS (no new item in your files), RESULT PASS.

## Files committed

Product: `include/GrooveTemplate.h`, `src/core/GrooveTemplate.cpp`, `src/core/GroovePool.cpp`,
`src/core/ControlCommandsGroove.cpp`, `src/core/ControlCommandsGroovePool.cpp`.
Tests: `tests/src/core/GrooveTemplateTest.cpp`, `tests/src/core/ControlGrooveCommandsTest.cpp`.
Report: `docs/qwen-fixes-2026-10/cluster-new-p10-151.md`.
No new files (so no registration), no control command added, no canary/snapshot touched.

## Self-check

1. **Regression FAILS without the change, PASSES with it.** Reverted: `test-6-red-reverted.log`, ctest EXIT **8**, the same five
   new tests fail (`overlongNamesAreRefusedNotTruncated`, `overlongNamesDoNotRenameOrLoadOntoOtherGrooves`,
   `overlongGroovesDoNotLoadOverEachOther`, `overlongNamesAreRefusedOnTheWire`, `overlongNamesAreRefusedOnSetAndExtract`).
   Reapplied: `test-7-green-reapplied.log`, EXIT **0**, 20/20 and 18/18. The original RED (`test-3-red-split3.log`, EXIT 8) is the same five failures.
2. **Deterministic.** No sleep, timer, clock or thread. Every assertion is on values computed in-process: `isWritable`/`rename`/`set`
   results, `GroovePool::size()` and `find()`, the `groove.list` JSON before and after (`QVERIFY(after.result == before.result)`). The wire tests use
   the existing `run()`, `makeClip()`, `addNote()` and `namesInPool()` helpers from `GrooveTestSupport.h`; no new seam was needed.
3. **Cause and siblings.** Every caller of `isWritable` was checked (grep: `GrooveTemplate.cpp` load and `extractGroove`, `ControlCommandsGroove.cpp`
   `readExtractGeometry`, `ControlCommandsGroovePool.cpp` `readGrooveBody`). Each now passes the raw name. `extractGroove` has one caller
   (`ControlCommandsGroove.cpp`, the `extractGroove(` call), which passes the already-validated stored name. Rename has two entries, both covered
   (`GroovePool::rename`, `groove.rename`). Lookups (`find`/`remove`/`apply`/rename-from) are **not** changed and are reported above as a separate defect.
4. **No collateral damage.** Names of 64 or fewer characters, after trimming, take the same path as before; a 64-character name still loads and saves
   (the existing `GrooveTemplateTest` and `ControlGrooveCommandsTest` round-trip, extract, apply, rename and undo tests all pass, 20/20 and 18/18).
   The only changed behaviour is the one the finding asks for. A hand-edited project with a 65+ character groove name is now skipped on load. The refusal
   matches the existing `1..64 characters` contract in the header, `docs/GROOVE-POOL.md:19` and the error text. No limit was added beyond that contract, and
   no legitimate caller passes an overlong name (product saves never write one).
5. Verdict is FIXED, so the ALREADY-FIXED / FALSE-POSITIVE item does not apply.
6. **Sibling commands in the changed command files** (`ControlCommandsGroove.cpp`, `ControlCommandsGroovePool.cpp`; ids from their `cmd.id =` lines):
   - `groove.set` (`ControlCommandsGroovePool.cpp:316`, reader `readGrooveBody`): **fixed** (raw name judged).
   - `groove.extract` (`ControlCommandsGroove.cpp:248`, reader `readExtractGeometry`): **fixed** (raw name judged).
   - `groove.rename` (`ControlCommandsGroovePool.cpp:369`): `to` **fixed** (`nameFits`); `from` is a lookup, reported below.
   - `groove.list` (`ControlCommandsGroove.cpp:229`, lookup at `:136`), `groove.remove` (`ControlCommandsGroovePool.cpp:347`, lookup normalises `name`),
     `groove.apply` / `groove.quantize` (in `ControlCommandsGrooveEdit.cpp`, not changed): **not changed**. They normalise a lookup name, so an overlong
     lookup still resolves to the 64-character groove. This is the separate lookup defect reported above.
   - Undo inverses (`ControlCommandsGroovePool.cpp:204,244,306`; `ControlCommandsGroove.cpp:214`) replay `groove.set` / `groove.remove` / `groove.rename`
     with names that were already stored, so they are ≤ 64 and pass the new checks unchanged.

**Gate note (honest).** `feature_check.py` as shipped fails in this sandbox with `PermissionError` creating `<parent>/zene-qwen-new-p10-151-feature-check`
(outside the write-jail; output in the conversation, exit 1). Its logic is unchanged in a copy (`feature-check-run/feature_check_logdir.py`) that writes logs to
this LOG FOLDER: all six gates and both ratchets PASS, RESULT PASS, exit **0** (`gate-1-feature-check.log`). The shipped script will still fail in this sandbox until it can
write its log folder; that is an environment limit, not a gate result.

**submit_check.py** (`submit-check-1.log`, before commit): FAIL on "commits on the branch", "working tree clean" and "tests added or changed"
(all three are resolved by the commit this report is part of); FAIL on "gates" for the log-folder reason above. Re-run after the commit: see the final message.
