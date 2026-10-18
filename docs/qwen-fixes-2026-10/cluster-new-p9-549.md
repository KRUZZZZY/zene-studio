# Cluster new-p9-549

Verdict: **NEEDS-CONTEXT**

The finding is real and still unfixed on this branch. Two owner decisions are open and not recorded anywhere in the repo: how AudioFileProcessor's interpolation choice should behave, and the pool capacity and overflow policy for this plugin. No product code was changed, so this commit is report-only.

Round 2 correction: the round 1 report said SlicerT's pool was unimplemented. That was wrong. SlicerT has a 128-slot pool on this branch, and the sections below are corrected to match. Luna's round 1 review was DISPUTE on that point.

## Finding and evidence

| Claim | Evidence (file:line) |
| --- | --- |
| The allocation is on the render path | `plugins/AudioFileProcessor/AudioFileProcessor.cpp:146` `nph->m_pluginData = new Sample::PlaybackState(interpolationMode);`. It runs from `Instrument::playNote()` (`include/Instrument.h:106-110`), which `NotePlayHandle::play()` reaches at `src/core/NotePlayHandle.cpp:307` (`m_instrumentTrack->playNote( this, buffer )`). |
| Constructing the state allocates a libsamplerate converter | `include/Sample.h:51` `m_resampler(interpolationMode)`; `src/core/AudioResampler.cpp:55` `m_state{src_new(...)}`. |
| The state is too large for the note's inline storage | `include/Sample.h:72` holds `std::array<SampleFrame, DEFAULT_BUFFER_SIZE>` with `DEFAULT_BUFFER_SIZE = 256` (`include/AudioEngine.h:72`). `SampleFrame` is two `sample_t` floats (`include/SampleFrame.h:40`, `include/LmmsTypes.h:39`), so the buffer alone is 2048 bytes. `NotePlayHandle::PluginDataStorageSize = 512` (`include/NotePlayHandle.h:77`). Monstro's placement-new (`plugins/Monstro/Monstro.cpp:1089-1092`) fits only because its `static_assert` keeps `MonstroSynth` under 512 bytes. Placement-new here would not avoid the `src_new` heap allocation. |
| A second allocation happens on the first render | `src/core/Sample.cpp:116-120`: `Sample::play()` calls `state->m_resampler.setMode(requestedMode)` when the state's mode differs. `requestedMode` comes from `ExportRenderSettings::srcQuality()`, which defaults to `SrcQuality::Linear` (`src/core/ExportRenderSettings.cpp:36-37`). `AudioResampler::setMode()` calls `src_new` again (`src/core/AudioResampler.cpp:111-122`). The comment at `src/core/Sample.cpp:107-115` says the same thing: the comparison is the render-path cost, and re-creation happens only for a non-default quality. |
| No pool exists for AudioFileProcessor | `plugins/AudioFileProcessor/AudioFileProcessor.h` and `.cpp` declare no pool. The only `PlaybackState` uses are `.cpp:146-148` (create) and `:188` (delete). |

Consequence, from reading the code (not run): the interpolation menu is "None" (ZOH), "Linear" (default) and "Sinc" (`AudioFileProcessor.cpp:96-99`, mode switch `:132-144`). A Linear instrument does not trigger the second conversion. A ZOH or Sinc instrument does: `Sample::play()` swaps in the global converter mode before the first frame is rendered, and that swap calls `src_new` on the render path. So in playback today, the per-instrument interpolation choice does not take effect.

## SlicerT pool (corrects round 1)

SlicerT implements the fixed per-instance pool that round 1 said was missing:

- `plugins/SlicerT/SlicerT.h:73-77` `PlaybackSlot` (a `Sample::PlaybackState` plus an atomic `inUse` flag). `:78` `acquirePlaybackSlot` declaration, `:79` `PlaybackSlotCount = 128`, `:80` `m_playbackSlots`, `:81` `m_noPlaybackSlotTag`.
- `plugins/SlicerT/SlicerT.cpp:79-92` `acquirePlaybackSlot()` claims a free slot with a compare-exchange scan. It allocates nothing.
- `plugins/SlicerT/SlicerT.cpp:132-142` claims a slot when a note starts. When all 128 are busy, the note is tagged, `Engine::audioEngine()->recordSlicerTStateOverflow()` is called (`:138`), and the note renders silence.
- `plugins/SlicerT/SlicerT.cpp:180-185` release resets the state and clears `inUse`. It frees nothing.
- The overflow counter is exposed as `slicert_state_overflow` (`src/core/ControlCommandsControl.cpp:72,92`) and read by `tests/src/core/SlicerTPlayheadTest.cpp:240-252`.
- The design this implements is `docs/m0-design/voice-pool.md:27`: 128 slots per instance, with silence and a counter on overflow.

Stale notes, not edited here (this commit touches only this report):

- `docs/m0-blocker-fixes-2026-10-08/B-realtime.md:15` (the BLOCKED row for 4467) and `:49` (the blocked narrative) describe SlicerT before its pool existed. Treat both as stale.
- Round 1 cited `docs/m0-design/voice-pool.md:13-19`. The SlicerT design is at `:27`.

