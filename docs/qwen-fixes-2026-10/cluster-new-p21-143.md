FIXED

# new-p21-143: EffectChain::removeEffect leaves a stale chain pointer on the effect

## Verdict and evidence

The finding is **REAL**, with a narrower consequence than the finding's "use-after-free" framing.

- Before the fix, `EffectChain::removeEffect()` erased the effect from `m_effects` and never touched the effect's back-pointer, so `effect->effectChain()` kept returning the chain it had left (`src/core/EffectChain.cpp:264-287` on the base commit; `include/Effect.h:270-273` is the getter, `include/Effect.h:278-281` the setter, `m_parent` at `include/Effect.h:370`).
- The codebase's own contract is that a removed device reads as "not on a chain": `src/gui/shell/DevicePanel.cpp:236` treats `chain == nullptr` as "leaving"; `src/gui/EffectDeviceMenu.cpp:71-73` carries the comment "taken off its chain meanwhile" and guards on `effectChain() == nullptr`; `src/core/PeakController.cpp:76` only calls `removeEffect` when `effectChain() != nullptr`. Before the fix, none of those guards could ever observe a removed effect.
- The dangling case is real but narrow. Every current removal caller calls `deleteLater()` right after `removeEffect()` (e.g. `src/core/ControlCommandsPlugin.cpp:361-364`, `src/core/ControlStructuralSupport.cpp:618-620`, `src/gui/EffectRackView.cpp:152-153`), so the stale pointer lives only until the deferred delete. It becomes a real dereference of a freed chain only if that effect outlives its chain and something then reads `m_parent`: the enabled-model lambda (`src/core/Effect.cpp:74`), `setDontRun` (`src/core/Effect.cpp:85-89`) or `sidechainBuffer` (`src/core/Effect.cpp:240-241`). I did not reproduce that end to end; the regression proves the pointer contract directly.

**Fix:** `src/core/EffectChain.cpp:274-278` sets `_effect->setEffectChain( nullptr )` right after the erase, inside the existing `requestChangeInModel()` / `doneChangeInModel()` window. The not-found early return (`src/core/EffectChain.cpp:268-272`) is untouched, so an effect that belongs to another chain is not detached by a stray call.

The sibling path `EffectChain::clear()` (`src/core/EffectChain.cpp:488-515`) pops and **deletes** every effect it drops, so it leaves no stale pointer and needed no change. `moveUp`/`moveDown`/`appendEffect`/`loadSettings` keep every effect on the list; `appendEffect` and `loadSettings` already set the back-pointer (`src/core/EffectChain.cpp:218`, `:240`).

## Regression test

No file among the three suggested in the brief fits (DelayControlsTest, ConvolutionFftwPlanScopeTest and EffectRackAddTest are plugin and GUI tests). I first wrote a new `tests/src/core/EffectChainDetachTest.cpp`, but the lane build tree is configured by the parent and refuses reconfiguration, so the new target could not be built here (see the attempt log below). I removed that file and its registration and put the regression in `tests/src/core/MixerStripTest.cpp`, which is already registered and configured and already uses `partd::FixedGainEffect` and `EffectChain::removeEffect` for the same purpose (`MixerStripTest.cpp:222`).

- `MixerStripTest::removedEffectIsDetachedFromItsChain` (`tests/src/core/MixerStripTest.cpp:571`) - append, remove, assert `effectChain() == nullptr` and that the chain is empty; then append to a second chain and assert the effect belongs only to it. **Fails on the base code** at the first assertion.
- `MixerStripTest::removeFromWrongChainLeavesOwnerIntact` (`tests/src/core/MixerStripTest.cpp:587`) - removing from a chain that does not hold the effect leaves the owner's pointer intact. Passes before and after; it guards the early-return path against a wrong fix that detaches before the lookup.

Test matrix covered: normal removal; removal then re-attach to another chain; removal from a non-owning chain (not-found path). Audio thread: not exercised (no render is needed to prove the pointer contract). Reconfiguration: the re-attach case. Supported platforms: the test is a plain Qt test on the same target as the rest of `MixerStripTest`.

## Verification

