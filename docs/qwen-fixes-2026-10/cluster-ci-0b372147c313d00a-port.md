ALREADY-FIXED

The hosted `build / linux-arm64` regression described by cluster
`ci-0b372147c313d00a` is already fixed on this branch, and this branch now owns a
regression test that pins the exact historical defect. No product edit was
needed in the retry: the header correction arrived with ancestor commits
`e9c37052ba` and `03e5a509b7`, and the retry adds the missing regression proof in
`tests/train-39-portability-check.py` (registered unchanged as
`Train39-unused-result`).

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

Under the hosted GCC/fortify configuration a `(void)::write(...)` cast does not
silence `-Wunused-result`. The current branch checks both handshake writes:

- `tests/src/core/MountNamespaceTestSupport.h:62-68` defines
  `writeNamespaceHandshakeByte`, which retries `EINTR` and returns whether the
  byte was written.
- `tests/src/core/MountNamespaceTestSupport.h:88-89` stores its result and
  returns `written && notified` from `mapOwnUidIntoNamespace`.
- `tests/src/core/MountNamespaceTestSupport.h:120` and `:127` store the result in
  `enterOwnMountNamespace` and return `unshared && notified && ...`.

`git merge-base --is-ancestor 03e5a509b7 HEAD` exited 0, so the header correction
is in this branch's ancestry, not only a working-tree artefact. The file is
unchanged by this retry (`git status` shows only the test script).

## Regression test (the port branch's own change)

`tests/train-39-portability-check.py` mode `unused-result`, still registered as
`Train39-unused-result` (`tests/CMakeLists.txt:9851-9863`), now compiles with the
hosted flags plus `-Werror=unused-variable` (`:175-176`) and adds two fixtures:

- `historic-unchecked-handshakes.cpp` (`:211-233`) reproduces both historical
  `(void)::write` handshakes and must be rejected. If it ever compiles, the check
  fails, so the exact regression cannot stop being pinned.
- `dropped-handshake-result.cpp` (`:249-271`) stores
  `writeNamespaceHandshakeByte`'s result in `notified` but returns `true`; under
  `-Werror=unused-variable` it must be rejected, so a caller that stops
  propagating the notification cannot pass.
- `propagated-handshake-result.cpp` (`:273-276`) returns `notified` and must be
  accepted, so the flag rejects dropping the result, not the helper.
- The existing discarded-write control must still be rejected and the existing
  checked-write control accepted, and the real header must still compile
  (`:280-296`).

Test matrix: normal path = the real header and both checked fixtures compile;
failure path = discarded and unpropagated results are rejected; reconfiguration =
each helper combines its notification into the caller's return value;
audio-thread = not applicable (test-support header used before the test starts
threads); supported platforms = Linux block checked here, non-Linux stub
unchanged.

## Commands and EXIT codes (this retry run)

Log folder:
`/home/kruzzzzy/.config/kads/zene/state/runs/ci-0b372147c313d00a-port.r3/logs`

GREEN baseline with the header fix in place:

```sh
cd build/tests && ctest -R '^Train39-unused-result$' --output-on-failure > LOGS/test-1-baseline.log 2>&1
# EXIT=0
```

The historical header fix patch was reconstructed from the last pre-fix header
blob and reverse-applied for RED:

```sh
git diff 2dd87473a6 HEAD -- tests/src/core/MountNamespaceTestSupport.h > LOGS/header-fix.patch
# EXIT=0; 57 lines
git apply --check -R LOGS/header-fix.patch
# EXIT=0
git apply -R LOGS/header-fix.patch
# EXIT=0; header back to the two (void)::write handshakes

cd build/tests && ctest -V -R '^Train39-unused-result$' > LOGS/test-2-red-reverted-header.log 2>&1
# EXIT=8: all five controls PASS; the real header FAILS at :78 and :110 with
# "ignoring return value of 'ssize_t write(...)' [-Werror=unused-result]"
```

`RecordToolsTest` was rebuilt both with the reverted header and after restoring
it:

```sh
cmake --build build --target RecordToolsTest -j4 > LOGS/build-1-recordtools-reverted.log 2>&1
# EXIT=0

git apply LOGS/header-fix.patch
# EXIT=0

cmake --build build --target RecordToolsTest -j4 > LOGS/build-2-recordtools-restored.log 2>&1
# EXIT=0
```

GREEN with the fix restored:

```sh
cd build/tests && ctest -V -R '^Train39-unused-result$' > LOGS/test-3-green-restored-header.log 2>&1
# EXIT=0: PASS control, PASS historical handshakes, PASS checked write,
# PASS dropped handshake notification, PASS propagated handshake,
# PASS mount namespace helper checks every write result
```

Direct consumer and family:

```sh
cmake --build build --target MountNamespaceHandshakeTest -j4 > LOGS/build-3-handshaketest.log 2>&1
# EXIT=0

cd build/tests && ctest -R '^(Train39-unused-result|MountNamespaceHandshakeTest|RecordToolsTest)$' --output-on-failure > LOGS/test-4-family.log 2>&1
# EXIT=8: Train39-unused-result Passed; MountNamespaceHandshakeTest Passed;
# RecordToolsTest failed only its pre-existing environment check
# "cannot measure the volume holding '/home/kruzzzzy/Documents/Zene Studio'".
```

Gates:

```sh
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > LOGS/gates-1.log 2>&1
# EXIT=0; RESULT: PASS (all eight gates)
```

## Self-check

- **Regression fails reverted and passes restored.** With the historical header
  reverse-applied, `Train39-unused-result` EXIT=8, failing on the ignored
  `write()` results at `MountNamespaceTestSupport.h:78` and `:110`
  (`test-2-red-reverted-header.log`). After `git apply` restored the fixed
  header (EXIT=0), the same registered test EXIT=0
  (`test-3-green-restored-header.log`).
- **Deterministic.** The check is compile-only with fixed flags and generated
  fixtures; it uses no clock, sleep, thread or race. Its controls are
  must-fail/must-pass pairs, so it cannot silently pass by rejecting everything.
- **Cause and completeness.** Both handshake writes go through
  `writeNamespaceHandshakeByte` (`:62-68`); both callers propagate its result
  (`:88-89`, `:120`, `:127`). The extended check now pins both the historical
  `(void)::write` shape and the dropped-notification shape. The only consumer,
  `RecordToolsTest`, rebuilt EXIT=0.
- **No collateral damage.** The header is unchanged by this retry; the non-Linux
  stub is untouched. No product, save/load, undo, command or audio-thread path is
  changed, and no limit or validation was added.
- **ALREADY-FIXED evidence.** Current code is
  `tests/src/core/MountNamespaceTestSupport.h:62-68`, `:88-89`, `:120`, `:127`;
  `git merge-base --is-ancestor 03e5a509b7 HEAD` exited 0. Reverse-applying the
  historical header faithfully reproduces the hosted error at the historical
  lines 78 and 110, and the registered check rejects that state (EXIT=8).
- **Sibling commands.** This cluster changes no command file, registry or
  reversibility table.

## Other findings

- `RecordToolsTest::theRemainingRecordTime()` fails on this box with
  `cannot measure the volume holding '/home/kruzzzzy/Documents/Zene Studio'`
  (`test-4-family.log`). That is an unrelated environment/volume check in an
  inherited test, not the header write-result defect; it is not fixed here.
- Fontconfig prints `No writable cache directories` while those tests load
  plug-ins; harmless noise, unrelated to this finding.
