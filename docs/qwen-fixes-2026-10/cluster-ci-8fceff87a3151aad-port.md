ALREADY-FIXED

ci-8fceff87a3151aad-port: ALREADY-FIXED

Cluster `ci-8fceff87a3151aad-port` ("Port: CI regression: build / windows-arm64") asks for the
windows-arm64 build regression to be ported to this branch (`bugs/qwen-ci-8fceff87a3151aad-port-port`,
HEAD `e4194e0bf5` when the session began). The finding has no log or line numbers. So I checked the original
failure and every later windows-arm64 build failure in the hosted train logs on this machine. Each one is
already fixed on this branch. Nothing needed porting, and no product or test file changed in this session.

## Evidence

### 1. The original failure (cluster ci-8fceff87a3151aad) is on this branch

- The original failure was a Qt 5.15 build break: `src/gui/FollowActionEditor.cpp:207: error: no matching member function
  for call to 'connect'` (bare `&QSpinBox::valueChanged`). See `docs/qwen-fixes-2026-10/cluster-ci-8fceff87a3151aad.md`.
- Fix commit `ab20525e38` is an ancestor of HEAD: `git merge-base --is-ancestor ab20525e38 HEAD` → `ANC=0`.
- The code on this branch, `src/gui/FollowActionEditor.cpp:210`:
  `QObject::connect(jump, QOverload<int>::of(&QSpinBox::valueChanged), jump, ...)`.
- The guard `tests/qt5-overloaded-signal-check.py` (ctest `Qt5OverloadedSignalCheck`) exists and passes on HEAD:
  - `python3 tests/qt5-overloaded-signal-check.py .` → EXIT=0, with
    "PASS: no connect names a Qt 5 overloaded signal without an explicit overload" (`logs/01-scan-head.log`).
  - `ctest -R 'Qt5OverloadedSignalCheck|MocQt5TrailingReturnCheck'` → EXIT=0, "100% tests passed out of 2"
    (`logs/05-ctest-qt5checks.log`).
- A Qt 5.15 syntax compile of the file is clean:
  `g++ -std=c++20 -fPIC -fsyntax-only $(pkg-config --cflags Qt5Widgets Qt5Xml) -Iinclude -Isrc -Ibuild/include -Ibuild/src src/gui/FollowActionEditor.cpp`
  → EXIT=0 (`logs/02-qt5-syntax-FollowActionEditor.log`).

### 2. Later windows-arm64 failures in the train logs are also fixed on this branch

These are the three newest train runs in `~/.local/share/zene-bunny-fix/qwen/train-ci/`. All three SHAs are
ancestors of HEAD. Each windows-arm64 job failed in its Build step:

| Train SHA (run, job log) | windows-arm64 first error | Fixed on HEAD by | Code on HEAD |
| --- | --- | --- | --- |
| `fa4a5f2e78` (`run-43786984969-job-133036727687.log`) | `tests/src/gui/MixerSendsOnFadersTest.cpp:278:21: error: no matching constructor for initialization of 'QStringList'` (Qt 5.15 has no `QStringList(count, value)`) | `421f6f13ae` (cluster ci-ec26fd9bddef0dd4): ancestor of HEAD (`anc=0`), not in `fa4a5f2e78` (`in-fa4a=1`) | `tests/src/gui/MixerSendsOnFadersTest.cpp:277-281` fills `expected` in a loop; guarded by `tests/train-51-portability-check.py` |
| `31f559ab47` (`run-43764792400-job-132967358917.log`) | `tests/src/plugins/SfzDistoBuffersTest.cpp:60:6: error: use of undeclared identifier 'posix_memalign'` | `6817e1b7dc` / `d9a26d06ad` / `f3b5426267`, all ancestors of HEAD (`anc=0`) | `tests/src/plugins/SfzDistoBuffersTest.cpp:56-61` uses `#if defined(_WIN32)` → `_aligned_malloc`; guarded by `tests/train-51-portability-check.py:130-131` |
| `cc29a63dde` (`run-43710174450-job-132799809296.log`) | `absl/random/internal/iostream_state_saver.h:23:10: fatal error: 'absl/meta/type_traits.h' file not found` (SfzPlayerTest) | (in the train range, ancestor of HEAD) | `tests/CMakeLists.txt:2937-2941` adds `plugins/SfzPlayer/sfizz/external/abseil-cpp` to SfzPlayerTest's include path |

