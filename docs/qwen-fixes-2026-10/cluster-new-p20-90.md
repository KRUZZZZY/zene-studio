Verdict: FIXED

new-p20-90: An invalid selector slot ID indexes outside the button array (`plugins/Vibed/NineButtonSelector.cpp`, finding line 68; current tree line 92)

Evidence:

- The finding is real. `NineButtonSelector::buttonClicked(int id)` (`plugins/Vibed/NineButtonSelector.cpp:70-73`) forwards the raw ID to `setSelected(int)` (`:80-84`), which passed it to `updateButton(int)` (`:86-96`). `updateButton` indexes `m_buttons[newButton]` (`:92`) with no bounds check. `std::array<PixmapButton*, 9>` has no bounds check either, so `-1` or `9` is an out-of-bounds read.
- The constructor clamp (`:39-42`, used at `:49` and `:66`) does not cover this path. That is the finding's point.
- The model does not protect the button array: `AutomatableModel::fittedValue` clamps to the model's min/max (`src/core/AutomatableModel.cpp:604-606`), so the model value is always 0-8. Only the raw argument reaching `updateButton` is out of range.
- Fix: `setSelected` now clamps the ID with the existing `clampButtonIndex` before it updates the model or the button state (`plugins/Vibed/NineButtonSelector.cpp:82`). Clamping to the nearest button matches the constructor's contract and the model's own fitting. No caller legitimately passes a value outside 0-8.
- Regression: `outOfRangeClickSelectsNearestButton` in `tests/src/core/PluginLogoResourceTest.cpp:168-203`. It calls `buttonClicked(-1)` and `buttonClicked(9)` on a selector whose default is 4. It checks the model value, that every announced ID is in 0-8, that the last announcement is the nearest button (0 or 8), and that exactly one button is checked at that position. The stubbed `NineButtonSelection` now records into `emittedSelections` (`:55-65`) so the announcements can be checked. The stub was an empty body before.
- The finding's suggested target `ctest -R '^NineButtonSelector$'` does not exist. The file `tests/reference/Vibed/NineButtonSelector.cpp` is an old reference snapshot that no build target compiles. The closest coverage is `PluginLogoResourceTest`, which `#include`s `plugins/Vibed/NineButtonSelector.cpp` (`tests/src/core/PluginLogoResourceTest.cpp:47`). It is the test I used, along with `VibedWaveformHandoffTest`, which compiles the Vibed sources (`tests/CMakeLists.txt:1767-1781`).
- Registration: `tests/upstream-modifications.txt` gets a `new-p20-90` line for `plugins/Vibed/NineButtonSelector.cpp`, next to the `new-p9-320` line. The test file is already listed in `tests/all-sources.txt:2852`, and this change adds no new file.

