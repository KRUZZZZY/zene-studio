ALREADY-FIXED

The hosted `build / linux-arm64` regression described by cluster
`ci-0b372147c313d00a` is already fixed on this branch. The port does not need a
product edit: the fix (and its regression probe) arrived with ancestor commit
`03e5a509b7` while this branch was brought up to `050/m0-train-52`.

## Finding and decision

The historical failure was:

```
tests/src/core/MountNamespaceTestSupport.h:78:22: error: ignoring return value of
'ssize_t write(int, const void*, size_t)' declared with attribute
'warn_unused_result' [-Werror=unused-result]
tests/src/core/MountNamespaceTestSupport.h:110:22: error: ignoring return value of
'ssize_t write(int, const void*, size_t)' declared with attribute
'warn_unused_result' [-Werror=unused-result]
```

The hosted `linux-arm64` job builds with Qt 5 and GCC's fortified libc, where a
`(void)::write(...)` cast does not silence `-Wunused-result`. The original
port branch fixed this by checking both handshake writes.

I checked the base revision this fix was made against (`40b4322860e9`), and it
still contains the rejected calls:

```
git show 40b4322860e9:tests/src/core/MountNamespaceTestSupport.h
    char done = written ? 1 : 0;
    (void)::write(writeEnd, &done, 1);       # historical line 78
    return written;
...
    char go = 1;
    (void)::write(toChild[1], &go, 1);       # historical line 110
```

The current branch contains the corrected code. `writeNamespaceHandshakeByte`
checks and retries the write (`tests/src/core/MountNamespaceTestSupport.h:62-68`);
both callers use it and include its result in their return value
(`tests/src/core/MountNamespaceTestSupport.h:88-89` and `:120`, `:127`).

`git blame` attributes those lines to `03e5a509b7`, and
`git merge-base --is-ancestor 03e5a509b7 HEAD` exited 0, so the correction is on
this branch rather than being a working-tree artefact. A search of
`tests/src/` found no remaining `(void)`, `(void) `, or `(void)` `::write` call.

## Regression test

The regression belongs to the existing Train 39 portability family:

- `tests/train-39-portability-check.py:158-228` (mode `unused-result`) compiles
  the real header with the hosted flags
  `-std=c++20 -O2 -D_FORTIFY_SOURCE=3 -Werror=unused-result -c`.
- `tests/CMakeLists.txt:9851-9863` registers it as `Train39-unused-result`.
- The probe also compiles a must-fail discarded-write control and a must-pass
  checked-write control, so the test cannot silently stop detecting the defect.
- The header is consumed by `RecordToolsTest` (`tests/src/core/RecordToolsTest.cpp:237`);
  the target rebuilt successfully (see below).

Test matrix: normal path = the real header compiles and the probe's checked
control compiles; failure path = a discarded `write()` result is rejected;
reconfiguration = a failed handshake write returns `false` instead of being
ignored; audio-thread = not applicable (test-support header, used before the
test starts any threads); supported platforms = the Linux block and the
non-Linux stub keep their existing behaviour.

## Commands and EXIT codes

Log folder:
`/home/kruzzzzy/.config/kads/zene/state/runs/ci-0b372147c313d00a-port.r2/logs`

```sh
cmake --build <worktree>/build --target RecordToolsTest -j4 > LOGS/build-1-recordtools.log 2>&1
# EXIT=0

ctest -R 'Train39-unused-result|RecordToolsTest' --output-on-failure > LOGS/test-2-green.log 2>&1
# EXIT=8: Train39-unused-result Passed; RecordToolsTest failed on the unrelated
# missing-volume environment check described under Other findings.
```

RED with the reported defect reintroduced (the two unchecked `(void)::write`
calls), then the restored tree:

```sh
git diff -- tests/src/core/MountNamespaceTestSupport.h > LOGS/reintroduce-bug.patch
# EXIT=0, 1363 bytes

ctest -R '^Train39-unused-result$' --output-on-failure > LOGS/test-3-red-reintroduced.log 2>&1
# EXIT=8: both controls passed; the real header failed at :88 and :120 with
# "ignoring return value of 'ssize_t write(...)' [-Werror=unused-result]"

git apply -R LOGS/reintroduce-bug.patch
# EXIT=0

ctest -R '^Train39-unused-result$' --output-on-failure > LOGS/test-4-green-restored.log 2>&1
# EXIT=0

ctest -R '^(Train39-unused-result|Train41-linux|UnusedResultFortifyCheck)$' --output-on-failure > LOGS/test-5-related-family.log 2>&1
# EXIT=0: 3/3 passed
```

`git status --short` was clean after the reverse patch.

Gates:

```sh
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > LOGS/gates-1.log 2>&1
# EXIT=0; all eight gates PASS
```

## Self-check

- **Regression fails reverted and passes restored.** With the two unchecked
  writes reintroduced, `Train39-unused-result` EXIT=8, failing at
  `MountNamespaceTestSupport.h:88` and `:120` (`test-3-red-reintroduced.log`).
  After `git apply -R` restored the fixed header (EXIT=0), the same test EXIT=0
  (`test-4-green-restored.log`).
- **Deterministic.** The probe is compile-only with fixed flags and fixed
  fixtures; it has a rejected discarded-write control and an accepted
  checked-write control. It uses no clock, sleep, thread or race.
- **Cause and completeness.** Both handshake writes now go through
  `writeNamespaceHandshakeByte` (`:62-68`), and both callers combine its result
  into their return value (`:88-89`, `:120`, `:127`). No `(void)` `::write`
  remains under `tests/src/`, and the only consumer, `RecordToolsTest`, rebuilt
  EXIT=0.
- **No collateral damage.** This is a Linux-only test-support header. The
  non-Linux stub is unchanged, no product/save/load/undo/command path is
  touched, and no limit or validation was added. The original `RecordToolsTest`
  volume check is unaffected by the header change; its local failure is
  environmental (Other findings).
- **ALREADY-FIXED evidence.** The base revision still has the rejected calls at
  historical lines 78 and 110, while the current header checks both writes at
  `tests/src/core/MountNamespaceTestSupport.h:62-68`, `:88-89`, `:120`, `:127`.
  `git blame` and `git merge-base --is-ancestor 03e5a509b7 HEAD` (EXIT=0) place
  the correction on this branch.
- **Sibling commands.** This cluster changes no command file, registry or
  reversibility table. The `app.*` and `arrangement.*` list in the pre-submit
  check comes from other train commits between the old base and `HEAD`; none of
  those commands reads or writes the mount-namespace test helper.

## Other findings

- `RecordToolsTest::theRemainingRecordTime()` fails on this box with
  `cannot measure the volume holding '/home/kruzzzzy/Documents/Zene Studio'`
  (`test-2-green.log`). That is an unrelated environment/mount-visibility
  failure in an inherited test, not the header write-result defect; it is not
  fixed here.
- Fontconfig prints `No writable cache directories` while those tests load
  plug-ins; harmless noise, unrelated to this finding.
