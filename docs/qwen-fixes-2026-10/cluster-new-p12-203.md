# cluster new-p12-203 — ReverbSC reports successful creation when allocation fails

Verdict: **FIXED**

## Finding

`sp_revsc_create()` returned `SP_OK` without checking `malloc()`, and `sp_revsc_init()`
ignored the auxiliary-buffer allocation and marked the state initialised before it.

## Evidence (before the fix)

| Location | Behaviour |
| --- | --- |
| `plugins/ReverbSC/revsc.c:52-55` | `*p = malloc(...)` then `return SP_OK` — no null check, so the caller gets `SP_OK` with a null state. |
| `plugins/ReverbSC/revsc.c:67` | `p->initDone = 1` ran *before* any allocation, so an uninitialised state advertised itself as ready. |
| `plugins/ReverbSC/revsc.c:72` | `sp_auxdata_alloc(&p->aux, nBytes)` return value discarded. |
| `plugins/ReverbSC/revsc.c:75-76` | the delay-line pointers were carved out of `p->aux.ptr` and `init_delay_line()`'s `memset(lp->buf, ...)` ran even when that pointer was null. |
| `plugins/ReverbSC/base.c:165-171` | `sp_auxdata_alloc()` always returned `SP_OK` and `memset(aux->ptr, 0, size)`-ed a possibly-null pointer — so there was nothing for a caller to check. |
| `plugins/ReverbSC/ReverbSC.cpp:61-62,138-139` | `sp_revsc_create()`/`sp_revsc_init()` were called unconditionally, so a refused allocation left `revsc` unusable and a later `sp_revsc_compute()` on garbage `tmpL/tmpR`. |

## Fix

* `plugins/ReverbSC/base.c` — `sp_auxdata_alloc()` returns `SP_NOT_OK` when `malloc()`
  refuses, and no longer memsets a null pointer.
* `plugins/ReverbSC/revsc.c` — `sp_revsc_create()` refuses a null out-pointer and a refused
  state allocation; `sp_revsc_init()` refuses null arguments, clears `initDone` up front,
  checks `sp_auxdata_alloc()` and only marks the state initialised once the delay lines are
  built; `sp_revsc_destroy()` tolerates a null handle so an already-refused state is safe to
  release.
* `plugins/ReverbSC/ReverbSC.cpp` — construction and `changeSampleRate()` release `revsc`
  and null it when `sp_revsc_init()` refuses, which is the bypass path `processImpl()`'s
  existing `revsc == nullptr` guard already implements (the same shape the DC blocker path
  uses since new-p9-115). No new control command, no per-period work.

No header changed, so no dependent target needed rebuilding beyond the `reverbsc` plugin
module and the new test.

## Regression

New fork-NEW ctest `ReverbSCInitTest`
(`tests/src/plugins/ReverbSCInitTest.cpp`), registered in `tests/CMakeLists.txt:1819-1840`.
It links the real `plugins/ReverbSC/revsc.c` and `plugins/ReverbSC/base.c` with `malloc`
renamed to `sp_revsc_test_malloc` (the pattern `ReverbScAllocationTest` already uses for
`dcblock.c`), so each allocation fails deterministically. The two failure cases run in a
`QProcess` child, because the unfixed code memsets through the null buffer and takes the
process down — the parent turns that crash into an ordinary assertion failure.

* `createReportsStateAllocationFailure` — a refused state allocation must yield `SP_NOT_OK`
  and a null handle.
* `initReportsDelayLineAllocationFailure` — a refused delay-line buffer must yield
  `SP_NOT_OK`, leave `initDone == 0` and `aux.ptr == nullptr`, and stay safe to feed to
  `sp_revsc_compute()`.
* `initialisesDelayLinesWhenAllocationSucceeds` — the happy path still initialises all eight
  delay lines.

## Commands and exit codes

