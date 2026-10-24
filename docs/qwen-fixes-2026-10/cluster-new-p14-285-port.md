FIXED

# Cluster new-p14-285-port: full script command queues

## Decision and integration result

The finding is REAL. Before the accepted patch, `ScriptEngine::enqueue` discarded the queue result, and `enqueueDaw` discarded that `void` call. A full queue therefore dropped a DAW edit without telling the script. The accepted patch changes the queue/engine enqueue APIs to return admission status, reports failed DAW edits, reserves a model edit's checkpoint and value write together, and handles the two sibling one-command paths.

The pinned target is `aac861e62bba78448c3ff14e7077cc7c9712e4f9`; the accepted source commit is `3b5b9a24112e73d0463250b8ca8fe901e6e7b3c2`. The three registration conflicts were resolved as a union: the target's `ScriptDawModelRegistrationTest` and both accepted regression tests remain registered in `tests/CMakeLists.txt`, `tests/fork-sources.txt`, and `tests/all-sources.txt`. The new Qt tests share the existing offscreen platform setup.

Two target differences were demonstrated and handled in tests without changing accepted production behavior:

- The source branch's `ScriptDawBindings::newEffect` accepted an `EffectChain*`; the pinned target exposes `newEffect(const QString& mixerChannelId, Effect*)` (`include/ScriptDawBindings.h:249`). The queue-full test now obtains the target's stable channel ID through `newMixerChannel(...).id()`.
- On this target, `LuaEffect::m_effective()` resolves through `liveChain()`, which calls `syncForRead()` before the switch (`src/core/ScriptDawEffects.cpp:52-57,124-131`). Thus the effect switch drains the existing queue before enqueueing. The effect case now proves that the pending queue is drained and the switch is applied without another drop or a false refusal. `setName`, paired mixer model edits, direct `LuaFloatModel` writes, and registry calls still exercise their actual full-queue refusal paths.

The mandatory registration gate also exposed the pinned train's existing `tests/all-sources.txt` mismatch: it carried 1,148 vendored sfizz paths even though the manifest recipe excludes `plugins/SfzPlayer/sfizz/`. The manifest header documents this recurring train snapshot issue and directs regeneration from its recipe. I rederived only the entry list from that recipe; the two new regression source paths are present in its output. `tests/all-sources-reproduce.sh` and the final feature gate then passed.

## Affected failure paths and hazards

| Path | Current behavior and evidence |
| --- | --- |
| One-command DAW edits | `enqueueDaw()` reports a queue-full refusal for the DAW edit callers (`src/core/ScriptDawEdit.cpp:113-118,349-454`). The test exercises rename and mute; `addChannel`, `loadEffect`, and `removeEffect` share this helper. The target's effect wrapper synchronizes first, as described above. |
| Mixer fader, mute, and solo | `enqueueModelWrite()` uses `ScriptEngine::enqueue(checkpoint, write)` so both commands fit or neither does (`src/core/ScriptDawEdit.cpp:135-156`). A one-slot queue refuses the pair without moving the model or adding an empty undo step; a fitting pair applies and one undo restores it (`ScriptDawPairReservationTest.cpp:136-191`). |
| Direct model wrapper | `LuaFloatModel::setValue()` reports a failed single-command enqueue (`src/core/ScriptDawBindings.cpp:629-644`). The full-queue regression uses a direct model wrapper so it reaches the admission failure without the mixer-ID read synchronization (`ScriptDawQueueFullTest.cpp:177-199`). |
| `zene.command()` bridge | `luaCommand()` returns typed `refused` immediately when the queue rejects the request (`src/core/ScriptCommandBridge.cpp:273-309`). Its test verifies a saturated queue yields `nil, ..., "refused"` rather than the previous default `busy` response (`ScriptCommandBridgeTest.cpp:110-128`). |
| Legacy v0 binding callers | `src/core/ScriptBindings.cpp` still ignores the returned bool. This preserves its documented drop-and-count contract (`docs/LUA-API-STABILISATION.md:29`) and is outside the DAW-control binding finding. |
| Queue/audio thread | The new pair operation checks fixed ring-buffer space, writes a two-command stack array, and updates the existing relaxed dropped counter (`src/core/ScriptCommandQueue.cpp:57-80`). The producer never waits. No code was added to the audio thread; `audioThreadTick()` remains a pair of relaxed reads. Refusal logging occurs on the script caller thread. |
| Undo, commands, and project data | A rejected pair leaves no checkpoint; a successful pair remains one undoable change. No control command, project serialization, saved-data limit, or load path changed. Existing projects and saved data are unaffected. |
| Supported platforms | The change uses the existing C++20 lockless ring buffer, `std::size_t`, fixed stack storage, and Qt logging; it adds no OS-specific API or command layout. The configured host/build verified here is Linux x86-64, Qt 6.12.0, GCC 16.2. Windows and macOS builds were not available in this tree, so they are not claimed. |

