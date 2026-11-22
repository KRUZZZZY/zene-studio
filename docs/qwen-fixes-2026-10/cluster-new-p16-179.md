FIXED

# new-p16-179: an unreadable rack selector no longer loads as chain 0

Branch `bugs/qwen-new-p16-179`. Product change in `src/core/Rack.cpp`, regression tests in `tests/src/core/RackTest.cpp`.

## Evidence (real defect, pre-fix code)

- Pre-fix `src/core/Rack.cpp:419` read the selector with `toInt()` and no success flag:
  `m_selectedChain = rack.attribute("selected", QString::number(Parallel)).toInt();`
  Now the same read is at `src/core/Rack.cpp:426-428`.
- `QString::toInt()` returns 0 for a non-numeric string, an empty string, `"1.0"`, and any integer that does not fit an `int` (e.g. `4294967296`). 0 is the channel's own base chain.
- `Rack::chain(0)` is the base chain, and `chainsToRoute` accepts it (`src/core/Rack.cpp:76-93`). So a corrupt selector silently routed the signal to chain 0.
- The documented contract is that a selection which is not a chain wires nothing: `docs/RACKS.md:142-144` and `include/Rack.h:137-142`.
- Tree precedents for an integer read that checks success: `src/core/Mixer.cpp:129` (comment on `toInt()` failing for numbers that do not fit), `src/core/Mixer.cpp:2968` (`toInt( &ok )`), `src/core/TempoMap.cpp:386` (`toInt(&tickOk)`).

## Fix

- `src/core/Rack.cpp:55`: `constexpr int kSelectionNamingNoChain = Rack::Parallel - 1;` (that is -2, since `Parallel` is -1 at `include/Rack.h:111`).
- `src/core/Rack.cpp:426-428`: `toInt(&selectedOk)`. If the attribute is absent, the default `QString::number(Parallel)` still gives Parallel, as before. If it parses, the value is kept unchanged. If it does not parse or overflows, the selection becomes -2, which names no chain and wires nothing.
- Chains, macros and zones still load in every case. Saving normalizes an unreadable selector to -2, and the reloaded file is un-wired.
- No header changed. No audio-path code changed.

## Why not the alternatives

- **Not Parallel:** Parallel routes every chain (`chainsToRoute`), which is an audible change nobody asked for.
- **Not the opaque `m_unsupportedSettings` path:** that drops the live chains, macros and zones along with the bad attribute. The sibling zone fix `b49dac66a7` keeps the rack and drops only the bad item; this fix follows that pattern.

## Readers of the -2 value (audited this session)

- Only caller of `Rack::loadSettings`: `src/core/Mixer.cpp:3009`. The real open path is `src/core/Song.cpp:2116` (`Engine::mixer()->restoreState`) through `Mixer::loadSettings`.
- `src/gui/shell/DevicePanel.cpp:171-179` (`addTargetChain`) clamps to 0 for any value outside 1..count-1.
- `src/core/MixerStrip.cpp:275-285` (`reductionDb`) matches no chain for a negative non-Parallel value.
- `src/core/ControlRackSupport.cpp:240` reports the raw value.
- `src/core/ControlCommandsRack.cpp:344-371` refuses writes to an index that is not Parallel and not a chain.
- `Rack::removeChain` (`src/core/Rack.cpp:192-213`) leaves a negative selection alone.
- `src/core/RackZones.cpp:54-59` (`readIntAttribute`) already used a checked `toInt(&converted)`.

## Test matrix (`tests/src/core/RackTest.cpp:671-880`)

| Case | Where | Expectation |
|---|---|---|
| not-a-number, empty, `1.0` | `aMalformedSelectorDoesNotBecomeChainZero` (678) | selectedChain != 0, un-wired |
| `4294967296`, `99999999999999999999999999` | `anOverflowingSelectorDoesNotBecomeChainZero` (688) | selectedChain != 0, un-wired |
| `9` and `-2` (existing invalid ints) | `aSelectorThatNamesNoChainLoadsAsBefore` (699-707) | unchanged: 9 and -2, un-wired |
| `1` and `-1` (normal) | same test (709-710) | routes {1} and {0,1} |
| absent | `aSelectorThatNamesNoChainLoadsAsBefore` (711) | default Parallel, routes {0,1}; the helper builds `<rack version="2">` with no attribute when given an empty QString |
| reconfiguration: corrupt load, then `setSelectedChain(1)`, then save | 845-877 | save writes `1` (877) |
| exhaustion | N/A | one int attribute, no bounded resource |
| audio thread | helper renders via `mixToChannel` (767) and `masterMix` (775) | load and rebuild are control-thread; no audio-path code changed |
| platforms | N/A | plain `QString`/`int`; no OS-specific API |

## Commands and exit codes

Commands ran from the worktree (ctest from build/tests); logs are named per step. Logs are under `~/.config/kads/zene/state/runs/new-p16-179.r1/logs/`.

