FIXED

# cluster new-p14-265 — Reverb destruction frees uninitialized aux after early init failure

## Verdict: FIXED

`sp_revsc_create()` allocated the state with `malloc()`, leaving `aux.ptr`
uninitialized (garbage). If `sp_revsc_init(NULL, p)` returned at the
null-argument check before `sp_auxdata_alloc()`, a later
`sp_revsc_destroy()` called `sp_auxdata_free()` → `free(garbage)`.

```c
// plugins/ReverbSC/revsc.c:52-61 (before)
*p = malloc(sizeof(sp_revsc));   // aux.ptr / aux.size uninitialized
...
// plugins/ReverbSC/revsc.c:96-105 (before)
sp_auxdata_free(&pp->aux);       // free(uninitialized aux.ptr)
free(*p);
```

Same risk when `sp_auxdata_alloc()` itself failed: `revsc.c:81-83` returns
`SP_NOT_OK` with `aux.ptr == NULL` — safe by itself, but the destroy path
still freed an aux struct whose validity was only implied by `initDone`.

## Fix (smallest correct change, at the cause)

| File | Change |
| --- | --- |
| `plugins/ReverbSC/revsc.c:56` | `malloc()` → `calloc(1, sizeof(sp_revsc))` — new state starts zeroed, so `aux.ptr == NULL` and `aux.size == 0` until init allocates them. |
| `plugins/ReverbSC/revsc.c:102-104` | `sp_auxdata_free()` only when `initDone != 0` (i.e. delay lines were actually built). An uninitialized or failed-init state frees nothing. |
| `plugins/ReverbSC/revsc.c:105-106` | `*p = NULL` after `free(*p)` — a double destroy through the same handle is now a null-handle refusal, not a use-after-free. |
| `plugins/ReverbSC/base.c:177-184` | `sp_auxdata_free()` null-checks `aux`, and after freeing sets `ptr = NULL; size = 0`. A second free of the same auxdata is a no-op. |
| `tests/CMakeLists.txt:2938-2941` | `ReverbSCInitTest` now also renames `calloc` → `sp_revsc_test_calloc` (create() uses calloc; both allocation sites are fail-injectable). |

Behaviour otherwise unchanged: happy path identical (zeroed state, aux
allocated by init, delay lines built, `initDone = 1`), no audio-thread
allocation/free/lock/log added, no new control command, no header change.

Siblings checked: `ReverbSC.cpp:85-97` create/init/destroy sequence —
create failure → `destroyState()` skips destroy (handle null); init failure →
`destroyState()` → `sp_revsc_destroy()` now skips the aux free and frees
only the zeroed state. `sp_auxdata_alloc`/`free` callers: only
`revsc.c` and `ReverbSCBaseTest` (covers its own aux). Re-init of an
already-initialized state (sample-rate change path) — `initDone` is reset
to 0 before the new `sp_auxdata_alloc`, exactly as before.

## Regression test

Extended `tests/src/plugins/ReverbSCInitTest.cpp` (existing ctest
`ReverbSCInitTest`, no new file):

* `destroyAfterFailedInitIsSafe` — `create()` + `sp_revsc_init(NULL, p)`
  (refused at the null check) + poison the uninitialized aux with `0xA5`
  bytes (makes the garbage deterministic) + `destroy()`. Child must exit
  normally: `SP_NOT_OK` from init, `SP_OK` from destroy, handle nulled.
* `destroyWithoutInitIsSafe` — `create()` + poison aux + `destroy()`.
  Caller-side destruction without any init attempt must be safe.
* `calloc` failure injection added (mirrors the existing malloc shim) so
  the create-failure case keeps exercising the same code path with
  `calloc`.

Both destroy cases run in a QProcess child (as the existing create/init
failure cases do): uninitialized `free()` crashes the child, and the
parent turns that crash into an ordinary assertion failure.

Test matrix coverage: normal path (`initialisesDelayLinesWhenAllocationSucceeds`,
unchanged), failure/reconfiguration paths (state-alloc failure, aux-alloc
failure, null-arg init failure, destroy-without-init), audio-thread
constraints (no new allocation on the compute path — create/destroy are
construction-time only), platforms (C only, no OS-specific calls).

## Evidence

Logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-265.r2/logs/`
(`build-*.log`, `test-*.log`, `fix.patch`, `gates*.log`).

### Evidence table

| Step | Command (from lane root unless noted) | Exit | Log |
| --- | --- | --- | --- |
| RED build (test added, no fix) | `cmake --build build --target ReverbSCInitTest` | 0 | `build-1.log` |
| RED test | `ctest -R '^ReverbSCInitTest$' --output-on-failure` (cwd `build/tests`) | **8** | `test-1-red.log` |
| GREEN build | `cmake --build build --target ReverbSCInitTest` | 0 | `build-4.log` |
| GREEN test | `ctest -R '^ReverbSCInitTest$' --output-on-failure` (cwd `build/tests`) | 0 | `test-4-green.log` |
| Reverse patch | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-265.r2/logs/fix.patch` | 0 | — |
| Reverse build | `cmake --build build --target ReverbSCInitTest` | 0 | `build-5.log` |
| Reverse test (must FAIL) | `ctest -R '^ReverbSCInitTest$' --output-on-failure` (cwd `build/tests`) | **8** | `test-5-red.log` |
| Re-apply | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-265.r2/logs/fix.patch` | 0 | — |
| Re-apply build | `cmake --build build --target ReverbSCInitTest` | 0 | `build-6.log` |
| Re-apply test (must PASS) | `ctest -R '^ReverbSCInitTest$' --output-on-failure` (cwd `build/tests`) | 0 | `test-6-green.log` |
| Family build | `cmake --build build --target reverbsc zene ReverbSCCreateTest ReverbSCBaseTest ReverbScAllocationTest ReverbSCPrecisionTest ReverbSCPluginStateTest ReverbSCRawProcessingTest` | 0 | `build-7.log` / `build-8.log` |
| Family test | `ctest -R Reverb --output-on-failure` (cwd `build/tests`) | 0 | `test-8-family.log` |
| Gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 | `gates.log` / `gates2.log` |

RED assertions (`test-1-red.log`, identical after reverse-apply in
`test-5-red.log`):

```
FAIL!  : ReverbSCInitTest::destroyAfterFailedInitIsSafe() '!')
    'child.exitStatus() == QProcess::NormalExit' returned FALSE. ()
    Loc: [../../tests/src/plugins/ReverbSCInitTest.cpp(220)]
FAIL!  : ReverbSCInitTest::destroyWithoutInitIsSafe() 'child.exitStatus() == QProcess::NormalExit' returned FALSE. ()
    Loc: [../../tests/src/plugins/ReverbSCInitTest.cpp(220)]
