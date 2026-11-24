ALREADY-FIXED

ci-8fceff87a3151aad-port: ALREADY-FIXED

Cluster `ci-8fceff87a3151aad-port` ("Port: CI regression: build / windows-arm64") asks for the windows-arm64
build regression to be ported to `bugs/qwen-ci-8fceff87a3151aad-port-port`. Every windows-arm64 build failure
on record is already fixed on this branch, so no product code changes. Round 5 adds one new static regression guard,
`tests/windows-arm64-port-check.py` (ctest `WindowsArm64PortCheck`), which fails if any of the four fixes goes missing.
Before this round, nothing guarded the SfzPlayerTest Abseil include path.

All logs for this round are in `/home/kruzzzzy/.config/kads/zene/state/runs/ci-8fceff87a3151aad-port.r5/logs/`
(called `$L` below).

## Evidence: each windows-arm64 failure and the code on HEAD that fixes it

| Failure (hosted windows-arm64 build) | Fix commit (ancestor of HEAD) | Code on HEAD | Guard check |
| --- | --- | --- | --- |
| Qt 5.15: `src/gui/FollowActionEditor.cpp: no matching member function for call to 'connect'` (bare `&QSpinBox::valueChanged`, overloaded in Qt 5) | `ab20525e38` | `src/gui/FollowActionEditor.cpp:210` `QObject::connect(jump, QOverload<int>::of(&QSpinBox::valueChanged), ...)` | `qt5-overload-followaction` |
| train `fa4a5f2e78`: `tests/src/gui/MixerSendsOnFadersTest.cpp:278:21: no matching constructor for initialization of 'QStringList'` | `421f6f13ae` (not in `fa4a5f2e78`) | `tests/src/gui/MixerSendsOnFadersTest.cpp:280-281` fills `expected` in a loop | `qt5-qstringlist-ctor` |
| train `31f559ab47`: `tests/src/plugins/SfzDistoBuffersTest.cpp:60:6: use of undeclared identifier 'posix_memalign'` | `6817e1b7dc` (with `d9a26d06ad`, `f3b5426267`) | `tests/src/plugins/SfzDistoBuffersTest.cpp:56-61` `#if defined(_WIN32)` → `_aligned_malloc` (`:57`), `#else` → `posix_memalign` | `windows-aligned-alloc` |
| train `cc29a63dde`: `absl/random/internal/iostream_state_saver.h:23:10: fatal error: 'absl/meta/type_traits.h' file not found` (SfzPlayerTest) | `16c24ac834` | `tests/CMakeLists.txt:2943-2945` `if(LMMS_TEST_NAME STREQUAL "SfzPlayerTest")` → `target_include_directories(... plugins/SfzPlayer/sfizz/external/abseil-cpp)` | `sfzplayer-abseil-include` |

Ancestry, rerun this round from the worktree:

| Command | EXIT |
| --- | --- |
| `git merge-base --is-ancestor ab20525e38 HEAD` | 0 |
| `git merge-base --is-ancestor 421f6f13ae HEAD` | 0 |
| `git merge-base --is-ancestor 421f6f13ae fa4a5f2e78` | 1 (the fix came after the failing train) |
| `git merge-base --is-ancestor {f3b5426267,d9a26d06ad,6817e1b7dc} HEAD` | 0, 0, 0 |
| `git merge-base --is-ancestor 16c24ac834 HEAD` | 0 |

## Regression guard (new in this round)

`tests/windows-arm64-port-check.py --source-root <dir>` is a stdlib-only static scan. It has no clock, sleep,
subprocess or network use. It runs four checks and exits 1 if any of them fails:
- (a) Every `&QSpinBox::valueChanged` in `FollowActionEditor.cpp` is wrapped in `QOverload<int>::of(...)`.
- (b) `MixerSendsOnFadersTest.cpp` has no two-argument `QStringList(count, value)` constructor. Comments are ignored.
- (c) `SfzDistoBuffersTest.cpp` uses `_aligned_malloc`, and every `posix_memalign` sits in the `#else` branch of an
  `_WIN32` conditional.
- (d) `tests/CMakeLists.txt`'s `SfzPlayerTest` block adds the `abseil-cpp` include directory.

Registration:
- **CMake:** `tests/CMakeLists.txt:6566-6572`, Linux + `PYTHON3_EXECUTABLE`, next to `Train51Portability`.
- **`tests/fork-sources.txt`:** listed with a comment.
- **`tests/all-sources.txt`:** deliberately not listed. Its header says it is the byte-exact generated list of
  first-party C/C++ sources, so a `.py` entry would break the `all-sources-reproduce` gate (which passes, see Gates).

### RED: pre-fix sources

The red root is a scratch copy of the four files at their pre-fix revisions:
- `git show ab20525e38^:src/gui/FollowActionEditor.cpp`
- `git show 421f6f13ae^:tests/src/gui/MixerSendsOnFadersTest.cpp`
- `git show 6817e1b7dc^:tests/src/plugins/SfzDistoBuffersTest.cpp` (the last version without `_aligned_malloc`;
  `f3b5426267^` already has it)
- `git show 16c24ac834^:tests/CMakeLists.txt`

