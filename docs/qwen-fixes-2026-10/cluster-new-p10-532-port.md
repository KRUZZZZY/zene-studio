FIXED

# Cluster new-p10-532-port

**Verdict: FIXED** - the finding was REAL; the Soundpipe status codes in
`plugins/ReverbSC/base.c` were repaired on this branch and the repair is now covered by a
deterministic regression that was re-proved RED/GREEN in the project build tree.

## The bug and why it is real

`plugins/ReverbSC/base.h:14-15` defines the status constants of the Soundpipe API:

```c
#define SP_OK 1
#define SP_NOT_OK 0
```

The initial ReverbSC import (`git show 0cb6c72e1:plugins/ReverbSC/base.c`) returned the
*failure* code `0` on its success paths:

| Function (upstream import) | Upstream success return | Upstream line |
|---|---|---|
| `sp_create` | `return 0;` | 20 |
| `sp_createn` | `return 0;` | 36 |
| `sp_destroy` | `return 0;` | 44 |
| `sp_process` | `return 0;` | 94 |

Every other status entry point of the same file already answered `SP_OK`
(`sp_process_raw`, `sp_process_spa`, `sp_process_plot`, `sp_auxdata_alloc`,
`sp_auxdata_free`, `sp_set`, `sp_out`), so the four paths above contradicted the
header's contract: a caller that tests the result against `SP_OK` reads a successful
create/destroy/process as a failure.

## Where the fix is now

| Location | State |
|---|---|
| `plugins/ReverbSC/base.c:7` | `sp_create` delegates to `sp_createn` and therefore reports `SP_OK`. |
| `plugins/ReverbSC/base.c:38` | `sp_createn` returns `SP_OK` (from `b0ee8d4b0`, cluster new-p10-533, already on this branch). |
| `plugins/ReverbSC/base.c:45` | `sp_destroy` returns `SP_OK` (fixed by this cluster, commit `a8c32288e`). |
| `plugins/ReverbSC/base.c:95` | `sp_process` returns `SP_OK` (fixed by this cluster, commit `a8c32288e`). |
| `plugins/ReverbSC/base.c:121,138,162,170,176,188,198` | the sibling status entry points already answer `SP_OK`; `sp_out` answers `SP_NOT_OK` for an invalid channel (`base.c:195`). |

## Sibling and caller audit

Every status-returning entry point of `base.c` was checked after the change, and every
caller of the changed functions was checked for a comparison against the old `0`:

* `base.c` siblings - `sp_create`, `sp_createn`, `sp_destroy`, `sp_process`,
  `sp_process_raw`, `sp_process_spa`, `sp_process_plot`, `sp_auxdata_alloc`,
  `sp_auxdata_free`, `sp_set`, `sp_out`: all success paths answer `SP_OK`, all failure
  paths answer `SP_NOT_OK`.
* Plugin siblings - `plugins/ReverbSC/dcblock.c:25,32,42,58,63,78` and
  `plugins/ReverbSC/revsc.c:54,80,89,159,173,275` already use the same
  `SP_OK`/`SP_NOT_OK` convention, so the two helper modules stay consistent.
* Callers - `plugins/ReverbSC/ReverbSC.cpp:58` (`sp_create(&sp)`) and
  `plugins/ReverbSC/ReverbSC.cpp:75` (`sp_destroy(&sp)`) and
  `plugins/ReverbSC/revsc.c:72` (`sp_auxdata_alloc(&p->aux, nBytes)`) ignore the returned
  status; no caller in the tree compares these results against `SP_OK`/`SP_NOT_OK`, so
  the `0` -> `SP_OK` change is behaviour-preserving for existing callers.

## Regression test

`tests/src/plugins/ReverbSCBaseTest.cpp` (registered in `tests/CMakeLists.txt:1178`,
`tests/all-sources.txt` and `tests/fork-sources.txt`) asserts the status contract of
`base.c`:

* `createReportsSpOk` - `sp_createn`/`sp_create` answer `SP_OK`.
* `createRefusesInvalidArguments` - `sp_createn` answers `SP_NOT_OK` for a null
  out-pointer and for a non-positive channel count, and clears the out-pointer.
* `destroyReportsSpOk` - `sp_destroy` answers `SP_OK`.
* `outReportsChannelStatus` - `sp_out` answers `SP_OK` for a valid channel and
  `SP_NOT_OK` for an out-of-range channel.
* `setReportsSpOk`, `auxDataReportsSpOk` - `sp_set`, `sp_auxdata_alloc`,
  `sp_auxdata_free` answer `SP_OK`.
