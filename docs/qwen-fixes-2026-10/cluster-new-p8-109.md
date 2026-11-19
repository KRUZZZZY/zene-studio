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
| RED test | `ctest -R DataFileSaveIntegrityTest --output-on-failure` (from `build/tests`) | EXIT=1, 0/1 passed; `failedBackupCopyKeepsThePreviousBackup` fails (`logs/04-test-red.log`) |
| GREEN build | same build command | EXIT=0 (`logs/05-build-green.log`) |
| GREEN test | same ctest command | EXIT=0, 1/1 passed (`logs/06-test-green.log`) |
| REVERTED build | production change removed, test kept | EXIT=0 (`logs/07-build-reverted.log`) |
| REVERTED test | same ctest command | EXIT=1, 0/1 passed, same `"current"` vs `"older generation"` failure (`logs/08-test-reverted.log`) |
| REAPPLIED build | production change restored | EXIT=0 (`logs/09-build-reapplied.log`) |
| REAPPLIED test | same ctest command | EXIT=0, 1/1 passed (`logs/10-test-reapplied.log`) |
| Gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <repo>` | EXIT=0; all eight rows PASS, complexity and file-length "no new item in your files" (`logs/11-gates.log`) |

The RED test is a real behavior failure, not a compile failure: the build in
`03-build-red.log` completes and `04-test-red.log` shows the assertion.

## Other findings

- `src/core/ControlCommandsProjectLifecycle.cpp:133-187` snapshots and restores
  template files including `.bak`, but it does not remove a `.bak` before
  copying into it; its actual project save goes through `DataFile::writeFile`,
  so it inherits this fix. No separate change made.
- `src/core/ProjectRecovery.cpp:243-244` removes the recovery file and its
  sidecar, not `.bak`. `src/core/RevisionTimeline.cpp:138-140` only derives the
  displayed `.bak` names. Neither is a second backup writer.
- `tests/src/core/DataFileSaveIntegrityTest.cpp` already had `occupy()` and
  `writeText()` helpers; the new cases reuse them and add no helper.
