ALREADY-FIXED

# cluster ci-aa184f959f204bcd-port - port of "CI regression: build / linux-x86_64"

ci-aa184f959f204bcd-port: ALREADY-FIXED

## What the finding is

The finding only gives the title. The original cluster's report is on this branch at
`docs/qwen-fixes-2026-10/cluster-ci-aa184f959f204bcd.md:1-30`. It says the hosted `linux-x86_64`
job failed with `-Werror=unused-result` at `tests/src/core/MountNamespaceTestSupport.h:78` and `:110`.
Both lines were `(void)::write(...)` calls. Ubuntu's `_FORTIFY_SOURCE` marks `write` as
`warn_unused_result`, and a `(void)` cast does not silence that. This job ports that fix to the
branch.

## Evidence that this branch already has the fix

- `tests/src/core/MountNamespaceTestSupport.h:63-68`: the new helper `writeNamespaceHandshakeByte`
  uses the `::write` result (`sent == 1`) and retries on EINTR.
- `tests/src/core/MountNamespaceTestSupport.h:84-88`: `mapOwnUidIntoNamespace` checks the uid_map
  write (`::write(...) == len`) and the handshake result (`notified`).
- `tests/src/core/MountNamespaceTestSupport.h:118`: `enterOwnMountNamespace` sends the go byte
  through the checked helper.
- The commits that landed it: `git log -- tests/src/core/MountNamespaceTestSupport.h` shows
  03e5a509b7 ("Fixed five hosted source portability causes, added compiler and handshake
  regressions") and de50c6cc4c.
- The regression test is on the branch and registered: `tests/unused-result-fortify-check.py`,
  ctest `UnusedResultFortifyCheck` at `tests/CMakeLists.txt:9622-9627`.
- A fff search for `(void)::write`, `(void)::read`, `(void) ::write`, `(void)write(` and
  `(void)::system` outside the vendored trees found them only inside test fixture strings
  (`tests/unused-result-fortify-check.py:77` and `tests/train-39-portability-check.py:187`).
  Those strings are the deliberate "must be rejected" samples. No product or test-helper code
  throws away a `__wur` result.

## Commands and exit codes (logs in /home/kruzzzzy/.config/kads/zene/state/runs/ci-aa184f959f204bcd-port.r2/logs)

```
cmake --build build --target RecordToolsTest -j4 > logs/build-1.log 2>&1; echo EXIT=$?          # EXIT=0
cd build/tests && ctest -R '^(UnusedResultFortifyCheck|RecordToolsTest)$' --output-on-failure \
  > logs/test-1.log 2>&1; echo EXIT=$?                                                             # EXIT=8
#   UnusedResultFortifyCheck: Passed
#   RecordToolsTest: Failed, 26 passed / 1 failed. The failure is environmental (see Other findings)
cd build/tests && ctest -R 'Portability|Fortify' --output-on-failure > logs/test-2.log 2>&1; echo EXIT=$?   # EXIT=8
#   UnusedResultFortifyCheck, Train51/Train43 portability, NAM portability checks: Passed
#   Train48Portability, Train47Portability, VideoTestClipPortabilityTest: failed or not run (see Other findings)
python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/feature-check-1.log 2>&1; echo EXIT=$?   # EXIT=0
#   RESULT: PASS (all 8 gates)
```

The product code is unchanged, so this job has no regression test of its own and no red/green
pair. The existing `UnusedResultFortifyCheck` was already proven red without the fix and green
with it in the original cluster report (`cluster-ci-aa184f959f204bcd.md`, ctest EXIT=8 then EXIT=0).
On this branch it passes.

## Self-check

- Verdict backed by evidence: yes. The header lines above were read this session, and
  `UnusedResultFortifyCheck` passed in `logs/test-1.log`.
- Regression test: the existing one covers this, so no new test was needed. No product file
  changed, which means there is no fix patch to revert.
- Gates: feature_check EXIT=0, RESULT: PASS.
- Realtime, undo and control commands: not touched.
- Registration: no new product file was added, and no inherited file was changed.
- Fails without the fix and passes with it: this job did not change the product, so there is
  nothing to revert. The original cluster report recorded the red/green proof for
  `UnusedResultFortifyCheck`: ctest EXIT=8 with the header reverted, EXIT=0 with the fix. On
  this branch it passes (`logs/test-1.log`, "#1015 UnusedResultFortifyCheck: Passed").
- Deterministic: yes. It is one compiler invocation over fixed probe source, with injected flags
  (`-D_FORTIFY_SOURCE=3 -Werror=unused-result`). There is no timing, no sleep and no process
  under test.
- At the cause and complete: all three `::write` calls in the header use their results (lines 66,
  86 and 118 via the helper). The `(void)`-cast scan found no other first-party site.
- Collateral damage: none. This commit only adds this report.
- ALREADY-FIXED code reference: `tests/src/core/MountNamespaceTestSupport.h:63-68`, `:84-88` and
  `:118`, plus `tests/CMakeLists.txt:9622`.
- Sibling commands (app.*, arrangement.*): the check lists them from the train branch's existing
  diff against its base (575 changed files). This job changed no command file, so none of them is
  affected.

## Other findings

These are not fixed here and are not caused by this cluster:

- `RecordToolsTest::theRemainingRecordTime` (`tests/src/core/RecordToolsTestParts1.h:296`) fails
  with "cannot measure the volume holding '/home/kruzzzzy/Documents/Zene Studio'". That directory
  does not exist on this box (`ls` reports "No such file or directory"). The test depends on the
  default project directory being there.
- `Train48Portability` fails with `SfzPlayerTest.moc: No such file or directory`. The
  `SfzPlayerTest` target has not been built in this lane tree, so its moc was never generated.
- `Train47Portability` fails because it tries to configure a CMake tree, and this lane's guard
  refuses that ("configuring is the parent's job").
- `VideoTestClipPortabilityTest` was not run because its executable is not built in this tree.

Delegation: Haiku ran the RecordToolsTest build and both ctest runs and summarised the logs. I
read the header, the registration and the failing assertion myself. No Sonnet or DeepSeek task was
needed, because there was nothing to change.