* `rawProcessingReportsSpOk`, `plotProcessingReportsSpOk` - `sp_process_raw` and
  `sp_process_plot` answer `SP_OK` and run the expected number of frames.

The test is deterministic: it uses no wall-clock timing, no sleep and no thread; the
only seam is the callback that `base.c` invokes (`fillFrame`), which counts the frames
it receives, and the sample count is pinned with `data->len = 2`.

`sp_process` cannot be exercised in this build because `base.h:4` defines
`NO_LIBSNDFILE`, which compiles that function out (`base.c:48-98`); `sp_process_spa` is
likewise compiled out (`USE_SPA` is not defined). Both are covered by the source-level
evidence above.

## Evidence table

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-532-port.r4/logs`.

| Purpose | Command | Exit | Log |
|---|---|---:|---|
| Build the four ReverbSC test targets (baseline) | `cmake --build build --target ReverbSCBaseTest ReverbSCCreateTest ReverbSCRawProcessingTest ReverbScAllocationTest` | 0 | `build-1.log` |
| Baseline family run | `cd build/tests && ctest -R 'ReverbSC\|ReverbSc' --output-on-failure` | 0 (4/4 passed) | `test-1.log` |
| Build the strengthened regression | `cmake --build build --target ReverbSCBaseTest` | 0 | `build-2.log` |
| GREEN before the sensitivity run | `cd build/tests && ctest -R '^ReverbSCBaseTest$' --output-on-failure` | 0 (1/1 passed) | `test-2.log` |
| Product patch of this cluster | `git diff a8c32288e^ a8c32288e -- plugins/ReverbSC/base.c > $LOGS/fix.patch` | 0 | `fix.patch` |
| **RED** - remove the product fix | `git apply -R $LOGS/fix.patch` | 0 | - |
| Rebuild after the revert | `cmake --build build --target ReverbSCBaseTest` | 0 | `build-3.log` |
| **RED** - regression must fail | `cd build/tests && ctest -R '^ReverbSCBaseTest$' --output-on-failure` | **8** (0/1 passed) | `test-3.log` |
| **RED (full finding)** - also revert `sp_createn`'s success return to `0` | inline edit of `plugins/ReverbSC/base.c:38` | 0 | - |
| Rebuild | `cmake --build build --target ReverbSCBaseTest` | 0 | `build-4.log` |
| **RED (full finding)** - regression must fail | `cd build/tests && ctest -R '^ReverbSCBaseTest$' --output-on-failure` | **8** (0/1 passed) | `test-4.log` |
| Restore `sp_createn` | inline edit of `plugins/ReverbSC/base.c:38` | 0 | - |
| Restore the product fix | `git apply $LOGS/fix.patch` | 0 | - |
| Rebuild after restoring the fix | `cmake --build build --target ReverbSCBaseTest` | 0 | `build-5.log` |
| **GREEN** - regression must pass | `cd build/tests && ctest -R '^ReverbSCBaseTest$' --output-on-failure` | **0** (1/1 passed) | `test-5.log` |
| Full build of the app and the plugin | `cmake --build build --target zene reverbsc` | 0 | `build-6.log` |
| **GREEN** - whole family | `cd build/tests && ctest -R 'ReverbSC\|ReverbSc' --output-on-failure` | **0** (4/4 passed) | `test-6.log` |
| Complexity ratchet | `bash tests/complexity-gate.sh --check` | 1 (pre-existing items only, none in a file this cluster changed - see below) | `gate-complexity-1.log` |
| File-length ratchet | `bash tests/file-length-gate.sh --check` | 1 (pre-existing items only, none in a file this cluster changed - see below) | `gate-filelength-1.log` |
| All gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-532-port` | **0** (8/8 rows PASS) | `gate-feature-check-1.log` |

Affected family: 4 tests executed (`ReverbSCBaseTest`, `ReverbSCCreateTest`,
`ReverbScAllocationTest`, `ReverbSCRawProcessingTest`), 4 passed.

The two ratchet scripts exit 1 on the train by design: their listed regressions belong
to other clusters (for example `tests/src/core/VideoTrackTest.cpp (589)` and
`tests/src/gui/ArrangerTimelineTest.cpp (689)` for the file-length gate) and none of
them is in `plugins/ReverbSC/base.c` or `tests/src/plugins/ReverbSCBaseTest.cpp`.
`feature_check.py` compares the listings against the branch base and reports
`PASS complexity  no new item in your files` and
`PASS file-length  no new item in your files`.

