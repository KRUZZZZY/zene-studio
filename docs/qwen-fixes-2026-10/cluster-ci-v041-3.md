ALREADY-FIXED

## Verdict and cause

The defect this cluster was opened for is already fixed on this branch. `tests/src/core/NamModelLoaderTest.cpp`
no longer uses `std::filesystem`, so the macOS deployment target below 10.15 no longer rejects it. No product
or test assertion change is made by this packet; it records the prior fix with gate evidence and adds this
report.

**The original historical error.** At the time of the finding the hosted macos-x86_64 job built below macOS
10.15 (its deployment target is derived from a dependency binary). libc++ marks `std::filesystem` unavailable
there, and the NAM loader regression then failed to compile:

    tests/src/core/NamModelLoaderTest.cpp:137:46: error: 'path' is unavailable: introduced in macOS 10.15
    tests/src/core/NamModelLoaderTest.cpp:141:17: error: 'basic_ofstream' is unavailable: introduced in macOS 10.15
    tests/src/core/NamModelLoaderTest.cpp:250:23: error: 'file_size' is unavailable: introduced in macOS 10.15
    tests/src/core/NamModelLoaderTest.cpp:329:57: error: 'temp_directory_path' is unavailable ...

The current floor is recorded at `CMakeLists.txt:271` as `SET(ZENE_MACOS_MINIMUM "10.13")`, and the current
CMake comment immediately above it (`CMakeLists.txt:264-270`) states that CI today yields 11.0+, which is above
that floor and is supported.

**The ancestor carrying the fix.** `03e5a509b7` is the ancestor that carries the 128-line source portability
change in this file (98 insertions, 30 deletions):

    $ git show --stat --oneline 03e5a509b7 -- tests/src/core/NamModelLoaderTest.cpp
    03e5a509b7 fix(train-sol): Fixed five hosted source portability causes, added compiler and handshake
    regressions, and retained the pending Windows regression. Targeted build, 18 CTest entries, unchanged gate
     tests/src/core/NamModelLoaderTest.cpp | 128 ++++++++++++++++++++++++++--------
     1 file changed, 98 insertions(+), 30 deletions(-)
    EXIT=0        (logs/11-ancestor-diff-stat.log)

That change replaced every `std::filesystem` use with plain `std::string` paths plus the POSIX/Windows pair
`mkdtemp`/`_mkdir` and `stat`/`_stat64`. The file on this branch has no `std::filesystem` left:

    $ rg -n 'filesystem' tests/src/core/NamModelLoaderTest.cpp
    EXIT=1        (logs/01-filesystem-grep.log — no match)

## Code references (ALREADY-FIXED)

- `tests/src/core/NamModelLoaderTest.cpp:33-54` — portable includes: `<fstream>`, `<string>`, `<sys/stat.h>`,
  and `_WIN32`-guarded `<direct.h>`, `<io.h>`, `<process.h>` versus `<unistd.h>`. No `<filesystem>`.
- `tests/src/core/NamModelLoaderTest.cpp:93-152` — `TemporaryFixtureDirectory` holds a `std::string` path; on
  Windows it composes `TEMP` + a pid/attempt suffix and calls `_mkdir` (:105), on POSIX it composes
  `TMPDIR`/`/tmp` + `XXXXXX` and calls `mkdtemp(&m_path[0])` (:120).
- `tests/src/core/NamModelLoaderTest.cpp:204-227` — fixture writing uses `std::ofstream` over a `std::string`
  path (`rejectsModelJson`).
- `tests/src/core/NamModelLoaderTest.cpp:311-334` — `writeFixture` writes with `std::ofstream` over
  `std::string` and measures the result with `_stat64` on Windows or `stat` on POSIX (:323).
- Current line `:120` (`mkdtemp`) is the manifest reference named by the packet; `:323` (`stat`) is the
  matching size check. Both are present as listed in `logs/01-portable-idiom.log` (EXIT=0).
- `tests/CMakeLists.txt:6295` — `add_test(NAME NamModelLoaderPortabilityCheck ...)` inside
  `if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND PYTHON3_EXECUTABLE)`.
- `tests/CMakeLists.txt:6312` — `add_test(NAME NamPortabilityRegistrationCheck ...)`, the registration check
  that fails if a configured tree lacks the guarded CTest. Both are registered in the configured tree
  (`build/tests/CTestTestfile.cmake:1609` and `:1611`, `logs/01-registration.log` EXIT=0).

## Gate evidence (commands as issued in packets p1-p3; exits from logs 01-10)

