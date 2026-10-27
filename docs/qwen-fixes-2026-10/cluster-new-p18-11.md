Verdict: FALSE-POSITIVE

new-p18-11 (Luna BUG 3, "Mixer channel scans can dereference a null channel"): the null slot the finding needs cannot be created by any code path in this tree. The mixer's channel container holds only live `MixerChannel*` values, the audio thread dereferences every slot every period without a check, and the control-side scans sit on the same invariant. No product change is made; the regression step cannot be run in the form the procedure asks for (see "Why no RED/GREEN").

## Evidence

**The scans named in the finding** (`src/core/ControlDeviceSupport.cpp`):
- `resolvePostInsertsTarget` loops over `Engine::mixer()->mixerChannel(i)` and dereferences `channel->m_fxChain` at lines 223-224, with no null check. The finding's line 223 is this loop.
- `rackChainEffect` does the same at lines 287-288 (`channel->m_fxChain`) and then `channel->m_rack` / `channel->m_postInserts` at 289-297.
- The one null check the finding refers to is in `resolveChannelTarget` at lines 133-134 (`candidate != nullptr && candidate->id() == wanted`). It is defensive. It does not show that a null slot can exist.

**The container and its accessor** (`include/Mixer.h`):
- Storage: `std::vector<MixerChannel*> m_mixerChannels;` at line 786 (raw pointers, private; `rg friend include/Mixer.h` finds no friend declaration, so a test cannot reach it).
- Accessor: `MixerChannel * mixerChannel( int _ch ) { return m_mixerChannels[_ch]; }` at lines 634-637. It returns the slot as stored, with no check. Callers that get a null from it would have been given one by the container.

**Every write to the container** (`rg` output in `logs/003-evidence-null-writers.log`, EXIT=0):
- `src/core/Mixer.cpp:1075` `m_mixerChannels.push_back( new MixerChannel( index, this ) );` is the only insertion. `new` throws on failure and does not return null, so the slot is never null.
- `src/core/Mixer.cpp:1441` `m_mixerChannels.erase(m_mixerChannels.begin() + index);` removes the slot and then deletes the channel. No null is left behind.
- `src/core/Mixer.cpp:1055` `m_mixerChannels.pop_back();` runs in the destructor.
- No line stores `nullptr` into the vector. The only `nullptr` hits on the container (`Mixer.cpp:2497`, `2508`) are arguments to `postinserts::faderBuffer` / `faderValue`, not stored values.

**The audio thread already assumes no null slot** (`src/core/Mixer.cpp`):
- `Mixer::masterMix` starts at line 2393. Its per-period loop at lines 2522-2526 runs `for (int i = 0; i < numChannels(); ++i)` and calls `m_mixerChannels[i]->m_bus.silenceAllChannels()`, `->m_sidechainBuffer...`, `->m_postFaderBuffer...`, `->m_surround...`, `->reset()`, with no null check. A null slot would crash the render path on the first period, before any control command could reach `resolvePostInsertsTarget` or `rackChainEffect`.
- Solo and mute handling also index the container without checks, e.g. `src/core/Mixer.cpp:1086` (`m_mixerChannels[m_lastSoloed]->m_soloModel.value()`) and `1088-1089`.

So the scans at `ControlDeviceSupport.cpp:223` and `:287` are consistent with the rest of the mixer: they assume the invariant that the render path, the solo logic and the channel lifecycle all rely on.

**Consequence for the proposed fix:** "skip null pointers in both scans" would add defensive code for a state that cannot occur. It would leave the mixer's own unguarded loops crashing on the same state, so it would not make a real null slot survivable. It would also change behaviour only in an unreachable branch.

## Why no RED/GREEN

- A regression test that fails without the fix needs a null slot in `m_mixerChannels`. The vector is private, there is no friend or test seam, and the only insertion (`Mixer.cpp:1075`) cannot produce null. Adding a seam to production code only to create an unreachable state would be a larger change than the finding justifies.
- So no product file was edited and no test was added. Step 4b (revert/reapply) is not applicable. Nothing was built or run for a RED/GREEN pair.

## Commands and results

| Evidence | Command | Result |
| --- | --- | --- |
| Feature gates, first attempt | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p18-11` (log `logs/001-feature-check-before-report.log`) | EXIT=1. `feature_check.py` writes its logs to `<worktree>-feature-check/`, a sibling directory outside the write-jail; it failed with `PermissionError` at `logs.mkdir` before running any gate. Not a gate result. |
| Feature gates, same gate functions with logs in the LOG FOLDER | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-11.r1/fc_driver.py <worktree>` (driver imports `feature_check` and runs its `SIMPLE` and `RATCHET` lists and the same `base_listings` / `key` / `item_file` logic; logs in `logs/feature-check/`; output `logs/002-feature-check-driver.log`) | EXIT=0, `RESULT: PASS`: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures all PASS; complexity and file-length "no new item in your files" |
| Null-write audit | `rg -n "m_mixerChannels\.(push_back\|erase\|pop_back\|insert\|emplace\|resize\|assign)\|m_mixerChannels\s*=" src/core/Mixer.cpp include/Mixer.h` plus accessor, friend, loop and nullptr searches (log `logs/003-evidence-null-writers.log`) | EXIT=0 |
| Completion check | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p18-11` | see "Self-check" |

## Self-check

- **Regression FAILS without and PASSES with the product change:** not applicable. The verdict is FALSE-POSITIVE, so there is no product change. The test that would prove the change cannot be written without a seam into the private `m_mixerChannels` vector, which no production path can fill with null (evidence above, `Mixer.h:786`, `Mixer.cpp:1075/1441/1055`).
- **Deterministic test:** not applicable (no test added).
- **Fix at the cause and complete:** not applicable (no fix). The sibling scans and the callers were checked (`ControlDeviceSupport.cpp:133-134, 223-224, 287-288`; `Mixer.cpp:1086`, `2522-2526`): all depend on the same non-null invariant, so a partial null check in two places would be inconsistent with the rest of the mixer.
- **No collateral damage:** no product file changed; saved projects and the audio path are untouched.
- **FALSE-POSITIVE evidence:** `src/core/ControlDeviceSupport.cpp:223-224` and `:287-288` (the unchecked dereferences in the finding); `include/Mixer.h:634-637, 786` (accessor and raw-pointer vector); `src/core/Mixer.cpp:1075` (`new`, the only insertion), `:1441` and `:1055` (erase and pop, no null left); `src/core/Mixer.cpp:2522-2526` (`masterMix` dereferences every slot per period, so a null slot would already crash the audio thread).
- **Gates:** `feature_check.py` itself exits 1 at the `logs.mkdir` step, because it writes logs into the sibling `zene-qwen-new-p18-11-feature-check/`, which the write-jail forbids (`logs/001`). It is an environment limit, not a gate result. The same gate functions run with logs in the LOG FOLDER (`fc_driver.py`, `logs/002`) give EXIT=0 and `RESULT: PASS` (the complexity and file-length rows report no new item in changed files). `submit_check.py` still shows `gates (feature_check.py)` as FAIL for that reason (`logs/004`).
