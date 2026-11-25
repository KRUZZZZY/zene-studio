FIXED

# Cluster new-p15-53 — Writer reports success with an unchecked partial readback

## Verdict: REAL, fixed

`writeDawProject()` fills `DawProjectWriteReport::bytes` and `sha256` from a
read-back of the file it just wrote (`src/core/DawProjectWrite.cpp:602-660`).
Before this fix the read-back checked only that the file OPENED:

```cpp
// src/core/DawProjectWrite.cpp, before (the block the finding pointed at)
QFile written(path);
if (!written.open(QIODevice::ReadOnly)) { ... return false; }   // open checked
container = written.readAll();                                  // result NOT checked
written.close();
const qint64 bytes = container.size();                          // partial bytes
report->bytes = bytes;
report->sha256 = dawProjectSha256(container);
...
return true;                                                    // success
```

`QIODevice::readAll()` cannot report an error: on a failed read it returns the
bytes it managed to read, and `QFile` records the failure only in
`QFileDevice::error()`. A read error after a successful open therefore produced
a report describing a partial (or empty) object while the function returned
success.

The contract it breaks is the report's own: `include/DawProjectInterchange.h:377-385`
— "What one successful write produced. `bytes` and `sha256` describe the FILE".
The container reader in the same module already honours that contract for the
read side: `readWhole()` / `refuseUnreadable()` (`src/core/DawProjectZipRead.cpp:163-190`)
refuse a file that "gave back less than it holds" and name it unreadable. The
writer's report did not.

## The fix

- `src/core/DawProjectWrite.cpp:296-304` — `armTestReadFault()`, the seam the
  container's read paths already use (`ZENE_TEST_CONTAINER_READ_FAULT`,
  `src/core/DawProjectZipRead.cpp:146-152`): opens the file, then closes it before
  the read, so the read gives back nothing while the open succeeded. Inert
  unless the variable is set. Committed on its own as `916a609f27` so the
  regression test can be shown red without the product change (see 4b below).
- `src/core/DawProjectWrite.cpp:306-349` — `readContainerBack()`: opens the file,
  records the file's own size, reads, and refuses when
  `QFileDevice::error() != QFileDevice::NoError` **or** the read returned fewer
  bytes than the file holds. Only then is `container` handed back.
- `src/core/DawProjectWrite.cpp:645` — `writeDawProject()` calls it and returns
  false with `"the container was written but cannot be read back: ..."` when the
  report cannot describe the written file. The report is left untouched, so no
  caller can see a partial identity as if it were the file. `writeDawProject`'s
  own CCN is unchanged (one call replaced the inline open check), and the new
  `readContainerBack` measures CCN 7.
- `include/DawProjectInterchange.h:530-537` — the declaration's doc now says a
  requested report that cannot describe the written file fails the write and
  leaves the file in place (the old text promised "no file left behind" for
  every failure).

Callers checked: `src/core/ControlCommandsDawProject.cpp:382-387`
(`dawproject.export` already turns a false return into a typed `Refused` with the
error text, so the new failure surfaces as a refusal and no result schema moves),
`tools/dawproject-roundtrip-proof.cpp:206-210`, and the tests below. No command
id was added and no undo step is involved (`dawproject.export` is classified
non-mutating). Nothing here runs on the audio thread: `writeDawProject` is an
export path.

## Regression test

`tests/src/core/DawProjectInterchangeRoundTripTest.cpp:2614-2663`,
`aReadBackThatGivesBackNothingIsNotReportedAsSuccess`, in the existing test file
for the area. It arms the shared read fault, calls `writeDawProject` with a
report, and asserts: the call fails, the error says the container cannot be read
back, `report.bytes == 0` and `report.sha256` is empty (no partial identity), the
container really is on disk and reads back as its two entries once the fault is
cleared (the write itself succeeded), and the same write with `report == nullptr`
still succeeds (the read-back only exists to fill a report). Finally it drives
`dawproject.export` with the fault armed and asserts a typed `Refused` whose
message says the container cannot be read back, so the shipped command path is
covered and not only the function.

## Commands and their exit codes (logs in `new-p15-53.r1/logs`)