Logs are under `/home/kruzzzzy/.config/kads/zene/state/runs/ci-v041-3.r1/logs/`. The logs hold each command's
output and exit code, not the command text: the command lines below are copied verbatim from the `commands`
arrays of packets p1, p2 and p3, and the exits are the recorded values, not predictions.

### p1 — ancestor, floor, portable idioms, registration

```
git merge-base --is-ancestor 03e5a509b7 HEAD; echo ANCESTOR_EXIT=$?; git log --oneline -3 -- tests/src/core/NamModelLoaderTest.cpp
rg -n 'CMAKE_OSX_DEPLOYMENT_TARGET|10[.]13|10[.]15' CMakeLists.txt .github/workflows
rg -n 'mkdtemp|_mkdir|std::ofstream|stat\(|sys/stat\.h' tests/src/core/NamModelLoaderTest.cpp
rg -n 'filesystem' tests/src/core/NamModelLoaderTest.cpp plugins/NeuralAmp/nam/NamModel.cpp plugins/NeuralAmp/nam/NamModelLoader.cpp
rg -n 'NamModelLoaderPortabilityCheck|NamPortabilityRegistrationCheck' tests/CMakeLists.txt build/tests/CTestTestfile.cmake
```

- `01-history.log` — EXIT=0 (`ANCESTOR_EXIT=0`)
- `01-floor.log` — EXIT=0 (one match, `CMakeLists.txt:271`)
- `01-portable-idiom.log` — EXIT=0 (`sys/stat.h` :46, `_mkdir` :105, `mkdtemp` :120, `std::ofstream` :208/:244/:286/:314, `stat` :323)
- `01-filesystem-grep.log` — EXIT=1 (no match in the test or either product file)
- `01-registration.log` — EXIT=0

### p2 — build and NAM test family

```
cmake --build /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-ci-v041-3/build --target NamModelLoaderTest NamModelPrewarmTest NamModelMonoOutputTest
cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-ci-v041-3/build/tests && ctest -N -R '^NamModelLoader|^NamPortability|^NamModelMono|^NamModelPrewarm'
cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-ci-v041-3/build/tests && ctest -R '^NamModelMonoOutputTest$|^NamModelPrewarmTest$|^NamModelLoaderTest$|^NamModelLoaderPortabilityCheck$|^NamPortabilityRegistrationCheck$' --output-on-failure
```