Totals: 5 passed, 2 failed, 0 skipped, 0 blacklisted, 159ms
```

GREEN (`test-6-green.log`): `100% tests passed out of 1` (7/7 individual
cases PASS).

Affected family (`test-8-family.log`): **7/7 executed, all Passed** —
`ReverbSCCreateTest`, `ReverbSCBaseTest`, `ReverbScAllocationTest`,
`ReverbSCInitTest`, `ReverbSCPrecisionTest`, `ReverbSCPluginStateTest`,
`ReverbSCRawProcessingTest`. (First family run was 6/7 `Not Run` because
those targets were not yet built in this lane build tree — `build-7.log`
shows the miss; after building them in `build-8.log` the family is 7/7.)

Gates (`gates.log`, `gates2.log`): `RESULT: PASS` — fork-sources,
all-sources-reproduce, no-upstream-regression, check-namespace,
no-tautology, doc-figures, complexity (no new item in changed files),
file-length (no new item in changed files); exit 0 both runs.

10 tests executed in the family (7 ctest entries, `ReverbSCInitTest`
has 5 pre-existing cases + 2 new ones = 7 QtTest cases), zero failures.

## Files committed

* `plugins/ReverbSC/revsc.c` (inherited; reason in `tests/upstream-modifications.txt`)
* `plugins/ReverbSC/base.c` (inherited; reason in `tests/upstream-modifications.txt`)
* `tests/src/plugins/ReverbSCInitTest.cpp` (fork-NEW, already registered in
  `tests/fork-sources.txt` and `tests/all-sources.txt`; only extended)
* `tests/CMakeLists.txt` (existing `ReverbSCInitTest` target, one added
  compile definition)
* `tests/upstream-modifications.txt` (two new inherited-file reason lines)
* `docs/qwen-fixes-2026-10/cluster-new-p14-265.md` (this report)

## Self-check

### Verdict evidence

REAL (not already fixed, not false positive): `revsc.c:56` used
`malloc()`, leaving `aux.ptr`/`aux.size` uninitialized; `revsc.c:65-67`
returned before any aux allocation; `revsc.c:102` freed `pp->aux` on every
destroy. The new child cases crash against the unfixed code (RED above)
and pass fixed — direct causal proof.

### Answers to the submit-check "?" items

* **Regression fails-without / passes-with the fix (exit codes).** Yes.
  RED build exit 0, RED test `ctest -R '^ReverbSCInitTest$'` exit **8**
  (`test-1-red.log`; identical after `git apply -R` → exit **8**,
  `test-5-red.log`). GREEN exit 0 (`test-4-green.log`); re-apply
  (`git apply` exit 0) → GREEN exit 0 (`test-6-green.log`). The product
  patch reversed was `revsc.c` + `base.c` only (saved as
  `.../logs/fix.patch`).
* **Deterministic, no timing races.** Yes. The seam is the existing
  malloc/calloc rename (`-Dmalloc=sp_revsc_test_malloc`,
  `-Dcalloc=sp_revsc_test_calloc`): `g_failNextAllocation` makes one
  allocation fail at a chosen point, and the poison fill
  (`memset(&revsc->aux.ptr, 0xA5, ...)`) makes the uninitialized value
  deterministic. No sleeps, no wall-clock, no threads; failure cases run
  in a `QProcess` child so a crash is a normal-exit assertion, not a race.
* **Fix at the cause, complete across callers/siblings.** Yes.
  `create()` zeroes the state (the uninitialized source), `destroy()`
  frees aux only when `initDone` is set (covers both the never-inited
  and the aux-alloc-failed cases) and nulls the caller's handle, and
  `sp_auxdata_free()` null-checks and nulls (the only other auxdata
  caller, `ReverbSCBaseTest`, still passes). The `ReverbSC.cpp`
  create/init-fail → `destroyState()` path now destroys a zeroed state
  with no aux free. Re-init/sample-rate path resets `initDone` before
  re-allocating, unchanged.
* **No collateral damage.** Yes. No saved/loaded data is involved
  (construction-time state only); `calloc` vs `malloc` is byte-identical
  for the state; the `initDone` guard suppresses only a free of memory
  that was never allocated; the family run is 7/7 green (`test-8-family.log`).
  `feature_check.py` gates PASS (exit 0).

### Commands run (with exit codes)

All exact commands are in the Evidence table. Notes:
* `ctest` must run from `build/tests` — two invocations from the lane
  root were rejected by bunny-guard (rc 97) before the echo state was
  used; the logged runs above are the from-`build/tests` ones (`test-4-*.log`,
  `test-6-*.log`, `test-8-family.log`).
* First RED run: 5 passed / 2 failed (not 0 added cases failing before the
  test existed — the two destroy cases failed; the three pre-existing
  cases passed, proving the prior round's fixes for create/init alloc
  failure were intact).
* First GREEN run: 7/7 PASS, ctest exit 0.
* Reverse-patch proof: apply `-R` → 5 passed / 2 failed, ctest exit **8**;
  re-apply → 7/7 PASS, exit 0. The new test catches the bug.
* `git status` before the final build: only the 4 changed files; after
  re-apply, `git diff --stat` shows the 4 fix/test files (+1 upstream.txt
  entry added after, commit list below).

### Answers to check "?" items

* **Collateral damage** — none: old projects/saved data untouched (no
  saved state involved, plugin XML unchanged); `calloc` vs `malloc` is
  byte-identical for the state allocation; the `initDone` guard only
  suppresses the aux free in the uninitialized/failed-init case (nothing
  was allocated to free); `free` on `NULL` / zeroed state unchanged.
  Family 7/7 confirms.
* **Realtime/undo/commands** — create/destroy are construction-time,
  never per-period; no new allocation/free/lock/log on audio paths; undo
  untouched; no new control command added.
* **Registration/gates** — feature_check PASS (exit 0). No new file;
  no header changed, so no dependent rebuild beyond `reverb` plugin +
  the test target. `tests/fork-sources.txt` / `tests/all-sources.txt`
  unchanged (test file already registered).
* **Completeness** — siblings checked: `sp_auxdata_free` also used only
  by this destroy path (plus `ReverbSCBaseTest`'s own aux, which passes);
  double-free of the same aux guarded in `base.c`; double destroy via the
  same handle guarded by `*p = NULL`. Re-sample-rate path calls
  create/init/destroy in the same order.