The MixerSendsOnFadersTest fix compiles on Qt 5.15:
`g++ -std=c++20 -fPIC -fsyntax-only $(pkg-config --cflags Qt5Widgets Qt5Xml Qt5Test) -Iinclude -Isrc -Itests -Itests/src -Ibuild -Ibuild/include -Ibuild/src tests/src/gui/MixerSendsOnFadersTest.cpp`
→ EXIT=1 (`logs/06-qt5-syntax-MixerSendsOnFadersTest.log`). The only diagnostic is
`:300:10: fatal error: MixerSendsOnFadersTest.moc: No such file or directory`, because that test's autogen
output has not been built here. Line 300 comes after the class closes (`:296`) and after `QTEST_MAIN` (`:298`).
So the compiler had already parsed line 278's function body under Qt 5.15 headers and found no error.
The first attempt (`logs/03-...`, EXIT=1) failed only on the missing `-Ibuild` (`lmmsconfig.h`) and is
superseded by `06`.

`FollowActionEditor` appears in none of the three logs' error lines. It shows up only in successful compile progress
lines (Haiku's read of the logs).

## Commands and exit codes

All logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/ci-8fceff87a3151aad-port.r2/logs/`.

| Command | EXIT |
| --- | --- |
| `git merge-base --is-ancestor ab20525e38 HEAD` | 0 |
| `git merge-base --is-ancestor 421f6f13ae HEAD` | 0 |
| `git merge-base --is-ancestor 421f6f13ae fa4a5f2e78...` | 1 |
| `git merge-base --is-ancestor {f3b5426267,d9a26d06ad,6817e1b7dc} HEAD` | 0, 0, 0 |
| `python3 tests/qt5-overloaded-signal-check.py . > logs/01-scan-head.log` | 0 |
| Qt 5 syntax check of `FollowActionEditor.cpp` (`02-qt5-syntax-FollowActionEditor.log`) | 0 |
| Qt 5 syntax check of `MixerSendsOnFadersTest.cpp` (`06-qt5-syntax-MixerSendsOnFadersTest.log`) | 1 (only the ungenerated `.moc` at :300, after the class) |
| `cmake --build . --target zene -j4 > logs/04-build-zene.log` (from build/) | 0 |
| `ctest -R 'Qt5OverloadedSignalCheck\|MocQt5TrailingReturnCheck' --output-on-failure > logs/05-ctest-qt5checks.log` (from build/tests) | 0 (2/2 passed) |
| `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/07-feature-check.log` | 0 (RESULT: PASS) |

## Gates

`feature_check.py` EXIT=0 (`logs/07-feature-check.log`):

```
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

## Regression test

No new test. Nothing changed, so there is no RED/GREEN pair to show. Existing guards already cover the
regressions: `Qt5OverloadedSignalCheck` (FollowActionEditor) and `tests/train-51-portability-check.py`
(MixerSendsOnFadersTest's QStringList, SfzDistoBuffersTest's aligned allocation).

Delegation: Haiku #1 read the windows-arm64 job logs in the three newest train-CI folders and reported the
first error of each. Haiku #2 ran the Qt 5 syntax checks, the `zene` build and the ctest Qt5 checks. I ran
the ancestry checks, the corrected Qt 5 syntax check (06), the gates and the commit myself. Sonnet was not
used because there was no fix or test to write.

## Other findings

None.
