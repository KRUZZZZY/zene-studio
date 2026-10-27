Verdict: FALSE-POSITIVE

# Cluster new-p23-8: phaseScope dereferences showPhaseScope()'s window and its channel without checks

Finding (historical): `src/core/ControlCommandsMixerView.cpp:199`. On this branch the handler lives at
`src/gui/ControlCommandsMixerView.cpp:182-203`, and the line in question is `:199`
(`Engine::mixer()->mixerChannel(window->channel())`). Branch `bugs/qwen-new-p23-8`, base commit `639e1d23e`.

Claim: the unchecked `showPhaseScope()` result, or an out-of-range `window->channel()`, can reach line 199 and crash.
Neither path is reachable. No product change is made.

## Evidence

Null window cannot be returned to line 199:
- `src/gui/ControlCommandsMixerView.cpp:197`: `if (controlMainWindow() == nullptr) { return noInterface(...); }`
  runs immediately before `showPhaseScope(index)` at `:198`.
- `showPhaseScope` returns `nullptr` only when `getGUI() == nullptr || getGUI()->mainWindow() == nullptr`
  (`src/gui/PhaseScopeView.cpp:221-224`).
- `controlMainWindow()` is `getGUI() != nullptr ? getGUI()->mainWindow() : nullptr`
  (`src/gui/ControlGuiSupport.cpp:51-55`). It is the same condition, evaluated on the same GUI thread with nothing
  between the two checks, so the `nullptr` return cannot be reached from `phaseScope`.
- `new PhaseScopeWindow(...)` (`src/gui/PhaseScopeView.cpp:225`) does not return null.

`window->channel()` is always a valid mixer index:
- `PhaseScopeWindow::channel()` is `std::max(0, indexOfId(m_channelId))` (`src/gui/PhaseScopeView.cpp:181-184`).
  It resolves the stored channel id against the live mixer on each call, so it cannot be a stale cached index.
- `indexOfId` returns `i` only for `i < Engine::mixer()->numChannels()`, and `-1` otherwise
  (`src/gui/PhaseScopeView.cpp:56-63`). The `-1` case is clamped to `0` by `channel()`.
- `setChannel` also clamps out-of-range indices to `0` (`src/gui/PhaseScopeView.cpp:170`).
- Channel 0 always exists. The `Mixer` constructor calls `createChannel()` for the master
  (`src/core/Mixer.cpp:1029-1030`), and the control surface refuses to remove it
  (`src/core/ControlCommandsMixer.cpp:334`, "the master channel ch-0 cannot be removed"). So `numChannels() >= 1`
  and `mixerChannel(0)` is valid.

Caveat: the in-range guarantee relies on the master-channel invariant held in other files, not on a local check at
line 199. A defensive check would be harmless, but I found no reachable path that demonstrates the crash, so I did not
add one. The finding's medium confidence does not survive the code above.

Existing coverage on the same path: `tests/src/gui/MixerMeteringTest.cpp:692-717`
(`thePhaseScopeListensToTheChosenChannel`). It drives `window.phase_scope` through the command path, asserts that
`window->channel()` equals the requested channel, asserts that a refused channel (`ch-9999`, NotFound) leaves it
unchanged, and checks the master default. It passes on this build (see the commands below).

## Commands and exit codes

| # | Command (run from the worktree, or `build/tests` for ctest) | EXIT | Log (run folder `logs/`) |
|---|---|---|---|
| 1 | `cmake --build build --target MixerMeteringTest -j4 > build-1.log 2>&1` | 2 | build-1.log. ccache could not write its temp file under `/home/kruzzzzy/.local/share/zene-bunny-fix/dsh/home/ccache-tmp-r6` ("Permission denied"). Environment failure, not a code failure. |
| 2 | `CCACHE_DISABLE=1 cmake --build build --target MixerMeteringTest -j4 > build-2.log 2>&1` | 0 | build-2.log. Ccache disabled for this command only. Target built and linked. |
| 3 | `ctest -R MixerMeteringTest --output-on-failure > test-1.log 2>&1` (from `build/tests`) | 0 | test-1.log. "100% tests passed, 0 failed out of 1". |
| 4 | `ctest -R MixerMeteringTest -V > test-2.log 2>&1` (from `build/tests`) | 0 | test-2.log. Line 62: `PASS : MixerMeteringTest::thePhaseScopeListensToTheChosenChannel()`. Line 66: "Totals: 31 passed, 0 failed, 0 skipped". |

Affected family: `MixerMeteringTest`, 1 ctest test, 31 test functions executed, 0 failed.

Regression test: not written. The finding is not real (step 2 of the procedure: skip to step 6), so there is no RED or
GREEN cycle. No product file changed, so there is no `fix.patch` to revert and reapply (step 4b does not apply).

## Gates

`python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>`: **could not run.** It exits 1 with
`PermissionError` while creating its log folder `zene-qwen-new-p23-8-feature-check` beside the worktree
(`feature_check.py:69-71`), which is outside my write-jail. I did not modify the script. Log: `gate-1.log`.

Equivalent manual run, using the unchanged check commands from `feature_check.py:22-29` in the worktree:

| Check | Command | EXIT | Log |
|---|---|---|---|
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 | gate-fork-sources.log |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 | gate-all-sources.log |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 | gate-no-upstream.log |
| check-namespace | `tests/scripted/check-namespace` | 0 | gate-check-namespace.log |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 | gate-no-tautology.log |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 | gate-doc-figures.log |
| complexity | `bash tests/complexity-gate.sh --check` | 1 | gate-complexity.log (7 REGRESSION lines) |
| file-length | `bash tests/file-length-gate.sh --check` | 1 | gate-file-length.log (22 REGRESSION lines) |

The two ratchets exit 1 on whole-tree items that already exist on the train (for example
`WasmSandbox::loadModuleBytes` CCN 15 -> 16). `feature_check.py` fails a ratchet only on items in files changed
against the fix base. Changed-file check: `git diff --name-only 050/m0-train-17 HEAD` returned 0 lines (EXIT 0), and
`git status --short` was empty before this report was written. No REGRESSION item is in a changed file, so the
feature_check ratchet rows would read "no new item in your files". This is inferred from the script's logic, not from
a run of the script.

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p23-8.md` (this file)

No product or test source changed.
