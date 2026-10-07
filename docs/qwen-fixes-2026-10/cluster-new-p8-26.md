# Cluster new-p8-26: instrument-load inverse

**Verdict: FIXED**

`plugin.load` replaces the current instrument, but its transaction marked the operation snapshot-only and still advertised a `plugin.load` inverse with no required `device` argument. Replaying that inverse was rejected by the command schema before the handler ran. The transaction now records an empty inverse because it does not preserve the previous instrument's resolvable device id or parameter state.

## Evidence table

| Check | Exact command | Result |
|---|---|---|
| RED before fix | `ctest -R ReversibilityUndoTest --output-on-failure --verbose > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-26.r1/logs/test-3.log 2>&1; echo EXIT=$?` | Exit 8; inverse replay refused for missing `device`. |
| GREEN after fix | `ctest -R ReversibilityUndoTest --output-on-failure --verbose > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-26.r1/logs/test-4.log 2>&1; echo EXIT=$?` | Exit 0; 1/1 CTest passed. |
| Fix-removal proof | `ctest -R ReversibilityUndoTest --output-on-failure --verbose > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-26.r1/logs/test-5.log 2>&1; echo EXIT=$?` | Exit 8; same missing-`device` failure after removing the fix. |
| Reapplied-fix proof | `ctest -R ReversibilityUndoTest --output-on-failure --verbose > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-26.r1/logs/test-6.log 2>&1; echo EXIT=$?` | Exit 0; 1/1 CTest passed. |
| Affected family | `ctest -R ReversibilityUndoTest --output-on-failure --verbose` from `build/tests` | 1/1 CTest executed; final QtTest totals 10 passed, 0 failed, 2 skipped. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-26.r1/logs/gate-1.log 2>&1; echo EXIT=$?` | Exit 0. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-26.r1/logs/gate-2.log 2>&1; echo EXIT=$?` | Exit 0. |
| Files committed | `src/core/ControlCommandsPlugin.cpp`; `tests/src/core/ReversibilityUndoTest.cpp`; `tests/upstream-modifications.txt`; `docs/qwen-fixes-2026-10/cluster-new-p8-26.md` | Fix commit `9ffe284d6`; report table added in the follow-up documentation commit. |

## Evidence

- [ControlCommandsPlugin.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p8-26/src/core/ControlCommandsPlugin.cpp:234): instrument replacement records an empty `inverse`; the transaction remains `reversible: false`.
- [ReversibilityUndoTest.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p8-26/tests/src/core/ReversibilityUndoTest.cpp:299): loads an instrument and replays any advertised inverse through the registry.
- [upstream-modifications.txt](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p8-26/tests/upstream-modifications.txt:683): records the reason for changing the inherited source file.

The affected CTest family contains **1 registered test**, and CTest executed **1/1**. The final QtTest run reported 10 passed, 0 failed, and 2 skipped (the two effect-specific slots had no loadable effect in this build).

## Commands and results

Build commands ran from the worktree root. CTest commands ran from `build/tests`. All output logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-26.r1/logs`.

Initial test-target build and skipped-fixture diagnosis:

```text
cmake --build build --target ReversibilityUndoTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-26.r1/logs/build-1.log 2>&1; echo EXIT=$?  -> EXIT=0
ctest -R ReversibilityUndoTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-26.r1/logs/test-1.log 2>&1; echo EXIT=$?  -> EXIT=0
ctest -R ReversibilityUndoTest --output-on-failure --verbose > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-26.r1/logs/test-2.log 2>&1; echo EXIT=$?  -> EXIT=0
cmake --build build --target zene ReversibilityUndoTest tripleoscillator > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-26.r1/logs/build-2.log 2>&1; echo EXIT=$?  -> EXIT=0
```

The first verbose run showed the new regression skipped because the build had no loadable instrument. After building `tripleoscillator`, the same regression reached the command and failed on the missing `device` argument:

```text
ctest -R ReversibilityUndoTest --output-on-failure --verbose > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-26.r1/logs/test-3.log 2>&1; echo EXIT=$?  -> EXIT=8 (RED: args: missing required property 'device')
```

After the fix, the initial GREEN run was:

```text
cmake --build build --target zene ReversibilityUndoTest tripleoscillator > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-26.r1/logs/build-3.log 2>&1; echo EXIT=$?  -> EXIT=0
ctest -R ReversibilityUndoTest --output-on-failure --verbose > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-26.r1/logs/test-4.log 2>&1; echo EXIT=$?  -> EXIT=0 (GREEN: 1/1 CTest passed)
```

The required fix-removal proof also reproduced the failure, then passed after reapplying the saved product patch:

```text
git diff -- src/core/ControlCommandsPlugin.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-26.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-26.r1/logs/fix.patch; echo EXIT=$?  -> EXIT=0
cmake --build build --target zene ReversibilityUndoTest tripleoscillator > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-26.r1/logs/build-4.log 2>&1; echo EXIT=$?  -> EXIT=0
ctest -R ReversibilityUndoTest --output-on-failure --verbose > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-26.r1/logs/test-5.log 2>&1; echo EXIT=$?  -> EXIT=8 (RED: args: missing required property 'device')
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-26.r1/logs/fix.patch; echo EXIT=$?  -> EXIT=0
cmake --build build --target zene ReversibilityUndoTest tripleoscillator > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-26.r1/logs/build-5.log 2>&1; echo EXIT=$?  -> EXIT=0
ctest -R ReversibilityUndoTest --output-on-failure --verbose > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-26.r1/logs/test-6.log 2>&1; echo EXIT=$?  -> EXIT=0 (GREEN: 1/1 CTest passed)
```

Required ratchet gates:

```text
bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-26.r1/logs/gate-1.log 2>&1; echo EXIT=$?  -> EXIT=0
bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-26.r1/logs/gate-2.log 2>&1; echo EXIT=$?  -> EXIT=0
```

The `fff grep` tool was not exposed in this session (`command -v fff` found no executable), so saved logs were searched with `rg` for the failure and gate results.

## Committed files

- `src/core/ControlCommandsPlugin.cpp`
- `tests/src/core/ReversibilityUndoTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p8-26.md`