| step | command | EXIT |
| --- | --- | --- |
| build, seam commit | `cmake --build build --target zene DawProjectInterchangeRoundTripTest -j4` (`build-1.log`) | 0 |
| family, seam inert | `cd build/tests && ctest -R '^DawProjectInterchangeRoundTripTest$' --output-on-failure` (`test-1.log`) | 0 |
| RED (test added, no fix) | `cmake --build build --target DawProjectInterchangeRoundTripTest -j4` (`build-2-red.log`) | 0 |
| RED | `cd build/tests && ctest -R '^DawProjectInterchangeRoundTripTest$' --output-on-failure` (`test-2-red.log`) | 8 |
| GREEN build | `cmake --build build --target zene DawProjectInterchangeRoundTripTest DawProjectDeflateImportTest -j4` (`build-3-green.log`) | 0 |
| GREEN slot | `./build/tests/DawProjectInterchangeRoundTripTest aReadBackThatGivesBackNothingIsNotReportedAsSuccess` (`test-4-green-slot.log`) | 0 |
| 4b revert | `git diff -- src/core/DawProjectWrite.cpp include/DawProjectInterchange.h > .../logs/fix.patch` | 0 |
| 4b revert | `git apply -R .../logs/fix.patch` | 0 |
| 4b revert build | `cmake --build build --target zene DawProjectInterchangeRoundTripTest -j4` (`build-4-reverted.log`) | 0 |
| 4b revert test (must fail) | `cd build/tests && ctest -R '^DawProjectInterchangeRoundTripTest$' --output-on-failure` (`test-5-red-reverted.log`) | 8 |
| 4b restore | `git apply .../logs/fix.patch` | 0 |
| 4b restore build | `cmake --build build --target zene DawProjectInterchangeRoundTripTest -j4` (`build-5-restored.log`) | 0 |
| 4b restore test (must pass) | `cd build/tests && ctest -R '^DawProjectInterchangeRoundTripTest$' --output-on-failure` (`test-6-green-restored.log`) | 0 |
| header includers rebuilt | `cmake --build build --target ProjectContainerTest -j4` (`build-6-container.log`) | 0 |
| whole family | `cd build/tests && ctest -R '^(DawProjectInterchangeRoundTripTest\|DawProjectDeflateImportTest\|ProjectContainerTest)$' --output-on-failure` (`test-7-family.log`) | 0 (3/3) |
| 4b re-run from the commits (`git diff 916a609f27 8635a276c9 -- src/core/DawProjectWrite.cpp include/DawProjectInterchange.h`) | `git apply -R logs/fix.patch && cmake --build build --target zene DawProjectInterchangeRoundTripTest -j4` (`build-7-red-commit.log`) then `ctest -R '^DawProjectInterchangeRoundTripTest$'` (`test-8-red-commit.log`) | build 0, test 8 |
| 4b re-run from the commits | `git apply logs/fix.patch && cmake --build ... -j4` (`build-8-green-commit.log`) then `ctest ...` (`test-9-green-commit.log`) | build 0, test 0 |
| 4b, final test (includes the export-command case): `git apply -R logs/fix.patch`, `cmake --build build --target zene DawProjectInterchangeRoundTripTest -j4` (`build-10-red-final.log`) then `ctest -R '^DawProjectInterchangeRoundTripTest$'` (`test-11-red-final.log`) | apply 0, build 0, test 8 (65 passed, 1 failed) |
| 4b, final test restored: `git apply logs/fix.patch`, build (`build-11-green-final.log`) then `ctest` (`test-12-green-final.log`) | apply 0, build 0, test 0 |

The RED failure is the finding itself: log `test-2-red.log` has
`QWARN ... QIODevice::read (QFile, ".../read-back-fault.dawproject"): device not open`
and then
`FAIL! : ... '!ok' returned FALSE. (a write whose read-back gave back nothing was reported as success)`
at `DawProjectInterchangeRoundTripTest.cpp(2632)`; totals 65 passed, 1 failed.
The same failure appears with the fix reverted in `test-5-red-reverted.log`.

## Gates

`python3 .../feature_check.py <worktree>` cannot complete in this lane: it needs
to create a baseline cache entry for the base commit in the shared
`~/.local/share/zene-bunny-fix/qwen/gate-baselines/` directory, which is
read-only here, and the guard refuses the `git checkout` it uses to build the
base tree:

```
bunny-guard: 'git -C .../qwen/gate-baselines/tree checkout -q --detach 9913d36d53...' is not allowed in a Bunny lane
Traceback ... File "feature_check.py", line 60, in base_listings ... CalledProcessError: ... exit status 97
```

(exit code 1; `logs/gates-baseline-1.log`). I ran every gate it runs, one by one,
against my worktree instead:

