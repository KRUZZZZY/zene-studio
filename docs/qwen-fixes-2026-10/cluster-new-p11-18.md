# Cluster new-p11-18

**Verdict: FIXED.** `plainText` removed HTML tags without a separator, merging text from adjacent elements. It now replaces each tag with a space before simplifying the text. The finding is reproduced through `app.ask` using a manual fixture containing `<p>drum</p><p>loop</p>`.

Evidence:

- [ControlCommandsAssistant.cpp:93](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p11-18/src/core/ControlCommandsAssistant.cpp:93) inserts a space for every matched tag.
- [CustomAccessTest.cpp:210](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p11-18/tests/src/core/CustomAccessTest.cpp:210) checks that `app.ask` finds the fixture section and returns `drum loop`.
- The test failed on the original code with `CustomAccessTest::manualMarkupPreservesWordBoundaries(): 'found' returned FALSE` and passed with the fix.

## Evidence table

| Check | Exact command | Result |
| --- | --- | --- |
| RED | `ctest -R '^CustomAccessTest$' --output-on-failure` (cwd: `build/tests`; run against original code and again with the fix patch reversed) | EXIT=8 both times |
| GREEN | `ctest -R '^CustomAccessTest$' --output-on-failure` (cwd: `build/tests`; after restoring the fix) | EXIT=0 |
| Affected family | `ctest -V -R '^CustomAccessTest$' --output-on-failure` (cwd: `build/tests`) | EXIT=0; 1/1 CTest target, 18 Qt cases passed |
| Complexity gate | `bash tests/complexity-gate.sh --check` | EXIT=0 |
| File length gate | `bash tests/file-length-gate.sh --check` | EXIT=0 |

## Commands and logs

All build, test, and gate output was redirected to new files in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-18.r1/logs`. The logged command wrappers recorded each exit code.

- `cmake --build build --target CustomAccessTest` — EXIT=0, `build-1-red.log`.
- `ctest -R '^CustomAccessTest$' --output-on-failure` — EXIT=8, `test-1-red.log`.
- `git diff -- src/core/ControlCommandsAssistant.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-18.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-18.r1/logs/fix.patch` — EXIT=0.
- `cmake --build build --target zene CustomAccessTest` — EXIT=0, `build-4-proof-red.log`.
- `ctest -R '^CustomAccessTest$' --output-on-failure` — EXIT=8 with the fix reversed, `test-4-proof-red.log`.
- `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-18.r1/logs/fix.patch` — EXIT=0.
- `cmake --build build --target zene CustomAccessTest` — EXIT=0, `build-5-proof-green.log` and `build-6-final.log`.
- `ctest -R '^CustomAccessTest$' --output-on-failure` — EXIT=0 after restoring the fix, `test-5-proof-green.log` and `test-6-final.log`.
- `ctest -V -R '^CustomAccessTest$' --output-on-failure` — EXIT=0, `test-7-count.log`; 18 Qt cases passed.
- `bash tests/complexity-gate.sh --check` — EXIT=0, `gate-3-complexity-final.log`.
- `bash tests/file-length-gate.sh --check` — EXIT=0, `gate-4-length-final.log`.

The fff grep tool was not available in this session; logs were searched with `rg` for `error`, `FAILED`, and `Assertion`.

## Files committed

- `src/core/ControlCommandsAssistant.cpp`
- `tests/src/core/CustomAccessTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p11-18.md`
