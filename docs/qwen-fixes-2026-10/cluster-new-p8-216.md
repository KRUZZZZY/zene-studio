# Cluster new-p8-216 — FIXED

## Finding and evidence

`namedDawCommand` in `src/core/ScriptDawEdit.cpp:83` now measures the UTF-8 byte length before populating the fixed command payload. Values requiring 256 bytes or more are rejected with a script-console error and never queued. The call sites at `src/core/ScriptDawEdit.cpp:337` and `src/core/ScriptDawEdit.cpp:349` leave the channel or effect chain unchanged on refusal. This covers both channel names and effect device references.

The real Lua binding regressions are in `tests/src/core/ScriptDawBindingTest.cpp:157` and `tests/src/core/ScriptDawBindingTest.cpp:182`. The first sends 128 two-byte characters (256 UTF-8 bytes) and checks that the channel name remains unchanged; the second sends a 256-byte device reference and checks that the chain remains unchanged and the refusal is reported.

## Verification evidence

The initial original-code regression was RED. The inverse-patch run on the final two regressions was also RED: both new slots failed on the old behavior. The restored fix was GREEN.

| Evidence | Exact command | Exit |
|---|---|---:|
| Initial RED build | `cmake --build build --target zene ScriptDawBindingTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-216.r1/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Initial RED test | `ctest -R '^ScriptDawBindingTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-216.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc` | 8 |
| First GREEN build | `cmake --build build --target zene ScriptDawBindingTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-216.r1/logs/build-2.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| First GREEN test | `ctest -R '^ScriptDawBindingTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-216.r1/logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Expanded GREEN build (both regressions) | `cmake --build build --target zene ScriptDawBindingTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-216.r1/logs/build-3.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Expanded GREEN test (both regressions) | `ctest -R '^ScriptDawBindingTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-216.r1/logs/test-3.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Save and reverse production patch | `git diff -- src/core/ScriptDawEdit.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-216.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-216.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` | 0 |
| Inverse-patch RED build | `cmake --build build --target zene ScriptDawBindingTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-216.r1/logs/build-4.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Inverse-patch RED test | `ctest -R '^ScriptDawBindingTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-216.r1/logs/test-4.log 2>&1; rc=$?; echo EXIT=$rc` | 8 |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-216.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` | 0 |
| Final GREEN build | `cmake --build build --target zene ScriptDawBindingTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-216.r1/logs/build-5.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Final GREEN test | `ctest -R '^ScriptDawBindingTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-216.r1/logs/test-5.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-216.r1/logs/gate-1.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-216.r1/logs/gate-2.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |

The affected CTest family count was 1 executed target (`ScriptDawBindingTest`), with 1/1 passing in the final run. The inverse-patch log records both regression slots failing. The existing fork-source manifest already lists both changed source and test files.

The requested `fff` grep tool was not exposed in the available tool list or command path. I searched the build, test, and gate logs with `rg` for `error`, `FAILED`, and `Assertion` instead.

## Files committed

- `src/core/ScriptDawEdit.cpp`
- `tests/src/core/ScriptDawBindingTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-216.md`
