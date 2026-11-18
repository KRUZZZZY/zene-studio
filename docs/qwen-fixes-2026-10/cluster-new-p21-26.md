Verdict: FALSE-POSITIVE

new-p21-26: FALSE-POSITIVE (`src/core/ControlCommandsLowLatency.cpp:75-76`, `trackChain()`)

The finding says `trackChain()` dereferences `audioBusHandle()` without a null check, so a null bus crashes. Neither half of that can happen on this branch.

Evidence:

- **`audioBusHandle()` cannot return null.** Both accessors return the address of an embedded member object: `include/InstrumentTrack.h:264-267` returns `&m_audioBusHandle` and `include/SampleTrack.h:77-80` returns `&m_audioBusHandle`. The members are plain values, not pointers: `include/InstrumentTrack.h:509` and `include/SampleTrack.h:125`. The address of a live member is never null, so "audioBusHandle() is null" is not a reachable state.
- **`effects()` is null only for a bus built without an effect chain.** `include/AudioBusHandle.h:81` returns `m_effects.get()`, and `src/core/AudioBusHandle.cpp:83` sets `m_effects(hasEffectChain ? new EffectChain(nullptr) : nullptr)`.
- **Both track types always build the bus with a chain.** `src/tracks/InstrumentTrack.cpp:66` and `src/tracks/SampleTrack.cpp:61` pass `true` as `hasEffectChain`.
- **Nothing removes the chain afterwards.** A search of `src` and `include` for `m_effects.reset`, `m_effects.release` and `m_effects =` finds no matches (`logs/001-source-audit.log`, section 7). The `unique_ptr` is set once in the constructor and never reseated.
- **The `dynamic_cast` guards the type.** For a null track or a track of another type, `dynamic_cast` returns null and lines 75-76 are skipped. The branches only run on live `InstrumentTrack` and `SampleTrack` objects.
- **The unguarded call is the codebase's contract.** Other callers dereference the same chain with no null check: `src/tracks/InstrumentTrack.cpp:1180,1219,1250,1252`, `src/tracks/SampleTrack.cpp:330,342,347,349`, and the same `trackChain` helper at `src/gui/shell/DevicePanel.cpp:87-88`. Adding a null check only in `ControlCommandsLowLatency.cpp` would make this one site differ from the rest and would hide a broken invariant if it ever occurred. It would change no behaviour.

Because the verdict is not-real and no product file changes, no regression test is added. Procedure step 2 sends this to the report step with the evidence above. There is no RED/GREEN pair: a test that passes on unchanged code proves nothing here, and a test that fails would need a state the constructors never produce.

Commands and results:

| Evidence | Command | Result |
| --- | --- | --- |
| Source audit of the finding site, accessors, members, constructors, `effects()` storage and absence of resets | `rg -n` over `src/core/ControlCommandsLowLatency.cpp`, `include/InstrumentTrack.h`, `include/SampleTrack.h`, `include/AudioBusHandle.h`, `src/core/AudioBusHandle.cpp`, `src/tracks/InstrumentTrack.cpp`, `src/tracks/SampleTrack.cpp`, `src/gui/shell/DevicePanel.cpp` and `rg -n "m_effects\.(reset\|release)\|m_effects\s*=[^=]" src include` (log written with `{ ...; } > logs/001-source-audit.log 2>&1; echo EXIT=$?`) | EXIT=0; `logs/001-source-audit.log` |
| Feature gate, as specified | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-26 > logs/002-feature-check-pre-report.log 2>&1; echo EXIT=$?` | EXIT=1, not a gate result: `PermissionError` creating the sibling folder `zene-qwen-new-p21-26-feature-check` outside the write area, before any gate ran (`logs/002-feature-check-pre-report.log`) |
| Feature gate, relocated copy (see note below) | `python3 -I /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-26.r1/gate/feature_check_relocated.py <worktree> > logs/003-feature-check-pre-report.log 2>&1; echo EXIT=$?` | EXIT=0; fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures PASS (exit 0); complexity and file-length PASS (no new item); RESULT: PASS (`logs/003-feature-check-pre-report.log`) |

Note on the gate run: the only change in the relocated copy is two lines in the copy. `ROOT` is pinned to the original loop directory, and the full-output folder is `logs/feature-check` in the run folder instead of a sibling of the worktree. The checks, thresholds and baselines are unchanged, and the original script is untouched.

Gate and completion results after the report was written:

| Evidence | Command | Result |
| --- | --- | --- |
| Final feature gate, relocated copy, tree including this report | `python3 -I /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-26.r1/gate/feature_check_relocated.py <worktree> > logs/004-feature-check-final.log 2>&1; echo EXIT=$?` | EXIT=0; RESULT: PASS, all eight rows PASS |
| Completion check, first run, before the report had a Self-check section or a commit | `python3 -I .../gate/submit_check_relocated.py <worktree> new-p21-26 > logs/005-submit-check.log 2>&1; echo EXIT=$?` | EXIT=1; FAIL on commits, clean tree and Self-check; WARN on scope guard (import path missing in that copy); the gates row was then reported as FAIL because the copy invoked the original gate |
| Completion check, rerun with the copy's import path and gate call corrected | `python3 -I .../gate/submit_check_relocated.py <worktree> new-p21-26 > logs/006-submit-check-rerun.log 2>&1; echo EXIT=$?` | gates row PASS; the remaining FAILs are the commit, clean-tree and Self-check items addressed by this commit |

## Self-check

- **Regression test fails with the product change reverted and passes with it.** Not applicable. The verdict is FALSE-POSITIVE and no product file changed, so there is no fix to revert (step 4b does not apply) and no test was added. The claim is checked by the source audit in `logs/001-source-audit.log`: the accessor returns `&m_audioBusHandle` (`include/InstrumentTrack.h:264-267`, `include/SampleTrack.h:77-80`), the members are values (`include/InstrumentTrack.h:509`, `include/SampleTrack.h:125`), and both constructors pass `true` (`src/tracks/InstrumentTrack.cpp:66`, `src/tracks/SampleTrack.cpp:61`).
- **Deterministic.** No test was added, so there is no timing, sleep or race to check.
- **Fix at the cause and complete.** No fix. The callers that rely on the same invariant were checked: `src/tracks/InstrumentTrack.cpp:1180,1219,1250,1252`, `src/tracks/SampleTrack.cpp:330,342,347,349`, and `src/gui/shell/DevicePanel.cpp:87-88`. All of them dereference the chain without a null check, which is consistent with the constructor contract.
- **No collateral damage.** No product file changed, so saved projects, old data and other behaviour are untouched.
- **Verdict evidence.** The file:line citations for the FALSE-POSITIVE verdict are in the Evidence list above and in `logs/001-source-audit.log`.
- **Gates.** `feature_check.py` as specified exits 1 with `PermissionError` on the sibling folder outside the write area (`logs/002-feature-check-pre-report.log`). The relocated copy, which changes only the `ROOT` and log-folder lines, passes all rows with EXIT=0 (`logs/003`, `logs/004`).

Files committed for this verdict: `docs/qwen-fixes-2026-10/cluster-new-p21-26.md`. No product file changed.
