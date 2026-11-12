Verdict: FALSE-POSITIVE

new-p12-114: Mallets can index its scaler table with a project-supplied invalid preset

The finding says a Mallets preset below 0 or above 13 survives in the model and indexes `m_scalers` out of bounds during playback. On this branch it does not: every path that can write the preset model fits the value into the model's range `[0, 13]` before it is stored, and the scaler table has exactly 14 entries.

## Code evidence

- The preset table has 14 items and 14 scalers, one per preset 0-13: `plugins/Stk/Mallets/Mallets.cpp:241-272` (14 `m_presetsModel.addItem` and 14 `m_scalers.append`, counted with `rg -c`).
- The range is `[0, N-1]` = `[0, 13]`: `include/ComboBoxModel.h` inherits `IntModel`; `src/core/ComboBoxModel.cpp:32-36` `addItem()` calls `setRange( 0, m_items.size() - 1 )`.
- Every value write is fitted: `src/core/AutomatableModel.cpp:604-616` `fittedValue()` clamps to `[m_minValue, m_maxValue]`, and is applied in:
  - the constructor, `src/core/AutomatableModel.cpp:156`;
  - `setValue()` / `setValueInternal()`, `src/core/AutomatableModel.cpp:465` and `:500`;
  - `setInitValue()`, `src/core/AutomatableModel.cpp:1061`, which the project load path reaches through `loadSettings()` at `src/core/AutomatableModel.cpp:440`;
  - automation buffers, `src/core/AutomatableModel.cpp:906`;
  - `setRange()`, `src/core/AutomatableModel.cpp:567-585`, which re-fits the current value (`:583`).
- The preset is read as `int p = m_presetsModel.value();` at `plugins/Stk/Mallets/Mallets.cpp:451` and indexes `m_scalers[p]` at `:556-557`. Because the model value is always in `[0, 13]`, the index is always in range.
- The finding's line numbers have moved. `plugins/Stk/Mallets/Mallets.cpp:193-195` is now the `return nullptr;` of `findFreeSlot()`, which is unrelated to presets.

## Runtime evidence

A regression probe was added to the existing `MalletsInstrumentViewTest`: `tests/src/plugins/MalletsInstrumentViewTest.cpp:132-151` (`aLoadedPresetOutsideTheScalerTableIsFittedIntoIt`). It loads `preset` attributes through the real `MalletsInstrument::loadSettings()` and reads back the value `saveSettings()` writes, which is the value the synth indexes. Results on the unmodified product code:

| loaded | saved (expected) |
| --- | --- |
| -1 | 0 |
| 0 | 0 |
| 13 | 13 |
| 14 | 13 |
| 99 | 13 |

The out-of-range inputs (-1, 14, 99) are the discriminating cases: without the clamp they would save unchanged.

## Commands and exit codes

No product code was changed, so the RED/GREEN revert-and-reapply step (4b) has no product diff to revert. The test is a guard that passes on the code as it is; it is not a regression for a bug that exists.

| # | Command (run from the worktree) | Log | Result |
| --- | --- | --- | --- |
| 1 | `cmake --build build --target MalletsInstrumentViewTest -j4 > .../build-1.log 2>&1; echo EXIT=$?` | build-1.log | EXIT=2: ccache could not create a temp file under `~/.local/share/zene-bunny-fix/dsh/...` (Permission denied). Not a code error. |
| 2 | `CCACHE_DISABLE=1 cmake --build build --target MalletsInstrumentViewTest -j4 > .../build-2.log 2>&1; echo EXIT=$?` | build-2.log | EXIT=0 (ccache bypassed; compiler run directly) |
| 3 | `cd build/tests && ctest -R '^MalletsInstrumentViewTest$' --output-on-failure > .../test-1.log 2>&1; echo EXIT=$?` | test-1.log | EXIT=0, 1/1 passed |
| 4 | `cd build/tests && ctest -R '^MalletsInstrumentViewTest$' -V > .../test-2.log 2>&1; echo EXIT=$?` | test-2.log | EXIT=0; `PASS: aLoadedPresetOutsideTheScalerTableIsFittedIntoIt()`; Totals 4 passed, 0 failed |
| 5 | `cd build/tests && ctest -N -R '^Mallets$'` | (stdout) | `Total Tests: 0`: the `^Mallets$` reference test is not registered in this build |

Affected family: `ctest -R '^MalletsInstrumentViewTest$'` ran 1 test (4 test functions inside it), 0 failures.

Gates. `feature_check.py` could not run as-is: it writes its logs to `<worktree>/../<name>-feature-check`, which is outside the write-jail, and failed with `PermissionError` (EXIT=1, log `gate-1.log`). Its commands were run by hand from the worktree:

| Gate | Command | EXIT |
| --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 |
| check-namespace | `tests/scripted/check-namespace` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 (7 `REGRESSION:` lines; none names `MalletsInstrumentViewTest.cpp`) |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 (22 `REGRESSION:` lines; none names `MalletsInstrumentViewTest.cpp`) |

The two ratchets exit 1 on items elsewhere in the tree, which the brief says is expected on the train. I did not reproduce the script's baseline comparison (it needs a worktree under `zene-cachyos` and a cache outside the jail), so the ratchet rows are only checked by "no reported item in the changed file", not by the full baseline diff.

Files committed: `tests/src/plugins/MalletsInstrumentViewTest.cpp` (guard test and its `savedPreset` helper) and this report.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.** Not applicable: there is no product change to revert. The test passes on the unmodified code (test-1.log, test-2.log), which is the evidence the finding does not reproduce. The out-of-range inputs in the test would expose a missing clamp (saved value would stay -1, 14 or 99).
- **Deterministic.** The test uses no clock, sleep or thread. It drives `loadSettings()` with fixed attribute strings and reads `saveSettings()` back. The `Engine::init(true)` and offscreen setup are the existing fixture's.
- **Fix at the cause and complete.** Not applicable (no fix). The checked callers are listed in the code evidence: every `setValue`, `setInitValue`, `setRange`, constructor, load and automation write goes through `fittedValue()`.
- **No collateral damage.** No product code changed. The probe only adds a test that loads values the instrument already accepts.
- **Verdict evidence.** The code and intended contract are cited above (`AutomatableModel.cpp:604-616` clamp on every write, `ComboBoxModel.cpp:32-36` range `[0, 13]`, `Mallets.cpp:241-272` 14 presets / 14 scalers).

Notes for the reviewer:

- `tests/src/plugins/MalletsInstrumentViewTest.cpp` is a fork-new file (added by `new-p17-90`) and is not listed in `tests/fork-sources.txt`. I did not edit that generated, byte-exact list; the `fork-sources` gate still exits 0.
- The plain `cmake --build` fails in this sandbox because of the ccache temp-directory permission, not the code. `CCACHE_DISABLE=1` is the workaround used.
