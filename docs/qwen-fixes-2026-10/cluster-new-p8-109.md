FIXED

# cluster new-p8-109: keep the previous `.bak` until the new backup copy succeeds

## The defect

The save path removed the previous backup before copying the new one over it, at
`src/core/DataFile.cpp:937-938` in the original tree (confirmed with
`git show HEAD:src/core/DataFile.cpp`, lines 937-938):

```cpp
		QFile::remove( fullNameBak );	// clear a stale backup; best effort
		if( ! QFile::copy( fullName, fullNameBak ) )
```

If that copy failed, the save was refused (correct), but the previous `.bak`
had already been deleted, so the only older generation was gone even though the
save never happened. The regression test reproduces exactly that: with the old
code it reads `"current"` where it expects `"older generation"`:

```text
FAIL!  : DataFileSaveIntegrityTest::failedBackupCopyKeepsThePreviousBackup() Compared values are not the same
   Actual   (readText( bak ))                     : "current"
   Expected (QStringLiteral( "older generation" )): "older generation"
```

## The fix

The copy now lands in `<name>.bak.new` and is atomically renamed over
`<name>.bak` only after it is complete (`src/core/DataFile.cpp:177-196`):

```cpp
inline bool replaceBackup( const QString & current, const QString & bak )
{
	const QString tmp = bak + QStringLiteral( ".new" );
	QFile::remove( tmp );	// clear a stale temp; best effort
	if( ! QFile::copy( current, tmp ) )
	{
		QFile::remove( tmp );	// best effort
		return false;
	}
	if( ! renameOverwrite( tmp, bak ) )
	{
		QFile::remove( tmp );	// best effort
		return false;
	}
	return true;
}
```

The save path calls it at `src/core/DataFile.cpp:964` instead of removing and
copying directly. On any staging failure the old `.bak` is untouched and the
save is refused with the existing messages; the written `<name>.new` is kept
for recovery.

## Regression tests

`tests/src/core/DataFileSaveIntegrityTest.cpp:194-205` declares two cases; the
bodies live in `tests/src/core/DataFileSaveIntegrityTestParts2.h:212-263`:

- `failedBackupCopyKeepsThePreviousBackup` — blocks the staging name with a
  non-empty directory, so the copy into `.bak.new` fails; requires the old
  `.bak` bytes, a refused save, the canonical file unchanged, and `<name>.new`
  kept.
- `successfulBackupReplacesTheOlderBackupAtomically` — the normal path; requires
  `.bak` to hold the previous content and `.bak.new` to be gone.

Both are already covered by the existing registrations: `tests/CMakeLists.txt:428`
(target source) and `tests/fork-sources.txt:779-780` (`Parts1.h`, `Parts2.h`).
`tests/upstream-modifications.txt` records this change.

## Evidence