- Patch match: `git apply -R --check logs/fix.patch` EXIT=0, which confirms the patch still matches the product hunk.
- The output text below is quoted from the raw logs. The step labels and exit codes (build-3 through build-6, test-4, test-5, test-8, APPLY_REVERSE, APPLY_RESTORE, and the RED/GREEN exits) come from the run record. The raw logs carry no EXIT line, so those codes were not re-verified in this session.
- Fix verified GREEN: build-3 EXIT 0, test-4 EXIT 0 (1/1), build-4 EXIT 0, test-5 EXIT 0 (5/5). Direct run test-4b: 13 passed, 0 failed.
- Reverse-apply of the fix to reproduce the defect: `APPLY_REVERSE` EXIT 0, build-5 EXIT 0.
- RED: test-6 ctest, "0% tests passed, 1 tests failed out of 1", five FAIL lines (three malformed, two overflow). Exit 8.
- RED: test-7 direct run, "Totals: 11 passed, 2 failed", both overflow FAIL lines and the malformed FAIL lines. Exit 2 (QTest's count of failed test functions, `aMalformedSelectorDoesNotBecomeChainZero` and `anOverflowingSelectorDoesNotBecomeChainZero`; the packet's expected 1 was wrong).
- Re-apply fix: `APPLY_RESTORE` EXIT 0, build-6 EXIT 0.
- GREEN: test-8 ctest, "100% tests passed out of 1", EXIT 0. test-9 direct, "Totals: 13 passed, 0 failed", EXIT 0.

Earlier notes that the >int64 value passes or is unverified are false: test-6 and test-7 show `99999999999999999999999999` failing on the pre-fix code.

## Gates

`feature_check` (log dir `logs/feature-check-1`, saved in `logs/gates-1.log`, exit `EXIT=0`):

| Gate | Result |
|---|---|
| fork-sources | PASS |
| all-sources-reproduce | PASS |
| no-upstream-regression | PASS |
| check-namespace | PASS |
| no-tautology | PASS |
| doc-figures | PASS |
| complexity | PASS (no new item in your files) |
| file-length | PASS (no new item in your files) |

RESULT: PASS. `lizard` was missing; the first install attempt failed on a read-only `~/.local/lib` (log `logs/lizard-install-1.log`). It was installed to `logs/pylib` (log `logs/lizard-install-2.log`, EXIT=0). The gate was run with `PYTHONPATH` pointing at that directory. The gate was not edited.

Completion check, run as `PYTHONPATH=/home/kruzzzzy/.config/kads/zene/state/runs/new-p16-179.r1/logs/pylib python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p16-179 new-p16-179`, log `/home/kruzzzzy/.config/kads/zene/state/runs/new-p16-179.r1/logs/submit-1.log`: `RESULT: PASS - submit`, `EXIT=0`.

## Files committed

- `src/core/Rack.cpp`
- `tests/src/core/RackTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p16-179.md`

Not touched: `tests/fork-sources.txt` (`src/core/Rack.cpp` is already registered) and `tests/upstream-modifications.txt`.

## Self-check

1. **RED/GREEN exit codes with the 4b commands.** Pre-fix RED: test-6 ctest exit 8, test-7 direct exit 2. Post-fix GREEN: test-8 ctest exit 0, test-9 direct exit 0 (13 passed). The 4b step (`APPLY_REVERSE` EXIT 0, build-5 EXIT 0) reproduces the defect before the re-apply.
2. **Deterministic.** Fixed XML strings with injected selector values; synchronous `Mixer::loadSettings` (`RackTest.cpp:816`, `:867`), then `mixToChannel` and `masterMix` (`:767`, `:775`). No sleeps, threads or timers were found in lines 671-880. The dummy audio device is stopped in `initTestCase` (`RackTest.cpp:273`, `Engine::audioEngine()->audioDev()->stopProcessing();`, read in this session).
3. **Cause and complete.** The defect is the single unchecked `toInt()` at the selector read. The caller (`Mixer.cpp:3009`) and every reader of `selectedChain()` were checked and tolerate -2. `RackZones` already uses a checked read. `RackMacros` still has the same class of unchecked read; it is listed under Other findings and is out of scope.
4. **No collateral.** Absent, -1, 1, 9 and -2 load as before (test lines 699-711). Only unreadable values change. No new limit is introduced. Valid data round-trips. A -2 selection reloads un-wired in this build and in older builds.
5. **Verdict: FIXED.** Evidence is the pre-fix code (`Rack.cpp:419`) and the RED logs (test-6, test-7), with the GREEN logs (test-8, test-9) after the fix.
6. **Sibling commands.** Not affected: the product commit changes no command files (`git show --stat c9a7beb048` lists only `src/core/Rack.cpp`, `tests/src/core/RackTest.cpp` and this report). The listed `app.*` and `arrangement.*` commands appear in the branch-wide base..HEAD diff from earlier commits, not in this cluster's commit c9a7beb048, and this fix adds or changes no command file, so none of them is affected.

## Other findings (not fixed, out of scope)

1. `src/core/RackMacros.cpp:446-449` reads the macro target `chain` and `effect` with an unchecked `toInt()`, and `src/core/RackMacros.cpp:157-169` (`isValidMacroTarget`) does not compare them with the chain and effect counts. A malformed macro target can still load as 0. Same defect class; not fixed here.
2. `src/gui/RackMenu.cpp:263-271` (`rackStatusText`) labels any negative selection as "parallel". A corrupt-load -2 is un-wired, but the menu says parallel. This was already true for any invalid negative value; it is a GUI issue and was not changed.
