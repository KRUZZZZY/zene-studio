Verdict: FALSE-POSITIVE

new-p21-87: FALSE-POSITIVE (finding "rackChainEffect dereferences a possibly null mixer channel", reported at `src/core/ControlDeviceSupport.cpp:288`, historical line 288, current line 288).

Evidence: the mixer channel vector never holds a null pointer, and every reader of it is bounded by its size.

- The only insertion is `createChannel()`, which pushes a freshly allocated channel: [Mixer.cpp:1075](src/core/Mixer.cpp:1075) `m_mixerChannels.push_back( new MixerChannel( index, this ) );`. No code assigns a null into `m_mixerChannels`. The only removal is the `erase` in `deleteChannel()` at [Mixer.cpp:1441](src/core/Mixer.cpp:1441), which removes the element and then deletes the channel at [Mixer.cpp:1442](src/core/Mixer.cpp:1442).
- `Mixer::numChannels()` returns `m_mixerChannels.size()` ([include/Mixer.h:769](include/Mixer.h:769)-[772](include/Mixer.h:772)). The loop in `rackChainEffect` is bounded by it, `for (int i = 0; i < Engine::mixer()->numChannels(); ++i)` at [ControlDeviceSupport.cpp:285](src/core/ControlDeviceSupport.cpp:285). So `mixerChannel(i)` is always called with `0 <= i < size()`, and every element in that range is a live `MixerChannel*`.
- The accessor has no null check ([include/Mixer.h:634](include/Mixer.h:634)-[637](include/Mixer.h:637)), so the protection has to come from the invariant above. It holds.
- The "stale" case in the finding does not dereference anything stale. `rackChainEffect` compares `&channel->m_fxChain` with the pointer `target.chain` ([ControlDeviceSupport.cpp:288](src/core/ControlDeviceSupport.cpp:288)); it does not read through `target.chain`. A channel removed since the target was resolved is no longer in the vector, so the loop never visits it.
- Caller path: `resolveControlEffect` reaches `rackChainEffect` at [ControlDeviceSupport.cpp:272](src/core/ControlDeviceSupport.cpp:272), after its own loop over the same bounded vector at [ControlDeviceSupport.cpp:265](src/core/ControlDeviceSupport.cpp:265).

Not checked: `Engine::mixer()` (declared [include/Engine.h:66](include/Engine.h:66)) is a static pointer that the loop dereferences without a check. The finding does not name it, and the sibling loops at [ControlDeviceSupport.cpp:131](src/core/ControlDeviceSupport.cpp:131) and [ControlDeviceSupport.cpp:221](src/core/ControlDeviceSupport.cpp:221) use the same pattern. I did not verify that the control dispatcher guarantees the mixer exists before this path runs. This is not a claim that the mixer is always present. It is a separate question, and the finding does not raise it.

No product file changed, so there is no RED/GREEN pair and no regression test. The definition of done asks for a test only for a fix. Nothing about this finding needs one, and writing a test for an invariant that already holds would only pin the current behaviour.

