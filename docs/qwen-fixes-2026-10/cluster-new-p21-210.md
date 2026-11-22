Verdict: FALSE-POSITIVE

new-p21-210: sfizz `Layer::delaySustainRelease` / `delaySostenutoRelease` never store anything

Evidence: the finding says the capacity guard skips the first push because the vectors start at capacity 0. That is true only for layers that Synth never reserves. Every layer that can reach the push is a `trigger=release` layer, and buildRegion reserves its vectors before any note event. The first `emplace_back` therefore succeeds.

1. The push is reachable only from the release-trigger branch. [Layer.cpp:114](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-210/plugins/SfzPlayer/sfizz/src/sfizz/Layer.cpp:114) opens `if (region.trigger == Trigger::release)`, and both `delaySustainRelease` calls sit inside it ([Layer.cpp:120](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-210/plugins/SfzPlayer/sfizz/src/sfizz/Layer.cpp:120), [Layer.cpp:125](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-210/plugins/SfzPlayer/sfizz/src/sfizz/Layer.cpp:125)); the block closes at [Layer.cpp:129](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-210/plugins/SfzPlayer/sfizz/src/sfizz/Layer.cpp:129). Those are the only callers of `delaySustainRelease` in the tree (`rg` over `plugins/SfzPlayer/sfizz`; the other hit is the sostenuto re-delay at [Synth.cpp:1448](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-210/plugins/SfzPlayer/sfizz/src/sfizz/Synth.cpp:1448), which runs on a layer that already has the stored entries).
2. The vectors are reserved for every release-trigger layer. [Synth.cpp:178](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-210/plugins/SfzPlayer/sfizz/src/sfizz/Synth.cpp:178) parses all opcodes first; then [Synth.cpp:228-233](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-210/plugins/SfzPlayer/sfizz/src/sfizz/Synth.cpp:228) runs `reserve(max(config::delayedReleaseVoices, keyLength))` on both vectors when `lastRegion->trigger == Trigger::release`. `delayedReleaseVoices` is 16 ([Config.h:166](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-210/plugins/SfzPlayer/sfizz/src/sfizz/Config.h:166)), so capacity is at least 16 before any note arrives. Nothing shrinks the vectors: `clear()` keeps capacity and `swapAndPopFirst` only pops.
3. Layers with no reserved capacity are the ones the guard is meant to keep empty. `storeSostenutoNotes` is called for any layer with a sostenuto CC ([Layer.cpp:143-144](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-210/plugins/SfzPlayer/sfizz/src/sfizz/Layer.cpp:143), [Layer.cpp:238-246](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-210/plugins/SfzPlayer/sfizz/src/sfizz/Layer.cpp:238)). For a non-release layer the capacity is 0, so nothing is stored. That matters: `ccDispatch` replays `delayedSostenutoReleases_` as NoteOff events for the layer ([Synth.cpp:1445-1454](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-210/plugins/SfzPlayer/sfizz/src/sfizz/Synth.cpp:1445), [Synth.cpp:1411-1425](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-210/plugins/SfzPlayer/sfizz/src/sfizz/Synth.cpp:1411)). Storing into an attack layer would start spurious note-offs there, so the zero-capacity gate is the intended behaviour.
4. The guard is the realtime allocation cap. Removing it, as the finding suggests, would let `emplace_back` allocate on the audio thread once a layer overflows its reserve. That breaks the rule in AGENTS.md (no allocation on audio paths), so the suggested fix would be a regression.

So the sustain and sostenuto stores do work on the release layers they are meant for, and the finding's "always false" claim does not hold for them.

Related issue, not fixed here (NEEDS-CONTEXT, from reading the code only; not run): entries are never de-duplicated. A re-pressed key played under the sustain pedal adds a new `delayedSustainReleases_` entry each time. Nothing removes the older entry on note-on (no reference to the vector outside [Layer.cpp:215-260](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-210/plugins/SfzPlayer/sfizz/src/sfizz/Layer.cpp:215) and [Synth.cpp:1398-1450](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-210/plugins/SfzPlayer/sfizz/src/sfizz/Synth.cpp:1398)). A single-key release region holds 16 entries, so the 17th note-off under the pedal is dropped silently. Fixing that means choosing between de-duplicating on note-on and raising the cap, and that is a design decision for the owner, so it is left alone.

No product code was changed, so there is no RED/GREEN regression test. The claim is closed on the code evidence above.

Commands run (logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-210.r1/logs/`):

| Step | Command | EXIT |
| --- | --- | --- |
| Gate script as specified | `python3 .../qwen/feature_check.py <worktree> > logs/001-feature-check-baseline.log 2>&1` | 1 - it crashed with `PermissionError` creating `<worktree>/../zene-qwen-new-p21-210-feature-check`, which is outside the write area. No gate ran. |
| Same checks, logs redirected | `python3 -I .../runs/new-p21-210.r1/gate_driver.py <worktree> logs/001-gates-baseline > logs/002-feature-check-baseline.log 2>&1` | 0 - all six simple gates exit 0; complexity and file-length: no new item in changed files; RESULT: PASS |

`gate_driver.py` imports the check list and pass rules from `feature_check.py` unchanged; only the log folder differs.

## Self-check

- **Regression test RED/GREEN:** not applicable. No product change was made, so there is nothing for a regression to guard. Reason: the premise does not hold on the current code (see Evidence 1-3).
- **Deterministic test:** not applicable (no test added).
- **Fix at the cause and complete:** not applicable (no fix). The callers of `delaySustainRelease` and `delaySostenutoRelease` and the reserve in `buildRegion` were checked (Evidence 1-3).
- **No collateral damage:** no product code changed, so behaviour outside this report is unchanged.
- **Audio-thread safety:** unchanged. Removing the guard as suggested would add allocation on the audio thread (Evidence 4).
- **Claim-only closure:** the verdict rests on cited code. It has not been run at runtime; an independent reviewer should confirm `Synth.cpp:228-233` and `Layer.cpp:114-129` by reading.
