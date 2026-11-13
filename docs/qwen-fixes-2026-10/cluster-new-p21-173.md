FIXED
new-p21-173: ALREADY-FIXED
new-p14-112: FIXED

# Batch cluster-new-p21-173 (new-p21-173 + new-p14-112) in plugins/FreeBoy/FreeBoy.cpp

## new-p21-173: ALREADY-FIXED

Decision: ALREADY-FIXED. The finding described a `std::unique_ptr<char[]>` holding the decoded
`sampleShape` buffer that was handed to `m_graphModel.setSamples(...)` and freed at function exit.
No such code exists on this tree. `plugins/FreeBoy/FreeBoy.cpp:431` loads the shape with
`waveform::loadGraph(_this, QStringLiteral("sampleShape"), m_graphModel, QStringLiteral("FreeBoy"))`,
which is the Q050-486 shared loader (`src/core/WaveformData.cpp:123-130`): it decodes with
`waveform::decode` into owning `std::unique_ptr<float[]>` storage and calls
`graph.setSamples(decoded.samples.get(), decoded.count)` only when the decode is Ok.
`graphModel::setSamples(const float*, int)` copies (`src/gui/widgets/Graph.cpp:544-552`:
`std::copy(samples, samples + copied, m_samples.begin())`), so the decoded buffer is dead after the
call returns and nothing dangles or leaks. History: commit `47a7d9c809` (new-p10-158-port) owned the
raw `base64::decode` buffer in a `unique_ptr<char[]>` *because* `setSamples` copies; commit
`dfff5c0f1c` (Q050-486) then replaced that hand-rolled path with `waveform::loadGraph` for all eight
graph plug-ins including FreeBoy. No product change is made for this cluster.

Evidence (existing contract tests, run on the unmodified tree for this bug):
- `cmake --build . --target WaveformDataTest GraphWaveformLoadTest PluginPortsMigrationTest > logs/build-01-baseline.log 2>&1` (from `build`) — EXIT=0
- `ctest -R '^(WaveformDataTest|GraphWaveformLoadTest|FreeBoy|FreeBoyPoolReclamation|FreeBoySampleRateHandoff)$' --output-on-failure > logs/test-01-baseline.log 2>&1` (from `build/tests`) — EXIT=0, `100% tests passed out of 5`
- `ctest -R '^(WaveformDataTest|GraphWaveformLoadTest)$' --output-on-failure > logs/test-05-waveform.log 2>&1` (from `build/tests`, after the new-p14-112 change) — EXIT=0, `100% tests passed out of 2` (FreeBoy `sampleShape` round-trip and damage rows in `GraphWaveformLoadTest::freeBoyShape` pass)

No RED/GREEN inversion applies: there is no failing behaviour to reproduce; the safe-copy contract is
already held and unchanged.

## new-p14-112: FIXED

Decision: REAL. `FreeBoyInstrument::playNoteImpl` (`plugins/FreeBoy/FreeBoy.cpp:459-492` before the
fix) acquired a voice from the published pool only when `nph->m_pluginData` was null and then kept
that voice forever. The `sampleRateChanged` handler (`:329-334`) swaps `m_voicePool` to the pool for
the new output rate, but a sustained note whose handle survived the change kept rendering through its
`Voice` (and its APU configured by `FreeBoyVoicePool` at the old rate, `:82-88`) — wrong pitch/timing
until the note ended. The existing test `freeBoyOldSampleRatePoolsAreReclaimed` even asserted this
retention (`QCOMPARE(handle.m_pluginData, oldVoice)`).

Fix (`plugins/FreeBoy/FreeBoy.cpp:459-513` after): when a sustained note's voice belongs to a pool
other than the currently published one, the next render period acquires a voice from the current pool
first (lock-free bounded scan, no allocation) and only then releases the old voice. The power and
volume initial registers are written through a shared `initInitialRegisters` lambda for both the
fresh-voice and migrated-voice paths, so both stay consistent. If the new pool is exhausted the old
voice is kept for that period and migration is retried next period, mirroring the existing unassigned-
note retry. No allocation, free, lock, wait or log is added on the audio thread; pool creation and
reclamation stay off that thread via the existing registry/reaper.

Regression: `PluginPortsMigrationTest::freeBoySustainedNoteMigratesAfterSampleRateChange`
(`tests/src/plugins/PluginPortsMigrationTest.cpp`), registered as `FreeBoySustainedNoteMigration` in
`tests/CMakeLists.txt`. Deterministic, no sleeps: it plays one period, captures the voice, changes
the device rate twice (`+505`, `+606`) with `emit engine->sampleRateChanged()`, and requires the
sustained handle's voice to change on each change, to render non-zero audio, and to do so with zero
heap allocations/deallocations under `partc::RealtimeAllocationWatch`.
`freeBoyOldSampleRatePoolsAreReclaimed` was updated to expect migration
(`QVERIFY(handle.m_pluginData != oldVoice)`) instead of the old retained voice; its pool-count
expectations are unchanged and still hold because migration releases the old voice before the collector
runs.