## Evidence

| Check | Result |
| --- | --- |
| Full header-dependent build | `build-4.log`: `EXIT=0`, `zene` plus all 16 test targets directly including `ScriptEngine.h` built. The later test-only additions were rebuilt in `build-10.log` and `build-12.log`, both `EXIT=0`. |
| Focused RED proof with accepted product changes reversed | `test-9.log`: `EXIT=8`; all 3 CTest targets failed on the intended regressions (bridge returned `busy`, direct DAW/model writes had no refusal, and the one-slot pair had no refusal). |
| Focused GREEN after restoring accepted changes | `test-10.log`: `EXIT=0`; 3/3 targets passed: `ScriptCommandBridgeTest`, `ScriptDawQueueFullTest`, and `ScriptDawPairReservationTest`. |
| Script/Lua family | `test-8.log`: `EXIT=8`; 16 targets ran, 13 passed and 3 existing environment-dependent targets failed: `ControlAutomationScriptTest` could not find expected fresh-song track fixtures, `ScriptEngineTest` could not load `tripleoscillator`, and `ScriptMemoryBudgetTest`'s simulated impossible allocation succeeded under host overcommit. The accepted source report records the same failures; the new focused targets passed in this run. |
| Registration gate diagnosis | `gate-1.log`, `gate-fork-2.log`, and `gate-all-2.log`: `EXIT=1` before rederiving the whole-tree source manifest; the exact diff was the 1,148 sfizz paths documented in the pinned manifest header. After rederiving, `gate-3.log` and `gate-4.log` both passed. |
| Feature gates | `gate-6.log`: `EXIT=0`, all 8 checks passed. Complexity and file length report no new item in the changed files. |
| Completion check | Precommit `submit-1.log`: `EXIT=1` because no integration commit existed and the tree was staged. Postcommit `submit-2.log`: `EXIT=0`, `RESULT: PASS - submit`; commit, clean-tree, report, test-registration, and gate checks all pass. The checker listed its five ASK items; each is answered in “Self-check.” |

## Exact commands and exit codes

