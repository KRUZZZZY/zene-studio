FALSE-POSITIVE

# Cluster new-p21-118: CrossoverEQEffect scratch buffers sized once at construction

The finding says `m_tmp1`, `m_tmp2` and `m_work` (plugins/CrossoverEQ/CrossoverEQ.cpp:67-69) are sized with
`framesPerPeriod()` at construction. It says `processImpl` (CrossoverEQ.cpp:137-195) can overrun them if
the engine's period size changes later, or if `process` gets more frames than one period.

Neither trigger can happen in this codebase. No change is made to product code.

## Evidence

1. **The period size cannot change while effects exist.**
   - `m_framesPerPeriod` is written in exactly one place: the AudioEngine constructor's initialiser list,
     `src/core/AudioEngine.cpp:491`. It is set to `std::min(m_framesPerAudioBuffer, DEFAULT_BUFFER_SIZE)`.
   - `m_framesPerAudioBuffer` is read from the config once, in the same initialiser
     (`src/core/AudioEngine.cpp:487-490`).
   - The member is declared at `include/AudioEngine.h:868`, and the only accessor is the const getter
     `framesPerPeriod()` (`include/AudioEngine.h:539`). There is no setter.
   - A fff search for `m_framesPerPeriod` in `*.{cpp,h}` finds only these two files. Every other hit in
     AudioEngine.cpp is a read (lines 546-549, 737, 855, 919-920, 1127).
   - The engine itself depends on the same invariant: `BufferManager::init(m_framesPerPeriod)`, the
     output buffers and the hardware-out buffer are all allocated once, at `src/core/AudioEngine.cpp:546-549`.
   - `sampleRateChanged()` (CrossoverEQ.cpp:79-89) only changes filter coefficients. A sample-rate change
     does not touch `m_framesPerPeriod`.