Test matrix considered before the edit:
- normal path: sustained note migrates once and renders audible (covered by the new slot)
- reconfiguration path: a second rate change migrates again to the newest pool (covered)
- failure/exhaustion: new pool full keeps the old voice and retries next period (implemented; 1024-voice fill is already covered for fresh notes by `freeBoyExhaustedNoteRetriesWhenVoiceFrees`, not duplicated)
- audio-thread constraints: migration period is allocation/deallocation free (asserted in the new slot); no locks/logs added
- platform/build: Linux RelWithDebInfo `zene` + `freeboy` + harness rebuild (EXIT=0)

Evidence (LOG FOLDER `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-173.r1/logs`):
- RED (test added, product unfixed): `cmake --build . --target PluginPortsMigrationTest > logs/build-02-red.log 2>&1` (from `build`) — EXIT=0; `ctest -R '^FreeBoySustainedNoteMigration$' --output-on-failure > logs/test-02-red.log 2>&1` (from `build/tests`) — EXIT=8, `FAIL! ... freeBoySustainedNoteMigratesAfterSampleRateChange() 'handle.m_pluginData != oldVoice' returned FALSE`
- GREEN (fix applied): `cmake --build . --target freeboy PluginPortsMigrationTest > logs/build-03-green.log 2>&1` (from `build`) — EXIT=0; `ctest -R '^FreeBoySustainedNoteMigration$' --output-on-failure > logs/test-03-green.log 2>&1` (from `build/tests`) — EXIT=0, `100% tests passed out of 1`
- Family: `ctest -R '^FreeBoy' --output-on-failure > logs/test-04-family.log 2>&1` (from `build/tests`) — EXIT=0, `100% tests passed out of 4` (`FreeBoy`, `FreeBoyPoolReclamation`, `FreeBoySampleRateHandoff`, `FreeBoySustainedNoteMigration`); repeated as `test-08-family-final.log` after reapply — EXIT=0, 4/4
- Reverse proof (product files only): `git diff -- plugins/FreeBoy/FreeBoy.cpp > logs/fix.patch` — EXIT=0; `git apply -R logs/fix.patch` — EXIT=0; `cmake --build . --target freeboy PluginPortsMigrationTest > logs/build-06-reverse.log 2>&1` — EXIT=0; `ctest -R '^FreeBoySustainedNoteMigration$' --output-on-failure > logs/test-06-reverse.log 2>&1` — EXIT=8 (same `FAIL!` as RED); `git apply logs/fix.patch` — EXIT=0; rebuild `logs/build-07-reapply.log` — EXIT=0; `ctest -R '^FreeBoySustainedNoteMigration$' --output-on-failure > logs/test-07-reapply.log 2>&1` — EXIT=0
- No collateral damage: `ctest -R '^(WaveformDataTest|GraphWaveformLoadTest)$' --output-on-failure > logs/test-05-waveform.log 2>&1` — EXIT=0, 2/2; `cmake --build . --target zene > logs/build-08-zene.log 2>&1` (from `build`) — EXIT=0
- Gates: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-173 > logs/gate-01.log 2>&1` — EXIT=0, `RESULT: PASS` (all gates PASS, complexity/file-length report no new item)

Files committed for this batch:
`plugins/FreeBoy/FreeBoy.cpp`, `tests/CMakeLists.txt`, `tests/src/plugins/PluginPortsMigrationTest.cpp`,
`tests/upstream-modifications.txt`, `docs/qwen-fixes-2026-10/cluster-new-p21-173.md`.

## Self-check

- Regression proof: yes per code above. The new slot fails without the product fix (RED EXIT=8, reverse EXIT=8) and passes with it (GREEN EXIT=0). Family is 4/4 (`FreeBoy` focused ctests); waveform family is 2/2; `zene` rebuild EXIT=0.
- Determinism: yes; no sleeps, wall-clock or races. The sample rate is injected with `setSampleRateForTesting` + `emit sampleRateChanged`, the voice pointer is compared directly, and allocation-freedom uses `RealtimeAllocationWatch`. The reaper does not affect the pointer comparison because the old voice's pool cannot be reclaimed while the voice is active.
- Cause and siblings: construction, `sampleRateChanged`, destruction, fresh-note acquisition, sustained-note migration, note cleanup and pool reclamation were all checked. Conductor/undo/command surface untouched; no new control command added. Pool exhaustion keeps the old voice (no silenced note); multiple rate changes go straight to the newest pool.
- Collateral: saved-data and `loadSettings` behaviour unchanged (new-p21-173 needs no code change; waveform tests still pass); polyphony cap semantics unchanged (1024-voice scan still bounded, retry next period).
- Verdict check: new-p21-173 is ALREADY-FIXED with pointer evidence (`FreeBoy.cpp:431`, `WaveformData.cpp:123-130`, `Graph.cpp:544-552`); new-p14-112 is REAL with the failing assertion quoted above.
- Gate exit codes: `gate-01.log` EXIT=0 (RESULT: PASS).