| Command | EXIT | Result |
| --- | --- | --- |
| `python3 tests/windows-arm64-port-check.py --source-root $L/red-root > $L/14-red-rerun.log 2>&1` | **RED_EXIT=1** | all four checks FAIL |
| same, all four pre-fix files (Sonnet's run) → `$L/01-red.log` | 1 | all four FAIL, e.g. `FAIL qt5-qstringlist-ctor: tests/src/gui/MixerSendsOnFadersTest.cpp:278: QStringList(count, value) constructor does not exist in Qt 5.15` |
| one pre-fix file each, the rest from HEAD → `$L/02-red-a.log` … `$L/05-red-d.log` | 1, 1, 1, 1 | each root fails only its own check, and the other three PASS |

The scratch roots were deleted after the runs.

### GREEN: HEAD, via ctest

```
cd build/tests && ctest -R '^WindowsArm64PortCheck$' --output-on-failure --no-tests=error --output-junit $L/test-green.xml > $L/12-test-green.log 2>&1
```
**GREEN_EXIT=0**: "1/1 Test #834: WindowsArm64PortCheck ... Passed", "100% tests passed out of 1".
Artifacts: `$L/12-test-green.log`, `$L/test-green.xml`.

| Command | EXIT |
| --- | --- |
| `cmake build` (direct reconfigure) → `$L/06-cmake-reconfigure.log` | 97: refused by the lane guard ("configuring is the parent's job ... Use 'cmake --build build --target <t>'") |
| `ctest -R '^WindowsArm64PortCheck$' ...` before regeneration → `$L/07-test-green.log` | 8 ("No tests were found", tree not yet regenerated) |
| `cmake --build build -j4 --target zene > $L/10-build-zene.log 2>&1` (regenerates the tree: "Configuring done", "Generating done") | 0 |
| `ctest -N -R '^WindowsArm64PortCheck$' > $L/11-ctest-list.log 2>&1` | 0 ("Total Tests: 1") |
| `ctest -R '^WindowsArm64PortCheck$' --output-on-failure --no-tests=error --output-junit $L/test-green.xml > $L/12-test-green.log 2>&1` | **0** |
| `ctest -R '^(Train51Portability\|Qt5OverloadedSignalCheck\|WindowsArm64PortCheck)$' --output-on-failure --no-tests=error > $L/13-test-family.log 2>&1` | 0 ("100% tests passed out of 3") |
| `python3 tests/windows-arm64-port-check.py --source-root . > $L/15-green-direct.log 2>&1` | 0 (four PASS lines) |

## Gates

`python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > $L/09-feature-check.log 2>&1`
gave EXIT=0, with the new file and registrations in the tree:

```
  PASS  fork-sources
  PASS  all-sources-reproduce
  PASS  no-upstream-regression
  PASS  check-namespace
  PASS  no-tautology
  PASS  doc-figures
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

Proof manifest: `$L/proof.json`. It holds one `regression-test` artifact for `tests/windows-arm64-port-check.py` /
`WindowsArm64PortCheck`, with the RED and GREEN commands above, `red_exit` 1 and `green_exit` 0, and schema
fields only (no `log` or `junit` keys).

Delegation: Sonnet wrote `tests/windows-arm64-port-check.py`, registered it in `tests/CMakeLists.txt` and
`tests/fork-sources.txt`, and ran the RED roots (01-05) and the gates (09). Its direct `cmake` reconfigure was
refused by the lane guard (06). Haiku regenerated the tree through `cmake --build` and ran the GREEN and family ctest
runs (10-13). I reviewed the script and diff, reran the combined RED root (14) and the direct green run (15), checked
the ancestry and cited lines, and wrote the report, manifest and commit. DeepSeek was not used.

## Other findings

None.

## Self-check

- **RED/GREEN:** no product change was made (ALREADY-FIXED). The new guard is the regression test:
  - On the pre-fix sources it gives RED_EXIT=1 (`$L/14-red-rerun.log`, `$L/01-red.log`), and each single-defect
    root fails only its own check (`$L/02-red-a.log` … `$L/05-red-d.log`).
  - On HEAD it gives GREEN_EXIT=0 through ctest (`$L/12-test-green.log`, `$L/test-green.xml`).
- **Deterministic:** the guard is a pure text scan of four files under `--source-root`. It uses no clock, sleep,
  threads, subprocess or network.
- **Cause and complete:** all four windows-arm64 failures on record are listed above, each with its fix on HEAD
  and its own check in the guard. Check (d) closes the one gap that no existing guard covered. The family ctest
  (`Train51Portability`, `Qt5OverloadedSignalCheck`, `WindowsArm64PortCheck`) passes, 3/3 (`$L/13-test-family.log`).
- **No collateral damage:** the only changes are one new test script, one ctest registration (Linux + Python only)
  and one `fork-sources.txt` entry. No product, save-format or command file was touched, so projects and saved data
  load exactly as before. All gates pass.
- **ALREADY-FIXED evidence:**
  - `src/gui/FollowActionEditor.cpp:210`
  - `tests/src/gui/MixerSendsOnFadersTest.cpp:280-281`
  - `tests/src/plugins/SfzDistoBuffersTest.cpp:56-61`
  - `tests/CMakeLists.txt:2943-2945`

  All four fix commits are ancestors of HEAD (table above).
- **Test file changed:** yes. This round adds `tests/windows-arm64-port-check.py` and edits `tests/CMakeLists.txt`
  and `tests/fork-sources.txt`. The earlier "no test file changed" statement no longer holds.
- **Sibling commands:** the check derives its list of command files from the branch's diff against the train base,
  which contains other lanes' merges. This lane changes no command file, so no `app.*`, `arrangement.*` or other
  command is affected.
