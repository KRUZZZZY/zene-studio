# Cluster new-p11-95

Verdict: **FIXED**

`EnvelopeAndLfoParameters::loadSettings()` now resets `m_userWave` to
`SampleBuffer::emptyBuffer()` before reading `userwavefile`. Loading settings
with a missing or empty path therefore clears the previous custom LFO waveform.

## File and regression evidence

- [src/core/EnvelopeAndLfoParameters.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p11-95/src/core/EnvelopeAndLfoParameters.cpp:387): reset before the optional path load.
- [tests/src/core/EnvelopeAndLfoParametersTest.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p11-95/tests/src/core/EnvelopeAndLfoParametersTest.cpp:84): the regression loads a real WAV, then checks both a missing attribute and an empty attribute.
- `tests/CMakeLists.txt:584`, `tests/fork-sources.txt:1947`, and `tests/all-sources.txt:2410`: test registration.
- `tests/upstream-modifications.txt:1054`: inherited production file registration and reason.

Before the fix, both regression slots failed because the loaded buffer was not
the canonical empty buffer. After the fix, both passed. Reversing only the
production patch reproduced both failures; reapplying it made them pass again.
The affected ctest family contains **1 CTest test (1/1) and 2 regression slots**.

CTest commands below ran from
`/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p11-95/build/tests`.
Build and test output is under
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-95.r1/logs`.

## Exact build, test, and proof commands

| Phase | Exact command | Exit |
| --- | --- | ---: |
| Initial target lookup | `cmake --build build --target zene EnvelopeAndLfoParametersTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-95.r1/logs/build-01-red.log 2>&1; status=$?; printf 'EXIT=%d\n' "$status"` | 2 |
| Target list refresh | `cmake --build build --target help > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-95.r1/logs/build-02-targets.log 2>&1; status=$?; printf 'EXIT=%d\n' "$status"` | 0 |
| Baseline app build | `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-95.r1/logs/build-03-zene-red.log 2>&1; status=$?; printf 'EXIT=%d\n' "$status"` | 0 |
| Baseline test build | `cmake --build build --target EnvelopeAndLfoParametersTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-95.r1/logs/build-04-red-test.log 2>&1; status=$?; printf 'EXIT=%d\n' "$status"` | 0 |
| RED | `ctest -R EnvelopeAndLfoParametersTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-95.r1/logs/test-01-red.log 2>&1; status=$?; printf 'EXIT=%d\n' "$status"` | 8 |
| GREEN build | `cmake --build build --target zene EnvelopeAndLfoParametersTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-95.r1/logs/build-05-green.log 2>&1; status=$?; printf 'EXIT=%d\n' "$status"` | 0 |
| GREEN | `ctest -R EnvelopeAndLfoParametersTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-95.r1/logs/test-02-green.log 2>&1; status=$?; printf 'EXIT=%d\n' "$status"` | 0 |
| Save production-only patch | `git diff -- src/core/EnvelopeAndLfoParameters.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-95.r1/logs/fix.patch; status=$?; printf 'EXIT=%d\n' "$status"` | 0 |
| Reverse patch | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-95.r1/logs/fix.patch; status=$?; printf 'EXIT=%d\n' "$status"` | 0 |
| Reverse-patch build | `cmake --build build --target zene EnvelopeAndLfoParametersTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-95.r1/logs/build-06-reverse-red.log 2>&1; status=$?; printf 'EXIT=%d\n' "$status"` | 0 |
| Reverse-patch RED | `ctest -R EnvelopeAndLfoParametersTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-95.r1/logs/test-03-reverse-red.log 2>&1; status=$?; printf 'EXIT=%d\n' "$status"` | 8 |
| Reapply patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-95.r1/logs/fix.patch; status=$?; printf 'EXIT=%d\n' "$status"` | 0 |
| Reapply build | `cmake --build build --target zene EnvelopeAndLfoParametersTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-95.r1/logs/build-07-reapply-green.log 2>&1; status=$?; printf 'EXIT=%d\n' "$status"` | 0 |
| Reapply GREEN | `ctest -R EnvelopeAndLfoParametersTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-95.r1/logs/test-04-reapply-green.log 2>&1; status=$?; printf 'EXIT=%d\n' "$status"` | 0 |

The initial combined target lookup returned 2 because the new target was not
yet listed in the generated makefiles; `--target help` returned 0 and refreshed
the generated target list. No configure command was run.

## Gates

| Gate | Exact command | Exit |
| --- | --- | ---: |
| Fork/all-source registration | `bash tests/fork-sources-gate.sh > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-95.r1/logs/gate-01-manifest.log 2>&1; status=$?; printf 'EXIT=%d\n' "$status"` | 0 |
| Complexity | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-95.r1/logs/gate-02-complexity.log 2>&1; status=$?; printf 'EXIT=%d\n' "$status"` | 0 |
| File length | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-95.r1/logs/gate-03-file-length.log 2>&1; status=$?; printf 'EXIT=%d\n' "$status"` | 0 |
| Inherited-file registration | `bash tests/no-upstream-regression-gate.sh > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-95.r1/logs/gate-04-upstream.log 2>&1; status=$?; printf 'EXIT=%d\n' "$status"` | 0 |

No `fff grep` tool was available in this session, so the specified log-folder
search terms were checked with `rg` against the log folder.

## Files in the commit

- `src/core/EnvelopeAndLfoParameters.cpp`
- `tests/src/core/EnvelopeAndLfoParametersTest.cpp`
- `tests/CMakeLists.txt`
- `tests/fork-sources.txt`
- `tests/all-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p11-95.md`