| Evidence | Command | Result |
| --- | --- | --- |
| Writers of the channel vector and accessor/count contract | `rg -n "m_mixerChannels" src/core/Mixer.cpp include/Mixer.h \| rg "push_back\|erase\|insert\|emplace\|= *(nullptr\|NULL\|0)\b\|resize\|assign"` plus `sed`/`rg -A3` on `mixerChannel`, `numChannels`, and `rackChainEffect` written to `logs/001-source-audit.log` | EXIT=0; output in `logs/001-source-audit.log` |
| Feature gate script (whole script) | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-87 > logs/002-feature-check-pre-report.log 2>&1` | EXIT=1, but it stopped before running any gate: `PermissionError` creating the sibling directory `zene-studio-program/zene-qwen-new-p21-87-feature-check` (outside my write area). Not a gate result. |
| Gate 1 fork-sources, run directly from the worktree | `bash tests/fork-sources-gate.sh > logs/gate-fork-sources.log 2>&1` | EXIT=0 |
| Gate 2 all-sources-reproduce | `bash tests/all-sources-reproduce.sh > logs/gate-all-sources-reproduce.log 2>&1` | EXIT=0 |
| Gate 3 no-upstream-regression | `bash tests/no-upstream-regression-gate.sh > logs/gate-no-upstream-regression.log 2>&1` | EXIT=0 |
| Gate 4 check-namespace | `tests/scripted/check-namespace > logs/gate-check-namespace.log 2>&1` | EXIT=0 |
| Gate 5 no-tautology | `bash tests/no-tautology-gate.sh > logs/gate-no-tautology.log 2>&1` | EXIT=0 |
| Gate 6 doc-figures | `python3 tests/doc-figures-gate.py > logs/gate-doc-figures.log 2>&1` | EXIT=0 |
| Ratchet complexity (raw exit) | `bash tests/complexity-gate.sh --check > logs/gate-complexity.log 2>&1` | EXIT=1 (red on the train, as the brief says). 7 `REGRESSION:` lines; none names a file in this branch. Changed files vs base: 0. |
| Ratchet file-length (raw exit) | `bash tests/file-length-gate.sh --check > logs/gate-file-length.log 2>&1` | EXIT=1 (red on the train). 22 `REGRESSION:` lines; none names a file in this branch. Changed files vs base: 0. |
| Regression tests | none run | 0 tests: no product change, so no test was needed for this verdict. |

The feature-check script could not run as a whole because it writes its logs into the directory next to the worktree. I ran the same eight commands it lists, from the worktree, with the same exit-code meaning. The two ratchet gates were run for their raw exit codes. Because this branch changes no file, the script's rule (a ratchet item fails only when it is new in a changed file) cannot be triggered. I did not change the script, the gate limits or the baselines.

Files committed for this verdict: `docs/qwen-fixes-2026-10/cluster-new-p21-87.md`. No product file changed.

## Self-check

- **Regression test fails with the product change reverted and passes with it (exit codes):** not applicable. The verdict is FALSE-POSITIVE, so there is no product change to revert and no test was added. No RED/GREEN exit codes exist for this cluster.
- **Test is deterministic (no wall-clock, sleeps or races):** not applicable; no test added.
- **Fix at the cause and complete (every caller and sibling checked):** not applicable; no fix made. Callers and siblings of the flagged loop were checked: `resolveControlEffect` ([ControlDeviceSupport.cpp:272](src/core/ControlDeviceSupport.cpp:272)) and the same-pattern loops at [ControlDeviceSupport.cpp:131](src/core/ControlDeviceSupport.cpp:131) and [ControlDeviceSupport.cpp:221](src/core/ControlDeviceSupport.cpp:221). All bound their index by `numChannels()`.
- **No collateral damage:** no code changed, so nothing in existing projects or saved data is affected.
- **Not-real verdict cites code or contract:** yes. The invariant "the channel vector holds only live channels and is read only for `i < size()`" is shown by [Mixer.cpp:1075](src/core/Mixer.cpp:1075), [Mixer.cpp:1441](src/core/Mixer.cpp:1441) and [include/Mixer.h:769](include/Mixer.h:769)-[772](include/Mixer.h:772); the stale-target case by [ControlDeviceSupport.cpp:288](src/core/ControlDeviceSupport.cpp:288), which compares addresses and does not dereference `target.chain`.
- **Gates:** six of eight pass when run directly (exit 0). Complexity and file-length exit 1 (red on the train, as the brief says); neither lists an item in a file this branch touches, because the branch touches no file. The script `feature_check.py` itself could not complete in this sandbox (see the table), so its own row is an environment block, not a gate result.
- **Open question for the reviewer:** whether `Engine::mixer()` can be null when this path runs. Not verified; see the "Not checked" note above.