| Step | Command | Result |
| --- | --- | --- |
| RED build | `cmake --build build --target DataFileSaveIntegrityTest` | EXIT=0 (`logs/03-build-red.log`) |
| RED test | `ctest -R DataFileSaveIntegrity --output-on-failure` (from `build/tests`) | EXIT=8, 0/1 ctest tests; binary totals 21 passed, 1 failed; `failedBackupCopyKeepsThePreviousBackup` fails (`logs/04-test-red.log`) |
| GREEN build | same build command | EXIT=0 (`logs/05-build-green.log`) |
| GREEN test | same ctest command | EXIT=0, 1/1 ctest tests passed, no `FAIL!` (`logs/06-test-green.log`; ctest hides the binary's totals on success) |
| REVERTED build | production change removed, test kept | EXIT=0 (`logs/07-build-reverted.log`) |
| REVERTED test | same ctest command | EXIT=8, 0/1 ctest tests; binary totals 21 passed, 1 failed, same `"current"` vs `"older generation"` failure (`logs/08-test-reverted.log`) |
| REAPPLIED build | production change restored | EXIT=0 (`logs/09-build-reapplied.log`) |
| REAPPLIED test | same ctest command | EXIT=0, 1/1 passed (`logs/10-test-reapplied.log`) |
| Gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <repo>` | EXIT=0; all eight rows PASS, complexity and file-length "no new item in your files" (`logs/11-gates.log`) |
| Submit check | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <repo> new-p8-109` | EXIT=0 (`logs/12-submit-check.log`) |

The RED test is a real behavior failure, not a compile failure: the build in
`03-build-red.log` completes and `04-test-red.log` shows the assertion. The
ctest logs record the failure but not the numeric exit code; EXIT=8 is what the
p2b and p4 runs returned.

## Commands run

Run directory: `/home/kruzzzzy/.config/kads/zene/state/runs/new-p8-109.r1`.
`<repo>` is `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p8-109`.

```sh
# from build/tests, RED (production fix not yet in the tree)
ctest -R DataFileSaveIntegrity --output-on-failure
# EXIT=8

# from the repository root, revert only the production diff for the reverted run
git diff > /home/kruzzzzy/.config/kads/zene/state/runs/new-p8-109.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.config/kads/zene/state/runs/new-p8-109.r1/logs/fix.patch
# EXIT=0; rebuild, then ctest as above gives EXIT=8 again

git apply /home/kruzzzzy/.config/kads/zene/state/runs/new-p8-109.r1/logs/fix.patch
# EXIT=0

python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <repo>
# EXIT=0

python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <repo> new-p8-109
# EXIT=0
```

## Other findings

- `src/core/ControlCommandsProjectLifecycle.cpp:113-187` snapshots and restores
  template files; the `.bak` path is built at `:317`. It does not remove a `.bak`
  before copying into it; its actual project save goes through
  `DataFile::writeFile`, so it inherits this fix. No separate change made.
- `src/core/ProjectRecovery.cpp:243-244` removes the recovery file and its
  sidecar, not `.bak`. `src/core/RevisionTimeline.cpp:138-140` only derives the
  displayed `.bak` names. Neither is a second backup writer.
- `tests/src/core/DataFileSaveIntegrityTest.cpp` already had `occupy()` and
  `writeText()` helpers; the new cases reuse them and add no helper.

## Self-check

- **Regression fails reverted and passes with the change.** Reverted production
  code with the new tests kept: build EXIT=0 (`logs/07-build-reverted.log`),
  `ctest -R DataFileSaveIntegrity --output-on-failure` EXIT=8 with the same
  `"current"` vs `"older generation"` assertion, binary totals 21 passed and
  1 failed (`logs/08-test-reverted.log`). Reapplied:
  build EXIT=0 (`logs/09-build-reapplied.log`), test EXIT=0, 1/1 ctest tests
  passed (`logs/10-test-reapplied.log`). The original RED run shows the
  identical failure and the same EXIT=8, 21 passed / 1 failed
  (`logs/04-test-red.log`).
- **Deterministic, no timing.** Both cases use a `QTemporaryDir` and plain files;
  there is no sleep, clock or thread. The failure is injected by pre-creating
  `<target>.bak.new` as a non-empty directory with the existing `occupy()`
  helper, so `QFile::copy` into it fails every time. The success case asserts
  only on file contents and existence, not timing.
- **Cause-level and complete.** The change is in `DataFile::writeFile()`, the
  single backup writer. `replaceBackup()` is the only new helper and is called
  once (`src/core/DataFile.cpp:964`). Sibling callers of `.bak` were checked in
  `logs/01-bak-callers.log` and `logs/sibling-callers.md`:
  `ControlCommandsProjectLifecycle.cpp` snapshots/restores templates without a
  remove-before-copy and its project save goes through `writeFile`, so it
  inherits the fix; `ProjectRecovery.cpp` removes the recovery file, not `.bak`;
  `RevisionTimeline.cpp` only displays paths. No other backup writer exists.
- **No collateral damage.** The normal save keeps its existing behavior: the
  older `.bak` is replaced by the previous canonical content, just via a staged
  copy and atomic rename; `ordinarySaveStillWritesAndKeepsABackup` and the rest
  of `DataFileSaveIntegrityTest` still pass (1/1 in `logs/06-test-green.log`,
  `logs/10-test-reapplied.log`). `disablebackup=1` still skips the block, and an
  unremovable existing `.bak` still refuses the save. Saving a new project with
  no `.bak` is unchanged. No limit, baseline or gate was modified; the eight
  feature-check rows are PASS (`logs/11-gates.log`).
- **Verdict is FIXED, not ALREADY-FIXED or FALSE-POSITIVE.** The old order is
  visible at `src/core/DataFile.cpp:937-938` of `HEAD` before this commit
  (`QFile::remove(fullNameBak)` then `QFile::copy(...)`), and the RED/reverted
  runs fail on the new assertion; the corrected order is committed in this
  change.
