# Cluster new-p9-241

Verdict: **FIXED**

## Evidence

- `plugins/Lv2Effect/Lv2FxControlDialog.cpp:51-55` now connects
  `Lv2FxControls::modelChanged` once in the constructor before the initial manual refresh.
- `plugins/Lv2Effect/Lv2FxControlDialog.cpp:69-72` now only refreshes the base view; it no
  longer adds a connection on each signal delivery.
- `tests/src/core/Lv2StateTest.cpp:108-130` adds
  `modelChangedConnectionIsInstalledInConstructor`, which requires the single connection to
  appear in the constructor and not in `modelChanged()`.
- The suggested CTest names are not registered in this configured tree. The regression runs
  under the existing `Lv2StateTest` target (one CTest test, 1/1 executed). Its source-structure
  check is deterministic and does not depend on an LV2 bundle being installed.

## RED/GREEN and reverse-patch proof

All build and CTest output is in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-241.r1/logs`.

| Check | Command | Exit |
|---|---|---:|
| Requested test-name discovery | `ctest -N -R '^(Lv2FxControlDialog|Lv2FxControls)$' > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-241.r1/logs/test-list-1.log 2>&1; echo EXIT=$?` | 0 (0 tests listed) |
| RED build | `cmake --build build --target zene lv2effect Lv2StateTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-241.r1/logs/build-1.log 2>&1; echo EXIT=$?` | 0 |
| RED test, original production code | `ctest -R '^Lv2StateTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-241.r1/logs/test-1.log 2>&1; echo EXIT=$?` | 8; failed at `constructorBody.contains(connection)` |
| GREEN build | `cmake --build build --target zene lv2effect Lv2StateTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-241.r1/logs/build-2.log 2>&1; echo EXIT=$?` | 0 |
| GREEN test | `ctest -R '^Lv2StateTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-241.r1/logs/test-2.log 2>&1; echo EXIT=$?` | 0; 1/1 passed |
| Save and reverse product patch | `git diff -- plugins/Lv2Effect/Lv2FxControlDialog.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-241.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-241.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Reverse build | `cmake --build build --target zene lv2effect Lv2StateTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-241.r1/logs/build-3.log 2>&1; echo EXIT=$?` | 0 |
| Reverse test, fix absent | `ctest -R '^Lv2StateTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-241.r1/logs/test-3.log 2>&1; echo EXIT=$?` | 8; same assertion failed |
| Reapply product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-241.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Reapplied build | `cmake --build build --target zene lv2effect Lv2StateTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-241.r1/logs/build-4.log 2>&1; echo EXIT=$?` | 0 |
| Reapplied test | `ctest -R '^Lv2StateTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-241.r1/logs/test-4.log 2>&1; echo EXIT=$?` | 0; 1/1 passed |

## Ratchet gates

| Gate | Command | Exit |
|---|---|---:|
| Complexity | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-241.r1/logs/gate-5.log 2>&1; echo EXIT=$?` | 0; PASS |
| File length | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-241.r1/logs/gate-6.log 2>&1; echo EXIT=$?` | 0; PASS |

## Files committed

- `plugins/Lv2Effect/Lv2FxControlDialog.cpp`
- `tests/src/core/Lv2StateTest.cpp`
- `tests/upstream-modifications.txt`
- `tests/fork-sources.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-241.md`