Commands and exit codes (logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p20-90.r1/logs/`):

| Step | Command | EXIT | Result / log |
| --- | --- | --- | --- |
| RED build, first try | `cmake --build build --target PluginLogoResourceTest -j4 > build-1-red.log 2>&1; echo EXIT=$?` | 2 | ccache could not create its temp file (`dsh/home/ccache-tmp-r6`, outside the write-jail). Not a compile error. |
| RED build, retry | `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp/ccache-new-p20-90 cmake --build build --target PluginLogoResourceTest -j4 > build-2-red.log 2>&1; echo EXIT=$?` | 0 | Test compiled with the fix not yet applied. |
| RED test | `cd build/tests && ctest -R '^PluginLogoResourceTest$' --output-on-failure > test-1-red.log 2>&1; echo EXIT=$?` | 8 | `164 - PluginLogoResourceTest (SEGFAULT)`. The trace shows `NineButtonSelector::updateButton(int)` called from `outOfRangeClickSelectsNearestButton()`. |
| GREEN build | `cmake --build build --target PluginLogoResourceTest -j4 > build-3-green.log 2>&1; echo EXIT=$?` | 0 | |
| GREEN test | `cd build/tests && ctest -R '^PluginLogoResourceTest$' --output-on-failure > test-2-green.log 2>&1; echo EXIT=$?` | 0 | `100% tests passed out of 1` |
| Mutation: save fix | `git diff -- plugins/Vibed/NineButtonSelector.cpp > logs/fix.patch` | (none) | 12-line patch |
| Mutation: revert | `git apply -R logs/fix.patch; echo EXIT=$?` | 0 | |
| Mutation: rebuild | `cmake --build build --target PluginLogoResourceTest -j4 > build-4-reverted.log 2>&1; echo EXIT=$?` | 0 | |
| Mutation: RED | `cd build/tests && ctest -R '^PluginLogoResourceTest$' --output-on-failure > test-3-reverted-RED.log 2>&1; echo EXIT=$?` | 8 | SEGFAULT, as in the first RED run |
| Mutation: reapply | `git apply logs/fix.patch; echo EXIT=$?` | 0 | |
| Mutation: rebuild | `cmake --build build --target PluginLogoResourceTest -j4 > build-5-reapplied.log 2>&1; echo EXIT=$?` | 0 | |
| Mutation: GREEN | `cd build/tests && ctest -R '^PluginLogoResourceTest$' --output-on-failure > test-4-reapplied-GREEN.log 2>&1; echo EXIT=$?` | 0 | 1/1 passed |
| Direct run | `cd build/tests && ./PluginLogoResourceTest > direct-green-1.log 2>&1; echo EXIT=$?` | 0 | `PASS : PluginLogoResourceTest::outOfRangeClickSelectsNearestButton()`, `Totals: 6 passed, 0 failed` |
| Family build | `cmake --build build --target VibedWaveformHandoffTest -j4 > build-6-vibed-family.log 2>&1; echo EXIT=$?` | 0 | Recompiles `NineButtonSelector.cpp` |
| Family count | `cd build/tests && ctest -N -R 'Vibed\|PluginLogo\|NineButton'` | (none) | `Total Tests: 2` |
| Family run | `cd build/tests && ctest -R 'Vibed\|PluginLogo\|NineButton' --output-on-failure > test-5-family-GREEN.log 2>&1; echo EXIT=$?` | 0 | `100% tests passed out of 2`: PluginLogoResourceTest and VibedWaveformHandoffTest |

Gates: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` exited 1 before running any check (`gates-1.log`): `PermissionError` creating `<worktree>-feature-check` next to the worktree, which is outside the write-jail. I did not edit the shared script. I ran the same commands in the worktree, with logs in the log folder:

| Gate | Command (run in the worktree) | EXIT | Note |
| --- | --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 | |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 | |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 | |
| check-namespace | `tests/scripted/check-namespace` | 0 | |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 | |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 | |
| complexity | `bash tests/complexity-gate.sh --check` | 1 | 7 REGRESSION lines, none in `NineButtonSelector.cpp` or `PluginLogoResourceTest.cpp` (`gate-complexity.log`). By feature_check's rule this row passes. |
| file-length | `bash tests/file-length-gate.sh --check` | 1 | Listed items are other files; none in the two changed code files. By feature_check's rule this row passes. (`gate-file-length.log`) |

The base-listing comparison that feature_check uses to separate new items from old ones was not run, because it needs a checkout outside the jail. This is safe here: an item counts as NEW only when it appears in the current output inside a changed file, and none does.

Files committed for this verdict:

- `plugins/Vibed/NineButtonSelector.cpp`
- `tests/src/core/PluginLogoResourceTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p20-90.md`

No control command was added, so `ControlCommandsSnapshot` and its canary are unchanged. The build used `-j4`.

## Self-check

1. **The regression fails with the product change reverted and passes with it.** RED: `test-1-red.log` EXIT=8, SEGFAULT in `updateButton(int)`. Mutation: `test-3-reverted-RED.log` EXIT=8 after `git apply -R`; GREEN: `test-4-reapplied-GREEN.log` EXIT=0 after `git apply`. The RED failure is a crash, not a clean assertion. The `-1` case reads `m_buttons[-1]`, which is undefined behaviour. In practice it segfaults at `updateButton`, so the failure is deterministic on this build but not an assertion message.

2. **The test is deterministic.** It has no wall-clock timing, sleeps or threads. The injected values are fixed: a default of 4 and the clicks `-1` and `9`. The emitted announcements come from a recording stub of `NineButtonSelection` (`tests/src/core/PluginLogoResourceTest.cpp:55-65`), so no signal-queue timing is involved. The `ms` figures in the logs are informational only.

3. **The fix is at the cause, and callers and siblings were checked.** `setSelected` is reached only from `buttonClicked` (checked with `rg` over `plugins/Vibed` and `tests/src`). `buttonClicked` is reached from the `PixmapButton::clicked` lambda, which passes `i` in 0-8 (`NineButtonSelector.cpp:63`), and from direct calls to the public slot. `updateButton` is reached from `setSelected` (now clamped) and from `modelChanged` (`:77`), which passes `model()->value()`. That value is already fitted to [0,8] (`AutomatableModel.cpp:604-606`). `Vibed.cpp:399` connects `NineButtonSelection` to `showString(int)`, which now only receives 0-8. The constructor clamp (`:39-42`, `:49`, `:66`) is unchanged.

4. **No collateral damage.** The clamp changes only out-of-range IDs. In-range clicks are unchanged, and the model's journaling and undo behaviour is unchanged: it is still one `setValue` call. Saved projects store the model value, which was already fitted to [0,8] on load, so loading is unchanged. The clamp range is exactly the button array (9 buttons, IDs 0-8), so no legitimate caller is affected. The verdict is FIXED, so no ALREADY-FIXED or FALSE-POSITIVE evidence is needed.