All logs are new numbered files in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-285-port.r6/logs`.

```text
cmake --build build --target zene ControlAutomationScriptTest ScriptBindingsTest ScriptCommandBridgeTest ScriptClockTest ScriptDawBindingTest ScriptDawEffectsTest ScriptDawModelRegistrationTest ScriptDawPairReservationTest ScriptDawQueueFullTest ScriptDawRemovalUndoTest ScriptEngineTest ScriptImportTest ScriptMemoryBudgetTest ScriptStabilisationTest LivecodePanelTest ScriptBudgetActionTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-285-port.r6/logs/build-4.log 2>&1; echo EXIT=$?  # EXIT=0
cmake --build build --target zene ScriptCommandBridgeTest ScriptDawQueueFullTest ScriptDawPairReservationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-285-port.r6/logs/build-10.log 2>&1; echo EXIT=$?  # EXIT=0
ctest -R 'ScriptDawQueueFullTest|ScriptDawPairReservationTest|ScriptCommandBridgeTest' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-285-port.r6/logs/test-10.log 2>&1; echo EXIT=$?  # EXIT=0 (run from build/tests; 3/3 targets passed)
git diff --cached -- include/ScriptEngine.h src/core/ScriptCommandBridge.cpp src/core/ScriptCommandQueue.cpp src/core/ScriptDawBindings.cpp src/core/ScriptDawEdit.cpp src/core/ScriptEngine.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-285-port.r6/logs/fix.patch; echo EXIT=$?  # EXIT=0
git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-285-port.r6/logs/fix.patch; echo EXIT=$?  # EXIT=0
cmake --build build --target zene ScriptCommandBridgeTest ScriptDawQueueFullTest ScriptDawPairReservationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-285-port.r6/logs/build-11.log 2>&1; echo EXIT=$?  # EXIT=0
ctest -R 'ScriptDawQueueFullTest|ScriptDawPairReservationTest|ScriptCommandBridgeTest' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-285-port.r6/logs/test-9.log 2>&1; echo EXIT=$?  # EXIT=8 (3/3 targets failed on the intended assertions)
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-285-port.r6/logs/fix.patch; echo EXIT=$?  # EXIT=0
cmake --build build --target zene ScriptCommandBridgeTest ScriptDawQueueFullTest ScriptDawPairReservationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-285-port.r6/logs/build-12.log 2>&1; echo EXIT=$?  # EXIT=0
ctest -R 'Script|Lua' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-285-port.r6/logs/test-8.log 2>&1; echo EXIT=$?  # EXIT=8 (16 targets: 13 passed, 3 environment-dependent failures)
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-integrate-new-p14-285-aac861e62b > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-285-port.r6/logs/gate-6.log 2>&1; echo EXIT=$?  # EXIT=0
```

The following integration diagnostics are recorded to make the compatibility adjustments reviewable:

```text
cmake --build build --target zene ControlAutomationScriptTest ScriptBindingsTest ScriptCommandBridgeTest ScriptClockTest ScriptDawBindingTest ScriptDawEffectsTest ScriptDawModelRegistrationTest ScriptDawPairReservationTest ScriptDawQueueFullTest ScriptDawRemovalUndoTest ScriptEngineTest ScriptImportTest ScriptMemoryBudgetTest ScriptStabilisationTest LivecodePanelTest ScriptBudgetActionTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-285-port.r6/logs/build-1.log 2>&1; echo EXIT=$?  # EXIT=2; first CMake regeneration had not exposed the new pair target at the root
cmake --build build --target ScriptDawPairReservationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-285-port.r6/logs/build-2.log 2>&1; echo EXIT=$?  # EXIT=0
cmake --build build --target zene ControlAutomationScriptTest ScriptBindingsTest ScriptCommandBridgeTest ScriptClockTest ScriptDawBindingTest ScriptDawEffectsTest ScriptDawModelRegistrationTest ScriptDawPairReservationTest ScriptDawQueueFullTest ScriptDawRemovalUndoTest ScriptEngineTest ScriptImportTest ScriptMemoryBudgetTest ScriptStabilisationTest LivecodePanelTest ScriptBudgetActionTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-285-port.r6/logs/build-3.log 2>&1; echo EXIT=$?  # EXIT=2; source test used the old EffectChain* factory API
ctest -R 'ScriptDawQueueFullTest|ScriptDawPairReservationTest' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-285-port.r6/logs/test-1.log 2>&1; echo EXIT=$?  # EXIT=8; effect expectation contradicted stable-ID pre-read semantics
cmake --build build --target zene ScriptCommandBridgeTest ScriptDawQueueFullTest ScriptDawPairReservationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-285-port.r6/logs/build-8.log 2>&1; echo EXIT=$?  # EXIT=2; signed comparison in the first bridge queue-fill assertion
ctest -R 'ScriptDawQueueFullTest|ScriptDawPairReservationTest|ScriptCommandBridgeTest' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-285-port.r6/logs/test-6.log 2>&1; echo EXIT=$?  # EXIT=8; attempted queue fill left three commands short of the assumed capacity
bash tests/fork-sources-gate.sh > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-285-port.r6/logs/gate-fork-2.log 2>&1; echo EXIT=$?  # EXIT=1; its nested whole-tree manifest check found stale sfizz entries
bash tests/all-sources-reproduce.sh > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-285-port.r6/logs/gate-all-2.log 2>&1; echo EXIT=$?  # EXIT=1; stale sfizz entries
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-integrate-new-p14-285-aac861e62b > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-285-port.r6/logs/gate-1.log 2>&1; echo EXIT=$?  # EXIT=1; stale sfizz entries
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-integrate-new-p14-285-aac861e62b new-p14-285-port > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-285-port.r6/logs/submit-1.log 2>&1; echo EXIT=$?  # EXIT=1; precommit check correctly required a commit and clean tree
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-integrate-new-p14-285-aac861e62b new-p14-285-port > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-285-port.r6/logs/submit-2.log 2>&1; echo EXIT=$?  # EXIT=0; RESULT: PASS - submit
```

The target API mismatch was found by the first test build; changing the test to pass the stable channel ID made the full build pass. The first focused run showed that this target's stable-ID effect wrapper drains pending commands; the test was updated to assert that behavior. The bridge queue-fill test initially assumed all nominal ring slots were usable; it now confirms actual overflow through the drop counter. The final expanded reversed-patch proof is the RED/GREEN evidence above.

The train's whole-tree manifest was regenerated from the command in its own header, with only the documented excluded sfizz entries removed and the two new test paths emitted by `git ls-files`:

```text
awk '/^[[:space:]]*[^#[:space:]]/ { exit } { print }' tests/all-sources.txt > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-285-port.r6/logs/all-sources-header-1.txt && { cat /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-285-port.r6/logs/all-sources-header-1.txt; { git ls-files '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hpp' | grep -vE '^(src/3rdparty/|tests/reference/|plugins/NeuralAmp/(rtneural|nam|tests)/|plugins/RnnoiseDenoiser/rnnoise/|plugins/SfzPlayer/sfizz/)' ; git ls-files tests/control-detect-commands.py tests/control_detect_fixtures.py ; } | LC_ALL=C sort -u; } > tests/all-sources.txt && git add tests/all-sources.txt; echo EXIT=$?  # EXIT=0
```

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p14-285.md` (accepted issue report)
- `docs/qwen-fixes-2026-10/cluster-new-p14-285-port.md` (this integration report)
- `include/ScriptEngine.h`
- `src/core/ScriptCommandBridge.cpp`
- `src/core/ScriptCommandQueue.cpp`
- `src/core/ScriptDawBindings.cpp`
- `src/core/ScriptDawEdit.cpp`
- `src/core/ScriptEngine.cpp`
- `tests/CMakeLists.txt`
- `tests/all-sources.txt`
- `tests/fork-sources.txt`
- `tests/src/core/ScriptCommandBridgeTest.cpp`
- `tests/src/core/ScriptDawPairReservationTest.cpp`
- `tests/src/core/ScriptDawQueueFullTest.cpp`

## Self-check

- The finding is REAL: the accepted report records the pre-fix `void enqueue()` and ignored push result; the final reversed-patch run reproduces the lost refusals and stranded-checkpoint path.
- The focused tests are deterministic: they saturate the bounded queue through enqueue calls, use no sleeps or timing assumptions, verify state/log/reply, and cover both the one-slot refusal and a fitting undoable pair.
- All changed queue admission callers were checked: DAW writes, paired model writes, direct `LuaFloatModel::setValue`, `zene.command`, and legacy v0 calls. The legacy caller behavior remains documented and unchanged.
- No saved-data or serialization code changed. No audio-thread allocation, free, lock, wait, or logging was added. Undo remains one step; no control command was added.
- Registration and final gates pass. The exact current-host limitation is recorded above; no cross-platform build is claimed.
- The focused family ran 3 CTest targets and passed 3/3 after restoration. The wider `Script|Lua` family ran 16 targets; the three environment failures and their evidence are listed above.
- The exact changed and committed file list is recorded in “Files committed.”
- The conditional ALREADY-FIXED/FALSE-POSITIVE evidence question does not apply: the report verdict is FIXED, and the bug is demonstrated by the reversed-patch failures.
