# Cluster new-p9-549

Verdict: **NEEDS-CONTEXT**

The finding is real, but the fix is a design decision (pool size, overflow policy, and interpolation semantics), so no product code was changed. Nothing in this cluster is committed as a fix.

## Finding and evidence

`AudioFileProcessor::playNoteImpl()` allocates playback state on the render path the first time a note is rendered.

| Claim | Evidence (file:line) |
| --- | --- |
| The allocation is on the render path | `plugins/AudioFileProcessor/AudioFileProcessor.cpp:146` `nph->m_pluginData = new Sample::PlaybackState(interpolationMode);`. `Instrument::playNote()` is the span entry point "used by the audio engine" and calls `playNoteImpl()` (`include/Instrument.h:106-110`). |
| The state owns a libsamplerate converter, so its construction allocates | `include/Sample.h:50-53` constructs `m_resampler(interpolationMode)`; `src/core/AudioResampler.cpp:54-56` builds `m_state{src_new(...)}`. |
| The state is too large for the engine's in-handle storage | `include/Sample.h:72` `std::array<SampleFrame, DEFAULT_BUFFER_SIZE> m_buffer;` with `DEFAULT_BUFFER_SIZE = 256` (`include/AudioEngine.h:72`) and `sample_t = float` (`include/LmmsTypes.h:39`), so the buffer alone is at least 2048 bytes. `NotePlayHandle::PluginDataStorageSize = 512` (`include/NotePlayHandle.h:77`, storage at `:393`). The Monstro placement-new pattern (`plugins/Monstro/Monstro.cpp:1089-1092`) does not fit. Placement-new would also not remove the `src_new` heap allocation. |
| A second allocation happens on the first render, and it overrides the sampler's interpolation setting | `src/core/Sample.cpp:116-120`: `Sample::play()` calls `state->m_resampler.setMode(requestedMode)` whenever the state's mode differs from `requestedMode`. `requestedMode` comes from `ExportRenderSettings::srcQuality()` (`src/core/Sample.cpp:116-117`), which defaults to `SrcQuality::Linear` (`src/core/ExportRenderSettings.cpp:36-37`). `AudioResampler::setMode()` calls `src_new` again (`src/core/AudioResampler.cpp:111-122`). |

Consequence of the second row, from reading the code (not run): an AudioFileProcessor set to interpolation "None" (ZOH, `AudioFileProcessor.cpp:135-137`) or "Sinc" (`:141-143`) gets a Linear converter on its first render in live playback, and that converter comes from a fresh `src_new` on the audio path. The per-instrument choice is replaced by the global default. Whether that is intended is a product question.

## Prior project decisions

- `docs/m0-blocker-fixes-2026-10-08/B-realtime.md:49` lists the same pattern for SlicerT (item 4467): "Neither was taken without a design decision."
- `docs/m0-design/voice-pool.md:13-19` records a chosen design for SlicerT: a fixed per-instance pool of 128 `PlaybackState` slots, with silence and a counter on overflow. `plugins/SlicerT/SlicerT.h` does not implement it, so it is still open.
- Precedent: `docs/qwen-fixes-2026-10/cluster-new-p9-174.md` (FreeBoy) is FIXED with a fixed 1024-voice pool that goes silent on overflow, checked by an allocation counter in the render test.

## Options (owner decision needed)

1. **Per-instance fixed pool (FreeBoy/SlicerT style).** Create N `Sample::PlaybackState` slots on the control thread at construction. Claim and release them in `playNoteImpl` and `deleteNotePluginData` through a lock-free flag. Overflow means silence plus a counter. Open points:
   - N, and the memory cost per instance: about 2 KB of buffer per slot plus libsamplerate state, which is not measured yet.
   - The converter mode is fixed at `src_new` time, so the pool needs one set per interpolation mode, or the mode must be decided first (see 2). Otherwise `setMode` still allocates on the render path.
2. **Decide the interpolation semantics first.** If ZOH and Sinc should be honoured in live playback, `Sample::play` has to stop overriding them with the global default. That is a behaviour change in a shared path and was not made here.
3. **Engine-level pooled playback state** (like the sample-voice pool in `docs/m0-design/voice-pool.md`). Bigger, and it changes the engine, not just this plugin.

Recommendation: option 1, once the owner has chosen N and the overflow policy, and after option 2 is decided, since that fixes what each pooled state must hold.

## What was not done

- No product code was changed, so there is no RED or GREEN run and no `fix.patch` round-trip. A regression test would encode a contract (N, overflow silence) that has not been decided.
- No runtime allocation probe was run. The allocation claim rests on the code references above.

## Commands and results

Worktree: `zene-qwen-new-p9-549`, branch `bugs/qwen-new-p9-549`, HEAD `ded355440`. `git status --short` is empty before this report was written.

| Check | Command | EXIT | Log |
| --- | --- | --- | --- |
| Allocation and storage-size references | `rg -n ... > logs/evidence-1.log 2>&1; echo EXIT=$?` | 0 | `evidence-1.log` |
| Resampler and mode-change references | `rg -n ... > logs/evidence-2.log 2>&1; echo EXIT=$?` | 0 | `evidence-2.log` |
| Render call site and SRC default | `rg -n ... > logs/evidence-3.log 2>&1; echo EXIT=$?` | 0 | `evidence-3.log` |
| Complexity ratchet | `bash tests/complexity-gate.sh --check > logs/gate-1.log 2>&1; echo CCN_EXIT=$?` | 1 | `gate-1.log` |
| File-length ratchet | `bash tests/file-length-gate.sh --check > logs/gate-2.log 2>&1; echo LEN_EXIT=$?` | 1 | `gate-2.log` |

Both gates were run on the untouched branch, so these failures are not caused by this cluster. `gate-1.log` ends with `FAIL: complexity ratchet regressed` and reports 68 functions over CCN 10. `gate-2.log` ends with `FAIL: file-length ratchet regressed` and reports new files over 500 lines. Neither log mentions AudioFileProcessor. I did not re-anchor either gate. This report commits no product code, so it has no effect on the gates. Both failures need to be resolved on their own branch.

## Files in this commit

- `docs/qwen-fixes-2026-10/cluster-new-p9-549.md` (this report)