- `02-build-nam.log` — EXIT=0
- `02-test-list.log` — EXIT=0 (5 tests: `NamModelMonoOutputTest` #750, `NamModelPrewarmTest` #800, `NamModelLoaderTest` #801, `NamModelLoaderPortabilityCheck` #802, `NamPortabilityRegistrationCheck` #803)
- `03-test-family-green.log` — EXIT=0 (5/5 passed in 0.93 s)

### p3 — portability red/green, restore, recheck

```
cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-ci-v041-3/build/tests && ctest -R '^NamModelLoaderPortabilityCheck$' --output-on-failure
# red mutation: `#include <filesystem>` added at tests/src/core/NamModelLoaderTest.cpp:34 (diff saved as 05-mutation.patch)
rg -n 'filesystem' tests/src/core/NamModelLoaderTest.cpp
cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-ci-v041-3/build/tests && ctest -R '^NamModelLoaderPortabilityCheck$' --output-on-failure
cp /home/kruzzzzy/.config/kads/zene/state/runs/ci-v041-3.r1/logs/05-NamModelLoaderTest.cpp.bak /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-ci-v041-3/tests/src/core/NamModelLoaderTest.cpp
sha256sum -c /home/kruzzzzy/.config/kads/zene/state/runs/ci-v041-3.r1/logs/05-sha-before.txt
git diff --exit-code -- tests/src/core/NamModelLoaderTest.cpp
cmake --build /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-ci-v041-3/build --target NamModelLoaderTest
cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-ci-v041-3/build/tests && ctest -R '^NamModelLoaderPortabilityCheck$' --output-on-failure
cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-ci-v041-3/build/tests && ctest -R '^NamModelMonoOutputTest$|^NamModelPrewarmTest$|^NamModelLoaderTest$|^NamModelLoaderPortabilityCheck$|^NamPortabilityRegistrationCheck$' --output-on-failure
git status --porcelain
```

- `04-test-portability-green.log` — EXIT=0 (1/1 passed in 0.73 s)
- `05-mutation-grep.log` — EXIT=0 (`34:#include <filesystem>`)
- `06-portability-red.log` — **EXIT=8** (`0% tests passed`, 1 failed out of 1)
- `cp` restore command — EXIT=0 from the console; no log
- `07-restore-sha.log` — EXIT=0 (`.../tests/src/core/NamModelLoaderTest.cpp: OK`)
- `07-restore-diff.log` — EXIT=0
- `08-rebuild-restored.log` — EXIT=0
- `09-portability-green-restored.log` — **EXIT=0** (1/1 passed in 0.94 s)
- `10-family-green-restored.log` — EXIT=0 (5/5 passed in 1.06 s)
- `10-status-after-restore.log` — EXIT=0 (empty)

## Self-check

**1. Red/green mutation exits.** The red run (`06-portability-red.log`) applied a one-line
`#include <filesystem>` mutation to the test and ran the portability CTest under the shadow header:
`EXIT=8`, `FAIL: tests/src/core/NamModelLoaderTest.cpp does not compile when <filesystem> is unavailable`,
`0% tests passed, 1 tests failed out of 1`. After restoring the file, the green run
(`09-portability-green-restored.log`) passed the same test: `EXIT=0`, `100% tests passed`. The mutation is
reversible and was reverted (`07-restore-diff.log` EXIT=0, `07-restore-sha.log` EXIT=0, `git status` empty in
`10-status-after-restore.log`).

**2. Determinism.** The check is deterministic: it compiles the file against a generated shadow
`<filesystem>` header that `#error`s under the macOS floor, and it uses the recorded compile command from the
configured tree. There is no timing threshold, no network access and no wall-clock comparison; the same
source produces the same verdict on every run.

**3. Cause and siblings.** The cause was `std::filesystem` use in the test under a deployment target below
10.15, not product logic. The sibling NAM targets — `NamModel.cpp`, `NamModelLoader.cpp`,
`NamModelMonoOutputTest` and `NamModelPrewarmTest` — all build and pass after the fix (log 02 build EXIT=0;
log 03 all 5 tests passed).

**4. No collateral.** This packet changes no product file and no test assertion. The only diff is this docs
file; `tests/src/core/NamModelLoaderTest.cpp` and `tests/CMakeLists.txt` are untouched, and the final
`git status --porcelain` is empty (`logs/18-status-final.log`, EXIT=0).

**5. ALREADY-FIXED code references.** See the list above: `tests/src/core/NamModelLoaderTest.cpp:33-54`,
`:93-152`, `:204-227`, `:311-334`, current `:120`, current `:323`, and `tests/CMakeLists.txt:6295`, `:6312`.
Ancestor `03e5a509b7` carries the 128-line change.

## Other findings

- `schemas/proof.json` is absent on this box: `ls schemas/proof.json` returns "No such file or directory"
  and a bounded `find` returned nothing. `submit_check.py` does not validate the proof schema, so this does
  not block the verdict; the missing schema is environmental.
- `build/tests/NamModelLoaderTest` was missing before packet 2 and had to be built; it exists now and the
  rebuilt binary passed (log 08 EXIT=0, log 10 EXIT=0). Build state is not part of this commit.
- The portability CTest guard is `CMAKE_SYSTEM_NAME STREQUAL "Linux"` (`tests/CMakeLists.txt:6294`), so this
  Linux box exercises the shadow-header check rather than a native macOS build. Hosted macOS confirmation is
  therefore external and is expected to come from the macos-x86_64 job.
- Prior round: `docs/qwen-fixes-2026-10/cluster-ci-9d1768ed0beb1636.md` fixed the same two layers (the
  `std::filesystem` cause and the `PYTHON3_EXECUTABLE` guard ordering). This round preserves that fix
  unchanged and only adds the verdict record.

## Commands for this packet

    $ head -1 docs/qwen-fixes-2026-10/cluster-ci-v041-3.md
    ALREADY-FIXED
    EXIT=0                    (logs/15-report-first-line-final.log)

    $ rg -n '^## Self-check|^## Other findings' docs/qwen-fixes-2026-10/cluster-ci-v041-3.md
    EXIT=0                    (logs/15-report-sections-final.log)

    $ FEATURE_CHECK_LOG_DIR=/home/kruzzzzy/.config/kads/zene/state/runs/ci-v041-3.r1/logs/feature-check-final \
        python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py \
        /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-ci-v041-3
    EXIT=0                    (logs/16-feature-check-final.log)

The commit is docs-only: `docs/qwen-fixes-2026-10/cluster-ci-v041-3.md`
(`logs/17-commit-stat-final.log`, `logs/18-status-final.log`).