| Step | Command | Exit |
| --- | --- | --- |
| RED build | `cmake --build build --target ReverbSCInitTest` (log `build-2.log`) | 0 |
| RED test | `ctest -R ReverbSCInitTest --output-on-failure` from `build/tests` (log `test-1.log`) | **8** |
| Fix build | `cmake --build build --target ReverbSCInitTest reverbsc` (log `build-3.log`) | 0 |
| GREEN test | `ctest -R ReverbSCInitTest --output-on-failure` from `build/tests` (log `test-2.log`) | 0 |
| Family green | `ctest -R Reverb --output-on-failure` from `build/tests` (log `test-3.log`) | 0 (4/4) |
| Reverse patch | `git diff -- plugins/ReverbSC/revsc.c plugins/ReverbSC/base.c plugins/ReverbSC/ReverbSC.cpp > logs/fix.patch && git apply -R logs/fix.patch` | 0 |
| Reverse build | `cmake --build build --target ReverbSCInitTest` (log `build-5.log`) | 0 |
| Reverse test | `ctest -R ReverbSCInitTest --output-on-failure` from `build/tests` (log `test-4-red.log`) | **8** |
| Re-apply | `git apply logs/fix.patch` | 0 |
| Re-apply build | `cmake --build build --target ReverbSCInitTest reverbsc` (log `build-6.log`) | 0 |
| Re-apply test | `ctest -R Reverb --output-on-failure` from `build/tests` (log `test-5-green.log`) | 0 (4/4) |

RED assertions (log `test-1.log`, identical after the reverse-apply in `test-4-red.log`):

```
FAIL!  : ReverbSCInitTest::createReportsStateAllocationFailure() Compared values are not the same
   Actual   (child.exitCode()): 1
   Expected (EXIT_SUCCESS)    : 0
   Loc: [../../tests/src/plugins/ReverbSCInitTest.cpp(155)]
FAIL!  : ReverbSCInitTest::initReportsDelayLineAllocationFailure() 'child.exitStatus() == QProcess::NormalExit' returned FALSE. ()
   Loc: [../../tests/src/plugins/ReverbSCInitTest.cpp(154)]
Totals: 3 passed, 2 failed, 0 skipped, 0 blacklisted, 288ms
```

Affected family: `ctest -R Reverb` executes **4** tests
(ReverbSCCreateTest, ReverbScAllocationTest, ReverbSCInitTest, ReverbSCRawProcessingTest),
all passing after the fix. `zene` and `reverbsc` were rebuilt before the runs.

## Gates

| Gate | Before the change | After the change |
| --- | --- | --- |
| `bash tests/complexity-gate.sh --check` | **1** (`gate-complexity-0.log`, 6 `REGRESSION` lines) | **1** (`gate-complexity-2.log`, the same 6 lines — none of them in a file this change touches) |
| `bash tests/file-length-gate.sh --check` | **1** (`gate-filelength-0.log`, 14 `REGRESSION` lines) | **1** (`gate-filelength-2.log`, the same 14 lines — none of them in a file this change touches) |
| `bash tests/fork-sources-gate.sh` | 0 | 0 |
| `bash tests/unregistered-tests-gate.sh` | — | 0 |
| `bash tests/duplication-gate.sh` | — | 0 |

Both required ratchets already failed on this branch before any edit (pre-existing
`REGRESSION` lines in `src/core/*` and other clusters' test sources, e.g.
`src/core/SmfInterchangeReader.cpp (504)`, `src/core/TakeLane.cpp (520)`). The two gate logs
recorded before the first edit and the two recorded after the last are byte-identical in
their `REGRESSION`/`FAIL` lines, so this change adds none and no baseline was re-anchored.

## Files committed

* `plugins/ReverbSC/base.c` (inherited, reason in `tests/upstream-modifications.txt`)
* `plugins/ReverbSC/revsc.c` (inherited, reason in `tests/upstream-modifications.txt`)
* `plugins/ReverbSC/ReverbSC.cpp` (inherited, reason in `tests/upstream-modifications.txt`)
* `tests/src/plugins/ReverbSCInitTest.cpp` (fork-NEW, registered in `tests/fork-sources.txt`
  and `tests/all-sources.txt`)
* `tests/CMakeLists.txt`, `tests/fork-sources.txt`, `tests/all-sources.txt`,
  `tests/upstream-modifications.txt`
* `docs/qwen-fixes-2026-10/cluster-new-p12-203.md`