2. **Every product caller passes at most one engine period.** These are all the non-test callers of
   `processAudioBuffer(` found by fff in `*.cpp`:
   - Mixer insert chains: `src/core/Mixer.cpp:785-799` and `src/core/MixerPostInserts.cpp:67` use the
     channel bus. `src/core/MixerSurroundMix.cpp:317-329` uses `channel.m_buffer` with the period's `frames`.
   - Racks: `src/core/RackNodes.cpp:125` uses the rack bus, rendered per mixer period.
   - `src/core/AudioBusHandle.cpp:188` uses the handle's period bus.
   - Clip effects: `src/core/SampleClipEffects.cpp:95-96` receives `buffer` from
     `src/core/SamplePlayHandle.cpp:660`. That is the play-handle buffer from `BufferManager::acquire()`
     (`src/core/PlayHandle.cpp:41`), which `BufferManager::init(m_framesPerPeriod)` sizes
     (`src/core/AudioEngine.cpp:546`).
   - Offline clip processing: `src/core/ControlCommandsClipProcess.cpp:80-94` splits the clip into blocks of
     exactly `framesPerPeriod()`, padding the last block, as its comment says ("one engine period at a time").
   - `src/core/DeltaTap.cpp:57-75` passes the caller's bus or buffer straight through.
   - Effect entry points do not change the size: `src/core/Effect.cpp:276`, `:356` and the `AudioPlugin`
     bridge (`include/AudioPlugin.h:246-356`, which wraps the caller's buffer in place) hand the caller's
     frame count to `processImpl`.
   - No product code calls `processImpl` directly. The fff search for `->processImpl(` / `.processImpl(`
     outside tests finds only `include/AudioPlugin.h:184,283,340`, which are the router lambdas above.

3. **This is the intended contract, shared by sibling effects.** Inherited LMMS effects size their
   per-period scratch the same way: `plugins/Bitcrush/Bitcrush.cpp:66` and
   `plugins/MultitapEcho/MultitapEcho.cpp:65`. Reallocating in `processImpl` would break the audio-thread
   rule (no allocation). Chunking would only guard against a caller that does not exist.

## Commands and results
All logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p21-118.r1/logs/`.

- Gates (step 5) are **BLOCKED in this lane**.
  `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p21-118 > logs/gates-2.log 2>&1; echo EXIT=$?`
  gave `EXIT=1` (gates-1.log, run by a subagent, also EXIT=1). The script printed no PASS/FAIL table. It stops
  in `base_listings()` (feature_check.py:60), and the log's first line is:
  `bunny-guard: 'git -C /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/gate-baselines/tree checkout -q --detach 9913d36d5397025d1b7092097f50f6d50c652dc4' is not allowed in a Bunny lane (AGENTS.md rules 1, 2, 11, 12).`
  The checkout targets a shared tree outside this worktree, so I did not work around it.
  The gate scripts feature_check.py wraps were run one by one, from the worktree, with the report file present:
  - `bash tests/fork-sources-gate.sh > logs/gate-fork-sources.log 2>&1; echo EXIT=$?`: EXIT=0 ("PASS: every tracked source in scope is registered")
  - `bash tests/all-sources-reproduce.sh > logs/gate-all-sources-reproduce.log 2>&1; echo EXIT=$?`: EXIT=0 ("REPRODUCES")
  - `bash tests/no-upstream-regression-gate.sh > logs/gate-no-upstream-regression.log 2>&1; echo EXIT=$?`: EXIT=0 ("PASS: every change to upstream-inherited code ... is declared")
  - `tests/scripted/check-namespace > logs/gate-check-namespace.log 2>&1; echo EXIT=$?`: EXIT=0 ("0 errors.")
  - `bash tests/no-tautology-gate.sh > logs/gate-no-tautology.log 2>&1; echo EXIT=$?`: EXIT=0 ("PASS")
  - `python3 tests/doc-figures-gate.py > logs/gate-doc-figures.log 2>&1; echo EXIT=$?`: EXIT=0 ("RESULT: PASS")
  - `bash tests/file-length-gate.sh --check > logs/gate-file-length.log 2>&1; echo EXIT=$?`: EXIT=0 ("PASS (check mode: no regressions)")
  - `bash tests/complexity-gate.sh --check > logs/gate-complexity.log 2>&1; echo EXIT=$?`: EXIT=2. The whole log
    is "complexity-gate: lizard is not installed". This run checked nothing. The diff is a single Markdown
    file, so the complexity ratchet has no function in a changed file to measure.
- Completion check: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p21-118 > logs/submit-1.log 2>&1; echo EXIT=$?`
  gave EXIT=1 before the commit (no commit yet, untracked report, gates). Only the gates row can stay FAIL
  after the commit, for the blocked reason above.

No build or test was run: there is no product change and no regression test. A FALSE-POSITIVE verdict
needs no test (definition of done, item 1).

## Self-check
- *Regression test fails without the fix and passes with it.* Not applicable: the verdict is
  FALSE-POSITIVE. No product file changed, so there is nothing to revert. No test was added, because
  any test would have to call the effect outside the engine's one-period contract (Evidence 1-2).
- *Test is deterministic.* Not applicable: no test was added.
- *Fix is at the cause and complete; callers and siblings checked.* No fix. All non-test callers of
  `processAudioBuffer(` were checked and listed in Evidence 2, plus the direct `processImpl` call sites.
  Siblings with the same scratch pattern (Bitcrush.cpp:66, MultitapEcho.cpp:65) were left unchanged, in
  line with that contract.
- *No collateral damage.* Only this report is added. No product code, saved data or limits change.
- *FALSE-POSITIVE evidence.* `src/core/AudioEngine.cpp:491` is the only write to `m_framesPerPeriod`;
  `include/AudioEngine.h:539` is the const getter, with no setter. `src/core/ControlCommandsClipProcess.cpp:80-94`
  shows the one offline effect render splitting into exactly one period per call.
  `src/core/PlayHandle.cpp:41` with `src/core/AudioEngine.cpp:546` show clip effects get period-sized
  buffers.

Delegation: Sonnet was asked to list every caller whose frame count could exceed `framesPerPeriod()`.
It returned nothing: its session had neither the fff tools nor shell search, so I ran the caller search
myself with fff. Haiku #1 ran feature_check.py, which the guard blocked (gates-1.log). Haiku #2 ran the
eight gate scripts one by one (gate-*.log); I checked every log tail. DeepSeek was not used because no
mechanical edit or build was needed.

## Other findings
- Subagents in this session had no fff tools, and shell search is blocked for them, so they cannot do
  repository-wide caller searches.
- Outside product code, some tests call effect chains with frame counts they choose themselves (for example
  `tests/src/core/GranularPitchShifterRealtimeTest.cpp:95`, `RenderFrames`). A future test that pushes a
  CrossoverEQ, Bitcrush or MultitapEcho instance through a block larger than `framesPerPeriod()` would break
  the engine contract above. That is a test-side constraint, not a product defect.
