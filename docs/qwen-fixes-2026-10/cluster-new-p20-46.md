FALSE-POSITIVE

new-p20-46: "Opening a missing plug-in's effect menu can crash" (`src/gui/EffectDeviceMenu.cpp:165`)

## Verdict

The finding's premise does not hold on this branch. `Effect::descriptor()` cannot return null for any effect, including a missing plug-in's `DummyEffect`. The menu therefore dereferences a valid descriptor, and the descriptor belongs to the placeholder rather than to `convolutionreverb`, so no crash is possible on that line.

## Evidence (file:line)

1. **Every `Plugin` gets a non-null descriptor.** `Plugin::Plugin` replaces a null descriptor with the static `dummyPluginDescriptor`: `src/core/Plugin.cpp:74-77` (`if( m_descriptor == nullptr ) { m_descriptor = &dummyPluginDescriptor; }`). `Plugin::descriptor()` returns `m_descriptor` unchanged (`include/Plugin.h:260-263`).

2. **The placeholder descriptor is named "dummy".** `src/core/Plugin.cpp:52-54` defines `dummyPluginDescriptor` with name `"dummy"`. So for a missing plug-in, `effect->descriptor()->name` is `"dummy"`, not `"convolutionreverb"`, and the branch at `src/gui/EffectDeviceMenu.cpp:165` is simply not taken.

3. **Missing plug-ins really are `DummyEffect`s, and they pass through the same constructor.** `include/DummyEffect.h:97` calls `Effect( nullptr, _parent, nullptr )`, which reaches the fallback in item 1. The save path handles them separately (`src/core/EffectChain.cpp:146`), as the finding says, but that does not affect the menu.

4. **The menu does reach `addParameterEntries` for a dummy.** `src/gui/EffectDeviceMenu.cpp:271-281` (`addEffectDeviceMenus`) calls `deviceAddress(effect)`, which is non-empty for a loaded dummy (`src/gui/EffectDeviceMenu.cpp:202-208`). The existing test `tests/src/gui/EffectHostingMenuTest.cpp:107` (`aLoadedDummyEffectHasADeviceAddress`) asserts exactly that. So the call at line 281 runs on a dummy, and the descriptor it reads is the valid placeholder from item 1.

5. **Line 165 is the only unguarded descriptor read in the menu.** `rg -n "descriptor\(\)" src/gui/EffectDeviceMenu.cpp` returns only line 165. The `effect` argument is non-null on every path into `addParameterEntries`: `addParameterMenus` (line 268) and `addEffectDeviceMenus` (line 281) both sit behind a `deviceAddress` check, and `deviceAddress` returns `{}` for a null effect (`src/gui/EffectDeviceMenu.cpp:203`).

The "fix" in the finding (skip descriptor-specific entries when there is no descriptor) would guard a state that cannot occur. A null-descriptor guard would be dead code, so no product change is made.

## Commands run

| Command | EXIT |
| --- | --- |
| `rg -n "m_descriptor = &dummyPluginDescriptor" src/core/Plugin.cpp` | 0 |
| `sed -n 52,54p src/core/Plugin.cpp` | 0 |
| `rg -n "Effect\( nullptr, _parent, nullptr \)" include/DummyEffect.h` | 0 |
| `rg -n "descriptor\(\)" src/gui/EffectDeviceMenu.cpp` | 0 |
| `rg -n "aLoadedDummyEffectHasADeviceAddress" tests/src/gui/EffectHostingMenuTest.cpp` | 0 |

Output of these is recorded in `logs/evidence-1.log` in the LOG FOLDER (`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p20-46.r1/logs/evidence-1.log`).

## Regression test

None added. The finding is not real, so there is no fix for a regression test to prove. Steps 3 and 4b do not apply: no product file changed, so `fix.patch` is not needed and no RED/GREEN pair exists.

Not run: the `EffectDeviceMenuTest` and `EffectHostingMenuTest` binaries are not built in `build/tests` (verified with `ls build/tests`), so no ctest family count is reported. The verdict rests on the code evidence above, not on a runtime crash test.

## Gates

`feature_check.py` could not run as written: it creates its log folder at `<worktree>-feature-check`, a sibling of the worktree, which is outside the write-jail (`PermissionError`, log in the LOG FOLDER). I ran each gate command it lists directly from the worktree, logging to the LOG FOLDER:

| Gate | Command | EXIT |
| --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 |
| check-namespace | `tests/scripted/check-namespace` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 (7 REGRESSION lines) |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 (22 REGRESSION lines) |

The two ratchet gates are red on the train, as the task states. Their REGRESSION lines name `src/` and `tests/` files (for example `src/wasm/WasmSandbox.cpp`, `src/core/ControlCommandsMixerTools.cpp`). This branch changes only `docs/qwen-fixes-2026-10/cluster-new-p20-46.md`, so no item is in a changed file, and the feature_check rule (fail only on new items in changed files) gives PASS for both. The exit code of the raw command is still 1.

## Self-check

- **Regression test RED/GREEN:** not applicable. The finding is a FALSE-POSITIVE, so the branch has no product change to revert. No test was added.
- **Deterministic test:** not applicable (no test).
- **Fix at the cause and complete:** not applicable (no fix). Sibling check: `rg -n "descriptor\(\)" src/gui/EffectDeviceMenu.cpp` returns only line 165, so the menu has no other descriptor dereference to guard.
- **No collateral damage:** the branch changes no product code, so saved projects and load/save behaviour are unchanged.
- **FALSE-POSITIVE evidence:** the null-descriptor path cannot happen (`src/core/Plugin.cpp:74-77`), the placeholder descriptor is named `"dummy"` (`src/core/Plugin.cpp:52-54`), and a loaded dummy does reach `addParameterEntries` with a valid descriptor (`src/gui/EffectDeviceMenu.cpp:271-281`, `tests/src/gui/EffectHostingMenuTest.cpp:107`). Details above.
- **Gates:** see the Gates section. `feature_check.py` itself could not write its log folder from the jail; the component gates pass, and the two ratchet gates fail only on items in files this branch does not change.