### RED evidence (assertion text)

With the product fix reverted (`test-3.log`):

```
FAIL!  : ReverbSCBaseTest::createReportsSpOk() Compared values are not the same
   Actual   (sp_destroy(&multiChannel)): 0
   Expected (SP_OK)                    : 1
FAIL!  : ReverbSCBaseTest::destroyReportsSpOk() Compared values are not the same
   Actual   (sp_destroy(&data)): 0
   Expected (SP_OK)            : 1
...
0% tests passed, 1 tests failed out of 1
```

With `sp_createn`'s success return reverted as well (`test-4.log`):

```
FAIL!  : ReverbSCBaseTest::createReportsSpOk() Compared values are not the same
   Actual   (multiCreateStatus): 0
   Expected (SP_OK)            : 1
```

## Self-check

1. **Does the regression test FAIL with the product change reverted and PASS with it?**
   Yes. RED: `git apply -R $LOGS/fix.patch` + rebuild + `ctest -R '^ReverbSCBaseTest$'`
   exited **8** with `Actual 0 / Expected SP_OK 1` (`test-3.log`); with `sp_createn`'s
   success return also reverted it exited **8** with `Actual (multiCreateStatus) 0 /
   Expected (SP_OK) 1` (`test-4.log`). GREEN: after `git apply $LOGS/fix.patch` + rebuild
   the same ctest exited **0**, 1/1 passed (`test-5.log`), and the whole ReverbSC family
   exited **0**, 4/4 passed (`test-6.log`).
2. **Is the test deterministic?** Yes. No wall-clock timing, no sleep, no thread and no
   race. The only injected values are the frame counter passed as the callback user data
   and the pinned sample count `data->len = 2`; the callback (`fillFrame`) increments a
   plain counter, and the assertions compare the exact number of processed frames.
3. **Is the fix at the cause and complete?** Yes. The cause was the status value itself:
   four success paths of `plugins/ReverbSC/base.c` returned `0`, which is `SP_NOT_OK` in
   `base.h:15`. All success paths of `base.c` now answer `SP_OK` and all failure paths
   answer `SP_NOT_OK` (table in "Where the fix is now"). Every sibling entry point of the
   same file and of the sibling modules (`dcblock.c`, `revsc.c`) was checked and follows
   the same convention, and every caller of the changed functions
   (`ReverbSC.cpp:58,75`, `revsc.c:72`) was checked: none of them consumes the returned
   status, so the change cannot alter their behaviour.
4. **Any collateral damage?** None. No limit, check or new condition was added to the
   product code - the only product change is the returned constant. The plugin still
   loads, initialises and processes exactly as before (`zene` and `reverbsc` build
   clean, exit 0, `build-6.log`), projects and saved data are untouched (no serialisation
   or project format was touched), and the change is confined to a helper API whose
   result the plugin ignores.
5. **Real time, undo and commands?** Nothing on the audio thread was touched: the
   changed functions are the Soundpipe setup/teardown helpers, and the only product edit
   is a return value - no allocation, free, lock, wait, I/O or logging was added
   anywhere. No undo step and no control command was added (the command snapshot is
   untouched, so the `ControlCommandsSnapshot` canary does not move).
6. **Verdict evidence (asked for ALREADY-FIXED / FALSE-POSITIVE verdicts).** Not needed
   here: the verdict is FIXED and the evidence is the upstream import
   (`git show 0cb6c72e1:plugins/ReverbSC/base.c`, success returns `0` at lines 20, 36, 44,
   94) versus `plugins/ReverbSC/base.h:14-15` (`SP_OK` is 1, `SP_NOT_OK` is 0).

## Files committed

- `plugins/ReverbSC/base.c` - the product fix (`sp_destroy`, `sp_process` answer `SP_OK`).
- `tests/src/plugins/ReverbSCBaseTest.cpp` - the regression (Zene-new file, registered
  in `tests/fork-sources.txt`).
- `tests/CMakeLists.txt` - registers the `ReverbSCBaseTest` target and links
  `plugins/ReverbSC/base.c` into it.
- `tests/all-sources.txt` - lists the new test source.
- `tests/fork-sources.txt` - lists the new test source in both recipe copies.
- `tests/upstream-modifications.txt` - records the inherited change of
  `plugins/ReverbSC/base.c`.
- `docs/qwen-fixes-2026-10/cluster-new-p10-532-port.md` - this report.