Residual on SlicerT: its slots are built in Linear, so the default path never calls `setMode`. A non-default `srcQuality` would make `Sample::play()` call `setMode` → `src_new` on the first render of each slot. Whether that render runs on the realtime audio thread was not checked here.

## Why AudioFileProcessor still needs an owner decision

A search of `docs/` for "AudioFileProcessor" or "interpolation" found no recorded decision on this plugin's interpolation behaviour or pool capacity. The hits are descriptive, for example `docs/RACK-MACROS.md:131`. `plans/` and `AGENTS.md` are not present in this worktree, so they were not searched.

Two decisions are open:

1. **Interpolation semantics (blocks any pool design).** A converter's mode is fixed at `src_new` time, and `Sample::play()` overwrites it with the global quality. A pool whose slots take the instrument's mode still calls `setMode` on first render, so it still allocates on the render path. Options:
   - (a) Build slots in the global Linear mode. This is allocation-free and matches what playback already does. The ZOH and Sinc menu items stay inert, as they are today.
   - (b) Honour the instrument's mode. This changes the shared `Sample::play()` path and the audible output of ZOH and Sinc instruments, and each slot needs a converter per mode.
   Which one is wanted is a product decision. Recommendation: (a) for a behaviour-preserving fix, with the inert menu recorded as a known issue, unless the owner wants (b).

2. **Capacity and overflow (sets memory and audible behaviour).** No slot count is recorded for AudioFileProcessor. SlicerT's 128 (`docs/m0-design/voice-pool.md:27`) and FreeBoy's 1024 (`docs/qwen-fixes-2026-10/cluster-new-p9-174.md:9,13`) are choices for their own note models, not rules that transfer. The cost is concrete: 128 slots are 128 × 2048 B = 256 KiB of sample buffer per instrument, before the unmeasured libsamplerate state. Thirty instruments would hold about 7.5 MiB. The overflow policy is also open. Silence, as SlicerT and FreeBoy do, drops a note mid-phrase. Stealing a live slot instead needs a render-safe handoff, which is its own design.

Decide 1 first, since it fixes what each slot holds. Then decide 2. NEEDS-CONTEXT stays until both are decided.

## What was not done

- No product code was changed, so there is no RED or GREEN run, no `fix.patch` round-trip and no regression test. A test would encode a contract (slot count, overflow behaviour) that has not been decided.
- No runtime allocation probe was run. The allocation claims rest on the code references above.
- The stale docs listed above were not edited.

## Commands and results

Logs are in `LOG = /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-549.r2/logs`.

| Check | Command | EXIT | Log |
| --- | --- | --- | --- |
| SlicerT pool references | `rg -n "PlaybackSlotCount\|struct PlaybackSlot\|m_noPlaybackSlotTag\|acquirePlaybackSlot\|m_playbackSlots" plugins/SlicerT/SlicerT.h plugins/SlicerT/SlicerT.cpp > $LOG/evidence-r2-1.log 2>&1; echo EXIT=$?` | 0 | `evidence-r2-1.log` |
| AudioFileProcessor, Sample, resampler references | `rg -n "new Sample::PlaybackState\|m_pluginData = \|DEFAULT_BUFFER_SIZE = \|PluginDataStorageSize = \|requestedMode = \|void AudioResampler::setMode\|m_state\{src_new" plugins/AudioFileProcessor/AudioFileProcessor.cpp include/AudioEngine.h include/NotePlayHandle.h src/core/Sample.cpp src/core/AudioResampler.cpp > $LOG/evidence-r2-2.log 2>&1; echo EXIT=$?` | 0 | `evidence-r2-2.log` |
| Combined docs and plans search | `rg -n -i "audiofileprocessor\|interpolation" docs plans AGENTS.md > $LOG/evidence-r2-3.log 2>&1; echo EXIT=$?` | 2 | `evidence-r2-3.log` (`plans` and `AGENTS.md` do not exist in this worktree; superseded by the next row) |
| Docs-only search | `rg -n -i "audiofileprocessor\|interpolation" docs > $LOG/evidence-r2-4.log 2>&1; echo EXIT=$?` | 0 | `evidence-r2-4.log` (its hits include this report) |
| Complexity ratchet | `bash tests/complexity-gate.sh --check > $LOG/gate-r2-1.log 2>&1; echo CCN_EXIT=$?` | 1 | `gate-r2-1.log` (`:2` 68 functions over CCN 10; `:108` `FAIL: complexity ratchet regressed`) |
| File-length ratchet | `bash tests/file-length-gate.sh --check > $LOG/gate-r2-2.log 2>&1; echo LEN_EXIT=$?` | 1 | `gate-r2-2.log` (`:38` `FAIL: file-length ratchet regressed`) |

Both gates fail on this branch and were not re-anchored. The commit touches only a Markdown file, and both gates measure source files (`tests/all-sources.txt` for complexity; the C/C++ scope for file length), so this commit cannot change their result. Their failures are for the branch owner to resolve.

## Files in this commit

- `docs/qwen-fixes-2026-10/cluster-new-p9-549.md` (this report)
