Verdict: FALSE-POSITIVE

# Cluster new-p21-60: chainSection dereferences target.chain without a null check

Finding: `src/core/ControlCommandsRouting.cpp:149` (`const EffectChain& chain = *target.chain;`), "when: a target resolves with a null chain". Verdict: no target that `resolveControlTarget` reports as a success can carry a null chain on this branch, so the crash state the finding describes is not reachable. No product change is made.

## The contract

- `include/ControlDeviceSupport.h:119` - `EffectChain* chain = nullptr;    //!< never null on success` (the struct `ControlTarget`, declared at :114).
- `chainSection()` (`src/core/ControlCommandsRouting.cpp:147-156`) is file-local and has one caller, `handleRoutingGetState` at `src/core/ControlCommandsRouting.cpp:184`. That caller runs only after `resolveControlTarget` returned true (`:178`, `if (!resolveControlTarget(...)) { return error; }`).

## Every success path of resolveControlTarget sets a non-null chain

- Track, instrument: `src/core/ControlDeviceSupport.cpp:101-102` - `target->chain = instrumentTrack->audioBusHandle()->effects();`. The bus is built with `hasEffectChain = true` at `src/tracks/InstrumentTrack.cpp:66`, and `AudioBusHandle` constructs `m_effects` as `new EffectChain(nullptr)` when that flag is true (`src/core/AudioBusHandle.cpp:83`). Nothing in `AudioBusHandle.cpp` resets `m_effects` (its only uses are reads at :109, :160, :162, :218).
- Track, sample: `src/core/ControlDeviceSupport.cpp:106-107` - the same pattern, with `hasEffectChain = true` at `src/tracks/SampleTrack.cpp:61`.
- Track of any other type: `src/core/ControlDeviceSupport.cpp:109-113` returns false (`Refused`) and sets no chain.
- Mixer channel: `src/core/ControlDeviceSupport.cpp:157` - `target->chain = &channel->m_fxChain;`, the address of a member, never null. Earlier failures (`:145`, `:152`) return false.
- Post-insert target `ch-<n>/post`: `src/core/ControlDeviceSupport.cpp:220-224` - it first resolves the channel (so the chain is non-null), then either leaves `target->chain` as the channel's fx chain or replaces it with `&channel->m_postInserts.chain()`. Both are non-null.
- Unknown id prefix: `src/core/ControlDeviceSupport.cpp:236-239` returns false.

So on success the chain is always non-null, which is what the header states.

## Sibling precedent

The other readers of `target.chain` after a successful resolve dereference it the same way. For example `src/core/ControlCommandsDsp.cpp:53` (`target.chain->effects()`) has no null check. The one guard I found is `src/core/ControlAutomationSupport.cpp:169` (`if (target.chain == nullptr) { return out; }`). It is a defensive check, not evidence that the state is reachable.

## Why no regression test and no fix

- The procedure says a not-real finding skips the fix and the RED/GREEN steps. The finding's failure needs a `ControlTarget` with a null chain, which production code cannot produce. Forcing that state in a test would need a new test seam in production code, and that is a behaviour change the finding does not ask for.
- Adding a null check that can never trigger would change nothing observable. I did not add one.
- Not verified at runtime: I did not run `routing.get_state` against a live instance. The verdict rests on the code paths and the header contract above.

## Commands run and exit codes

1. `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-60` - `EXIT=1`. It stopped at `logs.mkdir` with `PermissionError` on `.../zene-studio-program/zene-qwen-new-p21-60-feature-check`, a directory outside the write-jail. Log: `logs/gate-1.log`. The gate itself did not run.
2. The same gate logic, run from a copy at `runs/new-p21-60.r1/gate-runner/feature_check_logdir.py`. Only two lines differ from the original: `ROOT` points at the original qwen folder, and the per-gate log folder points at `runs/new-p21-60.r1/logs/gate-out`. Command: `python3 -I runs/new-p21-60.r1/gate-runner/feature_check_logdir.py <worktree>` - `EXIT=0`, `RESULT: PASS` (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures all PASS; complexity and file-length "no new item in your files"). Log: `logs/gate-2.log`.
3. Re-run after the report was committed: `python3 -I runs/new-p21-60.r1/gate-runner/feature_check_logdir.py <worktree>` - `EXIT=0`, `RESULT: PASS` (1 changed file). Log: `logs/gate-3.log`.
4. `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p21-60` - `EXIT=1`; the only FAIL is the gate row, for the write-jail reason in the Self-check. Log: `logs/submit-1.log`.
5. Not run: any build or ctest. No product file changed, so there is nothing to rebuild.

## Self-check

1. "The regression test FAILS with the product change reverted and PASSES with it (exit codes)": not applicable. There is no product change and no test was added. This is a claim-only closure; the evidence is the cited code and contract above.
2. "The test is deterministic": not applicable, no test added.
3. "The fix is at the cause and complete": not applicable, nothing was changed. The sibling readers of `target.chain` were checked (see "Sibling precedent").
4. "No collateral damage": nothing changed, so no saved data, project load or caller is affected.
5. "If your verdict is FALSE-POSITIVE: the code (file:line) or the intended contract": `include/ControlDeviceSupport.h:119` (the contract), `src/core/ControlDeviceSupport.cpp:101-107, 157, 220-224` (every success path), `src/core/AudioBusHandle.cpp:83`, `src/tracks/InstrumentTrack.cpp:66`, `src/tracks/SampleTrack.cpp:61` (the track bus always has a chain).

Completion check (`submit_check.py`, log `logs/submit-1.log`, EXIT=1): commits, clean tree, report verdict, claim-only test row and the "Self-check" section all PASS. The "gates" row FAILs for an environment reason, not a gate result. `submit_check.py` runs the original `feature_check.py`, and that script's `mkdir` of `<worktree>-feature-check` is refused by the write-jail. The same gate logic run from the copy (`logs/gate-3.log`) gives EXIT=0 and `RESULT: PASS` with 1 changed file. The original script was not edited.
