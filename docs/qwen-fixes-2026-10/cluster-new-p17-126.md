FIXED

# Cluster new-p17-126: editor header actions discard command errors

Finding: `src/gui/PluginNativeEditor.cpp:88` (historical line; the header's `headerButton` ignored the `runCommand` result), and the Bypass toggle reverted itself on refusal with no reason shown.

## Verdict and evidence

**Real, partly overstated.** Before the fix, the header kept no reason: a refused Store A/B, A/B or Bypass left nothing on the header itself, and Bypass only reverted (`follow()`). The finding says the user "gets no explanation", but that is not quite right: `runCommand` always calls `announce()` for a failed call (`src/gui/CommandRun.cpp:87-96`, called at `:111`), which shows the error text in a transient float for 4 s. The real gap is the missing persistent reason in the header, which the generated editor button already has (`src/gui/PluginNativeEditor.cpp:288`, its `reason` label).

Fix (`src/gui/PluginNativeEditor.cpp`):
- `reportResult` / `runHeaderCommand` (lines 78-91): a refusal's `errorMessage` goes to the header's reason label; a successful command clears it. A form the command opened (an `ask` command: Save Preset, Load Preset) is not a refusal and is not reported. `runCommand` returns a `Busy` failure in that case, so the form check is what keeps "waiting for the form" off the label.
- `headerButton` (line 94) and `bypassButton` (line 111) take the reason label; every header button passes it (`hostHeader`, line 137).
- `hostHeader` (lines 142-152): the header is now a column — the button row, then a hidden reason label (`objectName` `pluginHostReason`). The label is hidden at rest, so the normal header size does not change.
- The float from `announce()` is kept; this change is additive and does not alter it.

Test (`tests/src/gui/PluginNativeEditorTest.cpp`): new `aRefusedHeaderCommandSaysWhy` (line 287) with an `EngineNotReady` RAII guard (line ~126) that makes every command refuse deterministically (`ControlRegistry::isEngineReady()` is false, so `invoke` returns `Busy`; no timing, no sleeps). It checks: a refused Store A shows its reason; a refused Bypass goes back to unchecked, the effect stays enabled, and the reason shows; the next command that runs hides the reason.

Sibling checked: `src/gui/shell/DeviceCard.cpp:246` (the device card's power button) has the same revert-on-failure shape, but it is outside this cluster's surface and is unchanged. Every header button (`hostHeader`) goes through `headerButton` or `bypassButton`, so all are covered. The generated editor button (`editorButton`) is untouched.

Not a command change: no control command was added or altered; no snapshot or canary changes. Realtime: GUI-only code, nothing on an audio path. Undo: each header click still runs one command (unchanged).

## Test environment note

`plugin "amplifier"` is not built in this tree by default. On the first RED run the new test (and the three existing header tests) were **skipped** ("this host loads no effect module") and ctest still reported the family as passed. That run proved nothing, so I built the `amplifier` target (`build-3-amplifier.log`) and reran RED. `tripleoscillator` is also not found on this host, so `anInstrumentsHeaderHasPresetsButNoBypass` and `theShowEditorButtonSaysWhyWhenThereIsNone` pass without exercising an instrument (pre-existing; not changed).

## Commands and EXIT codes

All logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-126.r1/logs/`. Builds used `CCACHE_TEMPDIR=/tmp/ccache-tmp-p17` because ccache's default temp dir under `/run/user/1000` was not writable (first build attempt: `build-1-red.log`, EXIT=2, ccache permission error, not a code error).

| Step | Command (cwd = worktree unless noted) | EXIT | Log |
|---|---|---|---|
| RED build, first attempt | `cmake --build build --target PluginNativeEditorTest -j4` | 2 | build-1-red.log (ccache) |
| RED build | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p17 cmake --build build --target PluginNativeEditorTest -j4` | 0 | build-2-red.log |
| RED run, amplifier not built (invalid, new test skipped) | `cd build/tests && ctest -R '^PluginNativeEditorTest$' -V` | 0 | test-3-red-verbose.log |
| Build amplifier plugin | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p17 cmake --build build --target amplifier -j4` | 0 | build-3-amplifier.log |
| **RED** (unfixed product code, test file has the new test) | `cd build/tests && ctest -R '^PluginNativeEditorTest$' -V` | **8** | test-4-red-verbose.log: `FAIL! aRefusedHeaderCommandSaysWhy() ... (PluginNativeEditorTest.cpp(301))`; Totals 9 passed, 1 failed |
| GREEN build (fix applied) | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p17 cmake --build build --target PluginNativeEditorTest -j4` | 0 | build-5-green.log |
| **GREEN** | `cd build/tests && ctest -R '^PluginNativeEditorTest$' -V` | **0** | test-5-green-verbose.log: Totals 10 passed, 0 failed, 0 skipped |
| Proof: revert product hunk | `git diff -- src/gui/PluginNativeEditor.cpp > logs/fix.patch && git apply -R logs/fix.patch` | 0 | — |
| Proof: rebuild unfixed | `CCACHE_TEMPDIR=... cmake --build build --target PluginNativeEditorTest -j4` | 0 | build-6-proof-unfixed.log |
| Proof: test unfixed (must FAIL) | `cd build/tests && ctest -R '^PluginNativeEditorTest$' -V` | **8** | test-6-proof-unfixed.log: FAIL at PluginNativeEditorTest.cpp(301) |
| Proof: re-apply fix | `git apply logs/fix.patch` | 0 | — |
| Proof: rebuild fixed | `CCACHE_TEMPDIR=... cmake --build build --target PluginNativeEditorTest -j4` | 0 | build-7-proof-fixed.log |
| Proof: test fixed (must PASS) | `cd build/tests && ctest -R '^PluginNativeEditorTest$' -V` | **0** | test-7-proof-fixed.log: Totals 10 passed, 0 failed |

Affected family: `ctest -R '^PluginNativeEditorTest$'` ran 10 test functions (10 passed with the fix, 9 passed + 1 failed without it). `CommandFormTest` (the other `-R` hit for `CommandRun|CommandForm`) was not rebuilt or run: its source is untouched, and `runCommand` itself is unchanged.

Gates (run by hand, because `feature_check.py` could not run — see below):

| Gate | Command | EXIT | Log |
|---|---|---|---|
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 | gate-fork-sources.log |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 | gate-all-sources-reproduce.log |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 | gate-no-upstream-regression.log |
| check-namespace | `tests/scripted/check-namespace` | 0 | gate-check-namespace.log |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 | gate-no-tautology.log |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 | gate-doc-figures.log |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 | gate-complexity.log |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 | gate-file-length.log |

The two ratchet gates are red on the train on purpose. Their REGRESSION lines (5 complexity, 15 file-length) all name other files: `XyPadPairTest.cpp`, `DawProjectReadTracks.cpp`, `ControlCommandsMixerTools.cpp`, `ControlCommandsMixerStrip.cpp`, `ControlCommandsShortcuts.cpp`, `TakeLane.cpp` and 14 test files. None names `src/gui/PluginNativeEditor.cpp` or `tests/src/gui/PluginNativeEditorTest.cpp`. I did not compute the base listing, so this is "no listed item is in my files", which is stronger than "no new item".

Wrapper: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` — **EXIT 1, did not run** (`gates-1.log`): it writes its logs to `<worktree>/../<name>-feature-check/`, i.e. `projects/zene-studio-program/zene-qwen-new-p17-126-feature-check`, which is outside this run's write-jail, so it raised `PermissionError` at `mkdir`. I did not edit the gate script. The six gates above are the same commands it runs, executed with their logs in the LOG FOLDER.

No new files were added, so no registration in `tests/fork-sources.txt` or `tests/upstream-modifications.txt` was needed (both edited files already exist in the tree; `fork-sources` and `no-upstream-regression` pass).

## Self-check

- Each new test FAILS with the product change reverted, and PASSES with it restored: yes, test-6 (EXIT 8, line 301) and test-7 (EXIT 0, 10 passed). The reverted state is the unpatched `PluginNativeEditor.cpp`; the test file was not reverted.
- Test is deterministic: yes. `EngineNotReady` makes `invoke` return `Busy` without any timer; the Bypass poll timer (500 ms) is only read, not waited on. The checks use `isVisible`/`isChecked` after synchronous `click()`s.
- Acceptance / finding criteria: the header shows the refusal (test lines 301-314, `reason->isVisible()` and non-empty text for Store A and Bypass); Bypass still reverts (`!bypass->isChecked()`, `effect->isEnabled()`, lines 312-313); the reason clears on the next success (lines 317-318). Each proven by test-7.
- Single undo step / reversibility: unchanged; no command was added or changed.
- Realtime: GUI code only (`src/gui/`); no audio-thread path touched.
- Old projects and data: no save, load or model code touched; the header only adds a hidden label.
- Qt portability: `QLabel`, `QVBoxLayout`, `QHBoxLayout` are already used in this file; no Qt-6-only API added.
- Sibling commands: no command file changed, so the snapshot and sibling-command steps do not apply.
- Gates: six pass; ratchets red for other files only; the wrapper itself could not run (see above).
- Working tree: clean after the commit below (report, fix and test committed together).
