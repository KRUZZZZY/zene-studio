ALREADY-FIXED

# Cluster ci-512849fa221097db-port: "Port: CI regression: build / msvc-x64"

ci-512849fa221097db-port: ALREADY-FIXED

This replaces the round-2 NEEDS-CONTEXT report. The msvc-x64 build regression is MSVC C2492
on a `thread_local` data member of the exported `AudioEngine` class. It is the one fixed for
cluster ci-512849fa221097db by commit `c99455b15c` ("fix(ci): CI regression: build / msvc-x64"),
and that commit is an ancestor of this branch's HEAD (`git merge-base --is-ancestor c99455b15c HEAD`
-> 0). This round changes no product code.

## The CI failure

The hosted msvc-x64 log line, as recorded with the original cluster in
`tests/exported-class-tls-check.py:7-8` and `docs/qwen-fixes-2026-10/cluster-ci-512849fa221097db.md:7-10`:

```
include/AudioEngine.h(891): error C2492: 'lmms::AudioEngine::s_changeInModelDepth':
data with thread storage duration may not have dll interface
```

The original job's log files are not on this machine: `gh` is absent and the run folders hold
only local logs. The CI location cited here is therefore the recorded copy of that error line
(`tests/exported-class-tls-check.py:7`), not a downloaded hosted log.

## Evidence that HEAD is fixed

- `src/core/AudioEngine.cpp:473-477`: the depth is a file-local
  `static thread_local int s_changeInModelDepth = 0;` (line 477), under `#ifdef LMMS_TESTING`,
  with the comment "File-local, not a static member: MSVC rejects thread_local data with a DLL
  interface (C2492) in the exported AudioEngine class."
- `include/AudioEngine.h:681-683`: `isChangeInModelHeld()` is only declared out of line,
  "MSVC rejects a thread_local data member of a dllexport class (C2492), so the depth lives in
  AudioEngine.cpp." `class LMMS_EXPORT AudioEngine` has no thread_local data member: the guard
  below scans the whole header and reports none (log 02).
- `tests/CMakeLists.txt:7086-7095`: ctest `ExportedClassTlsCheck` runs
  `tests/exported-class-tls-check.py` over the tree (`add_test` at 7091).

## Proof that the guard can fail (logs in `runs/ci-512849fa221097db-port.r3/logs/`)

| Step | Command | EXIT |
|---|---|---|
| Scratch copy of `include/` and `src/` in `$TMPDIR/tls-scratch-512849` (outside the worktree). `static thread_local int s_changeInModelDepth;` re-inserted into `class LMMS_EXPORT AudioEngine` after its own `private:` (scratch line 718) | `python3 tests/exported-class-tls-check.py $S > 01-guard-red-scratch.log` | **1**: `include/AudioEngine.h:718: exported class declares a thread_local data member (MSVC C2492)` |
| Real tree | `python3 tests/exported-class-tls-check.py $W > 02-guard-green-real.log` | **0**: `PASS: no thread_local data member in an exported class` |
| ctest | `cd build/tests && ctest -R '^ExportedClassTlsCheck$' --output-on-failure > 03-ctest-guard.log` | **0**: 1/1 passed |

The first scratch insertion landed in the nested `RequestChangesGuard` class and gave EXIT 0.
That result was correct: the nested class is not exported. The member was then placed in
AudioEngine's own private section, and the table rows above are from that run. The scratch
directory was deleted.

## Test change this round: an AudioEngine-shape fixture

The guard's self-test now includes `FIXTURE_AUDIOENGINE` (`tests/exported-class-tls-check.py`). It
reproduces the exact regression shape: a namespaced `class LMMS_EXPORT AudioEngine : public QObject`
with `Q_OBJECT`, and the member after `private:` inside `#ifdef LMMS_TESTING`. The guard must
report exactly one finding, on line 11. The scanner's job does not change, and no existing
fixture or assertion was relaxed.

| Step | Command | EXIT |
|---|---|---|
| RED: copy of the script whose fixture has `static int` instead of `static thread_local int` (in the log folder, deleted after) | `python3 -B $S/check-red.py . > 04-fixture-red.log` | **1**: `SELF-TEST FAIL: AudioEngine-shape fixture flagged [], expected one finding on line 11` |
| GREEN: real script | `python3 -B tests/exported-class-tls-check.py . > 05-fixture-green.log` | **0** |

## Separate failure (not this cluster)

The ClipView failure on train commit `fa4a5f2e78` is a different defect: MSVC friend lookup in the
clip/track headers. It belongs to cluster ci-4ff107c9ee4dc69c, fixed by `f841e48ffe` ("Fix cluster
ci-4ff107c9ee4dc69c: friend the real gui:: view classes so MSVC lookup is unambiguous"). That commit
is also an ancestor of HEAD (`git merge-base --is-ancestor f841e48ffe HEAD` -> 0). See
`docs/qwen-fixes-2026-10/cluster-ci-4ff107c9ee4dc69c.md`.

**Request:** confirm with the next hosted train run that msvc-x64 is green. MSVC can't be
reproduced on this box (`which cl.exe` -> not found; `tools/local-ci.sh:341-346` reports
msvc-x64 NOT-REPRODUCIBLE-HERE), so a green hosted job is the only final confirmation.

## Gates

`python3 .../feature_check.py <worktree> > 06-feature-check.log` -> EXIT=0, `RESULT: PASS`. Every row passed: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length.

## Self-check

- RED/GREEN exit codes: no product change in this round. The verdict is ALREADY-FIXED. The guard's
  red/green comes from the scratch mutation (EXIT 1, log 01) against the real tree (EXIT 0, log 02),
  and the new fixture's from the mutated script (EXIT 1, log 04) against the real one (EXIT 0, log 05).
- Determinism: the test is a pure text scan of headers and inline fixtures, with no timing,
  threads or I/O beyond reading files.
- Fix at the cause and complete: the original fix (`c99455b15c`) moved the only thread_local member
  out of the exported class. The four test callers go through `isChangeInModelHeld()`, according
  to the original report (`cluster-ci-512849fa221097db.md:27-30`). The guard scans every
  exported class in include/ and src/, so a sibling member elsewhere would also fail (log 02
  shows none).
- No collateral damage: this round touches only a test script's self-test fixture and this
  report. No product, save-format or limit changes.
- ALREADY-FIXED code reference: `src/core/AudioEngine.cpp:477`, `include/AudioEngine.h:681-683`,
  `tests/CMakeLists.txt:7091`.
- Sibling commands: no command files were changed in this round. The listed app.* and
  arrangement.* commands come from earlier train commits on the branch and are unaffected.

Delegation: Haiku made the scratch copy, re-inserted the member, and ran the red/green guard
and the ctest (logs 01-03). I wrote the fixture and its mutation proof (logs 04-05) myself, since
each was a few lines. Sonnet and DeepSeek were not needed because no product code changed.

## Other findings

None.