| gate | command | EXIT |
| --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` (`gate-fork-sources.log`) | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` (`gate-all-sources-reproduce.log`) | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` (`gate-no-upstream-regression.log`) | 0 |
| check-namespace | `tests/scripted/check-namespace` (`gate-check-namespace.log`) | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` (`gate-no-tautology.log`) | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` (`gate-doc-figures.log`) | 0 |
| complexity | `bash tests/complexity-gate.sh --check` (`gate-complexity-1.log`) | 0 — "PASS (check mode: no regressions)" |
| file-length | `bash tests/file-length-gate.sh --check` (`gate-file-length-1.log`) | 0 — "PASS (check mode: no regressions)" |

`lizard`, which the complexity gate needs, is not installed on this box and
`~/.local/lib` is read-only, so I installed it into a venv at
`$TMPDIR/lizard-venv` and ran the gate with that venv's `python3` first on
`PATH` (`venv-lizard.log`: venv EXIT=0, pip EXIT=0). The gate's own baselines in
`tests/complexity-baseline.tsv` and `tests/file-length-gate.sh` were not touched.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with
  it.** RED: `git apply -R logs/fix.patch` (EXIT=0), build (EXIT=0), ctest
  EXIT=8 (`test-11-red-final.log`: 65 passed, 1 failed, `'!ok' returned FALSE` at
  `DawProjectInterchangeRoundTripTest.cpp(2632)`). GREEN: `git apply
  logs/fix.patch` (EXIT=0), build (EXIT=0), ctest EXIT=0
  (`test-12-green-final.log`). The same red before the fix existed at all is
  `test-2-red.log` (EXIT=8, 65 passed, 1 failed); the earlier rounds are
  `test-5-red-reverted.log` / `test-6-green-restored.log`.
- **Deterministic.** No sleeps, no wall clock, no threads. The fault is injected
  through the module's existing seam `ZENE_TEST_CONTAINER_READ_FAULT`, which
  closes the file between open and read (`QFile::close()`, no platform call), so
  every platform runs the same fault at the same point. The assertion on the
  error text is an exact substring; the byte checks are exact numbers.
- **Fix at the cause, callers and siblings checked.** The cause is that
  `readAll()`'s result was used without `QFileDevice::error()` or a byte-count
  check; `readContainerBack()` now does both (a short read without an error, e.g.
  a file truncated in between, is caught by the count). Callers: the export
  command (`src/core/ControlCommandsDawProject.cpp:382-387`) already turns false
  into a typed refusal; the proof tool and the tests only read the report on
  success. Siblings: the container reader's `readWhole()`/`refuseUnreadable()`
  (`src/core/DawProjectZipRead.cpp:163-190`) already refuses a short read — this
  makes the writer agree with it; `dawProjectZipWrite()` writes through
  `QSaveFile` and does not read back, so it has nothing to check.
- **No collateral damage.** The check only fires when the read does not return
  the whole file, which cannot happen for a healthy write: the family run
  (`test-7-family.log`, 3/3 EXIT=0) passes all 65 round-trip slots including
  `theWriteReportHashesTheFileItWrote` (bytes and hash still describe the file),
  `writingWithoutAReportDoesNotReadTheArchive` (no report, no read-back),
  `DawProjectDeflateImportTest` and `ProjectContainerTest` (the .mmpz path that
  shares `writeDawProject`). Old projects and saved data are untouched: no read
  path, no format and no model changed.
- **Every gate re-run at the committed tree.** After the commit, all eight gates
  were run again against `8635a276c9` with the same commands and the same exit
  codes as the table above (0 each).
- **Completion check.** `python3 .../submit_check.py <worktree> new-p15-53`
  (`submit-check-1.log`) reports PASS for "commits on the branch", "working tree
  clean", the report file and "tests added or changed", and one FAIL: "gates
  (feature_check.py)". That row is the environment failure described above — the
  shared `gate-baselines` directory is read-only for this lane, so
  `feature_check.py` crashes while building its base listing before it prints any
  row — and every gate it would run passes individually (table above). It is the
  only item this environment cannot turn into a PASS.
- **Verdict is REAL, not a false positive.** The pre-fix code is reproduced in
  this report and the RED run shows the partial read reported as success.

## Other findings (not fixed here)

1. `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` cannot
   run in this lane because `.../qwen/gate-baselines/` is read-only and the guard
   refuses the `git checkout --detach` it uses for the base listing
   (`gates-baseline-1.log`). The completion check's "gates (feature_check.py)"
   row therefore reports FAIL for an environment reason, not for a gate that
   fails; every gate passes individually (table above).
2. `lizard` is missing from the box and `~/.local/lib` is read-only, so the
   complexity gate needs a venv interpreter on `PATH` to run at all.
3. `DawProjectZipRead.cpp` and `DawProjectWrite.cpp` now each carry their own
   three-line `armTestReadFault()` for the same environment variable. Sharing it
   would mean moving code into `DawProjectZipShared.h`; that is a cleanup outside
   this bug and was left alone.