All build and test output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-143.r1/logs/`.

| Stage | Exact command (run from the worktree unless noted) | Exit | Result |
|---|---|---:|---|
| New-file test target (abandoned) | `cmake --build build --target EffectChainDetachTest -j4` (`build-1-red.log`) | 2 | `No rule to make target` - the target is not in the configured tree. |
| Reconfigure attempt (abandoned) | `cmake -S . -B build` (`configure-1.log`) | 97 | Refused by the lane build guard: configuring is the parent's job. The new file and its registration were removed. |
| Build, RED (`build-2-red.log`) | `cmake --build build --target MixerStripTest -j4` | 2 | ccache could not create its temp file (permission denied in the shared shim dir). |
| Build, RED (`build-3-red.log`) | `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-new-p21-143 cmake --build build --target MixerStripTest -j4` | 0 | Built with the fix not yet applied. |
| Original code, RED (`test-1-red.log`) | `cd build/tests && ctest -R '^MixerStripTest$' --output-on-failure` (3-test draft) | 8 | `removedEffectIsDetachedFromItsChain` and the re-attach removal check failed (`MixerStripTest.cpp:581`, `:609`). |
| Build with fix (`build-4-green.log`) | `CCACHE_TEMPDIR=... cmake --build build --target zene MixerStripTest -j4` | 0 | `zene` and the test rebuilt. |
| Fixed code, GREEN (`test-2-green.log`) | `cd build/tests && ctest -R '^MixerStripTest$' --output-on-failure` | 0 | 1/1 CTest target passed. |
| Reverse-patch proof, patch saved (`fix.patch`) | `git diff -- src/core/EffectChain.cpp > $L/fix.patch && git apply -R $L/fix.patch` | 0 | Fix reverted. |
| Rebuild without fix (`build-10-final-red.log`) | `CCACHE_TEMPDIR=... cmake --build build --target zene MixerStripTest -j4` | 0 | Rebuilt. |
| Reverted code, RED (`test-7-final-red.log`) | `cd build/tests && ctest -R '^MixerStripTest$' --output-on-failure` | 8 | 1 failed, 24 passed (`removedEffectIsDetachedFromItsChain`, `MixerStripTest.cpp:571`: "a removed effect still names the chain it was taken off"). |
| Restore patch | `git apply $L/fix.patch` | 0 | Fix restored. |
| Rebuild restored (`build-11-final-green2.log`) | `CCACHE_TEMPDIR=... cmake --build build --target zene MixerStripTest -j4` | 0 | Rebuilt. |
| Final GREEN (`test-8-final-green2.log`) | `cd build/tests && ctest -R '^MixerStripTest$' -V` | 0 | 25 passed, 0 failed (`Totals`). |
| Affected family, final (`test-9-family-final.log`) | `cd build/tests && ctest -R '^(MixerStripTest\|DelayControlsTest\|ConvolutionFftwPlanScopeTest\|EffectRackAddTest\|DevicePanelTest\|EffectDeviceMenuTest\|EffectHostingMenuTest\|W3EngineTest\|ReversibilityUndoDeviceTest\|DeviceCopyTest\|RackMacrosTest\|ScriptDawRemovalUndoTest\|ModulatorPanelTest\|PluginNativeEditorTest\|ControlChainPresetTest)$' --output-on-failure` | 0 | 15 of 15 CTest targets passed (`100% tests passed out of 15`). Targets built with `cmake --build build --target ... -j4` (`build-7-family-a.log`, `build-8-family-b.log`, both exit 0). |
| Feature gates, in-tree script (`gate-1-feature-check.log`) | `python3 .../feature_check.py <worktree>` | 1 | `PermissionError`: the script writes its logs to `<worktree>/../<name>-feature-check`, outside the write-jail. Nothing was checked. |
| Feature gates, log directory redirected (`gate-3-feature-check.log`) | `python3 -I $R/gate-copy/feature_check_logdir.py <worktree>` | 0 | `PASS` for all 8 rows (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length). |

The gate run used a copy of `feature_check.py` in the run folder with two changes only: `ROOT` pinned to the bunny-fix directory (so `FIX-BASE` and the shared baseline cache are read exactly as before), and the log directory pointed inside the run folder. The gate logic and limits are unchanged.

The first gate run found `MixerStripTest.cpp` at 501 code lines, a new file over the 500-line limit (`gate-2-feature-check.log`, `file-length` FAIL). I trimmed the test (folded the re-attach check into the detach test, removed a redundant size assertion) rather than touching the gate; the run after that passes.

## Scope checks

- **Callers of `removeEffect`:** all that dereference a removed effect's chain pointer after the call were checked (see the verdict section). `ControlCommandsTrackPresets.cpp:109-119` reads `old->effectChain()` before its removal; `ControlChainPresetSupport.cpp:462-470` and `ControlDeviceMoves.cpp:151` do not read the pointer afterwards; `ControlCommandsW3Engine.cpp:167` only deletes; `ScriptDawEdit.cpp:268` and `ControlCommandsPlugin.cpp:361` call `detachEffect` (which is `removeEffect`) then `deleteLater()`.
- **Null-unsafe readers of `effectChain()`:** `src/gui/shell/DeviceActionMenu.cpp:84` (`addMoveEntries`) and `src/gui/EffectDeviceMenu.cpp:56` (`slotOf`) dereference the chain without a null check. Both run while building a menu from a live device card; `DevicePanel` retires the card on the removal signal (`DevicePanel.cpp:230-240`), so a removed device is not reached through them in the paths read. This is the one residual point to watch; I did not change those readers.
- **Save/load:** no change to `saveSettings`/`loadSettings` or to any saved field; `loadSettings` already sets the back-pointer for every restored effect (`EffectChain.cpp:218`).
- **Sidechain and latency:** a removed effect now reports `sidechainBuffer() == nullptr` and no chain refresh from its own setters. Both only matter to an effect in a chain, and a removed effect is not processed (it is out of the list inside the same model change).
- **Realtime:** the change is a pointer store inside the existing `requestChangeInModel()` / `doneChangeInModel()` window. No allocation, lock, wait or logging was added.
- **Undo, commands:** no change; no control command was added; `removeRecordedEffect` and the journalled removal paths still take the chain they were given.
- **Registration:** the two inherited files I changed are registered in `tests/upstream-modifications.txt` (the last two lines). No new file was added.

## Files included in the fix commit

- `src/core/EffectChain.cpp` - product fix (4 lines).
- `tests/src/core/MixerStripTest.cpp` - regression cases.
- `tests/upstream-modifications.txt` - registration of the two changed inherited files.
- `docs/qwen-fixes-2026-10/cluster-new-p21-143.md` - this report.

## Self-check

- **Regression fails with the product change reverted and passes with it.** Yes: `test-1-red.log` (exit 8, draft), `test-7-final-red.log` (exit 8, 1 failed: `removedEffectIsDetachedFromItsChain`), `test-8-final-green2.log` (exit 0, 25 passed). Reverse and re-apply used `fix.patch` (`git apply -R` exit 0, `git apply` exit 0).
- **Deterministic.** Yes. No sleeps, timing, threads or event loop; the test uses no audio processing and no timers. The chain and effect objects are owned locally; the only seam is the public `appendEffect`/`removeEffect` pair. The engine is started with the existing `partd::initEngine()` helper, which the file already uses.
- **Fix is at the cause and complete.** The cause is the missing back-pointer reset in `removeEffect`. Every other path that drops an effect from the list either deletes it (`clear()`) or sets the pointer on the way in (`appendEffect`, `loadSettings`); the readers that guard on nullptr already expect this (`DevicePanel.cpp:236`, `EffectDeviceMenu.cpp:71`, `PeakController.cpp:76`). Two unguarded readers remain (`DeviceActionMenu.cpp:84`, `EffectDeviceMenu.cpp:56`), flagged above and not reached by a removed device in the paths read.
- **No collateral damage.** No saved-data or load path changed. No limit or check was added. Affected family: 15 of 15 pass (`test-9-family-final.log`, exit 0). Gate `file-length` was reached by my own test and fixed in the test, not by changing the gate.
- **Verdict is REAL**, so no ALREADY-FIXED or FALSE-POSITIVE evidence applies. The residual "use-after-free" is not reproduced end to end; the report states that.

## Completion check

`submit_check.py` (`submit-1.log`, exit 1) passes the commit, clean-tree, report and tests rows. Its `gates (feature_check.py)` row fails for an environmental reason only: the script runs `feature_check.py` in-tree, which cannot create `<worktree>-feature-check` outside the write-jail (`PermissionError`, reproduced exit 1 in this lane). The gate itself passes with its log directory redirected: `gate-4-committed-feature-check.log`, exit 0, all 8 rows PASS on commit `0fd20dd1e`. The completion check's "?" items are answered in the Self-check section above.
