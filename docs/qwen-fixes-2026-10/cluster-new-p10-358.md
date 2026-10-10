# Cluster new-p10-358

Verdict: **FIXED**

`SaProcessor::m_flipRequest` was read by the analyzer before a waterfall view
could request publication, but had no initialization. It now starts as `false`
in [SaProcessor.h](../../plugins/SpectrumAnalyzer/SaProcessor.h#L145).

The regression in [AudioPluginTest.cpp](../../tests/src/plugins/AudioPluginTest.cpp#L198)
constructs the real processor over storage filled with `0x01` and compares the
flag's object representation with a `false` value. It fails before the fix,
passes with the fix, and fails again when only the production header patch is
reversed.

This checkout does not register a `SaProcessor` ctest target. The files under
`tests/reference/SpectrumAnalyzer` are migration reference sources. The
regression therefore runs in the existing `AudioPluginTest` target, which
already initializes the engine; no CMake reconfiguration was needed.

## Evidence

All build, test, and gate output was redirected to new logs in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-358.r1/logs/`.
Each log records the exit code captured immediately after its command.

| Check | Exact command | Exit |
| --- | --- | ---: |
| Build before initial RED (`build-1.log`) | `cmake --build build --target zene analyzer AudioPluginTest` | 0 |
| Initial RED (`test-1.log`, cwd `build/tests`) | `ctest -R '^AudioPluginTest$' --output-on-failure` | 8 |
| Build with fix (`build-2.log`) | `cmake --build build --target zene analyzer AudioPluginTest` | 0 |
| Initial GREEN (`test-2.log`, cwd `build/tests`) | `ctest -R '^AudioPluginTest$' --output-on-failure` | 0 |
| Reverse only the product patch (`proof-1.log`) | `git diff -- plugins/SpectrumAnalyzer/SaProcessor.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-358.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-358.r1/logs/fix.patch` | 0 |
| Build after reversal (`build-3.log`) | `cmake --build build --target zene analyzer AudioPluginTest` | 0 |
| Revert-proof RED (`test-3.log`, cwd `build/tests`) | `ctest -R '^AudioPluginTest$' --output-on-failure` | 8 |
| Reapply product patch (`proof-2.log`) | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-358.r1/logs/fix.patch` | 0 |
| Build after restoring fix (`build-4.log`) | `cmake --build build --target zene analyzer AudioPluginTest` | 0 |
| Restored GREEN (`test-4.log`, cwd `build/tests`) | `ctest -R '^AudioPluginTest$' --output-on-failure` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check` | 0 |

The RED runs each executed one registered ctest target; QtTest reported 11
passed and the new regression failed. The GREEN runs each passed the one
registered target. Both required gates passed.

## Files committed

- `plugins/SpectrumAnalyzer/SaProcessor.h`
- `tests/src/plugins/AudioPluginTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-358.md`
