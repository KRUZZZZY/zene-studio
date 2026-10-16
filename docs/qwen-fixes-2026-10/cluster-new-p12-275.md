# Cluster new-p12-275: TripleOscillator user-wave pointers race with playback

**Verdict: FIXED**

## The bug is real

`OscillatorObject::oscUserDefWaveDblClick()` wrote two `std::shared_ptr` members on the
control/GUI thread:

- `plugins/TripleOscillator/TripleOscillator.cpp` (was line 142) `m_sampleBuffer = SampleBuffer::fromFile(af);`
- and the line below it, `m_userAntiAliasWaveTable = Oscillator::generateAntiAliasUserWaveTable(...)`.

`TripleOscillator::playNoteImpl()` reads those same two members on the audio thread when it
configures new oscillators:

- `plugins/TripleOscillator/TripleOscillator.cpp:358` `oscs_l[i]->setUserWave( m_osc[i]->m_sampleBuffer );`
- `plugins/TripleOscillator/TripleOscillator.cpp:360` `oscs_l[i]->setUserAntiAliasWaveTable(m_osc[i]->m_userAntiAliasWaveTable);`

A `shared_ptr` only makes distinct instances safe; reading one `shared_ptr` object while another
thread assigns it is a data race (the object's pointer and its control-block reference are read
and written concurrently), and a note starting mid-load could receive mismatched wave data. The
same unguarded assignment existed in `TripleOscillator::loadSettings()` (`userwavefile` handling).

`playNoteImpl()` runs inside `AudioEngine::renderNextPeriod()`, which holds
`AudioEngine::m_changeMutex` for a whole render period
(`src/core/AudioEngine.cpp:916`). Every other control-thread edit that the audio thread reads
goes through the same lock (the `AutomatableModel` setters use `modelChangeGuard()` /
`requestChangesGuard()`; `Song::setScale` and `ConvolverReverbEffect::install` publish
`shared_ptr`s under `requestChangeInModel()`/`doneChangeInModel()`). The user-wave pointers were
the only publication that skipped it.

## Fix

`OscillatorObject::setUserWave(std::shared_ptr<const SampleBuffer>)` is the single publication
seam. It builds the anti-alias table first (outside the lock), then swaps both pointers together
under `Engine::audioEngine()->requestChangesGuard()`. `oscUserDefWaveDblClick()` and
`TripleOscillator::loadSettings()` now both call it. This is behaviour-preserving: the same
buffer/table are produced and stored; only their handoff is now serialized with the render
period. Nothing was added to the audio-thread path (the read side in `playNoteImpl` is unchanged).

- `plugins/TripleOscillator/TripleOscillator.h` — declares `setUserWave`.
- `plugins/TripleOscillator/TripleOscillator.cpp:137-151` — the guarded publication; the two
  call sites route through it.

## Regression

`tests/src/plugins/TripleOscillatorUserWaveTest.cpp` (new target
`TripleOscillatorUserWaveTest`, registered in `tests/CMakeLists.txt`; the plugin sources are
compiled into the test binary, the `SlicerTZoomTest` pattern). It holds a render period open
with `AudioEngine::requestChangeInModel()` on the test thread and calls the real publication on
a second thread; with the guard the call cannot complete inside the period, without it the call
escapes immediately.

## Evidence

All `ctest` commands ran from `build/tests`; all builds ran from the worktree root. Every run's
output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-275.r1/logs/`.

| Stage | Exact command | Exit |
| --- | --- | --- |
| Build with fix (`build-5-green.log`) | `cmake --build build --target zene tripleoscillator TripleOscillatorUserWaveTest` | 0 |
| GREEN (`test-3-green.log`) | `ctest -R '^TripleOscillatorUserWaveTest$' --output-on-failure` | 0 (1/1) |
| RED, full product patch reversed (`build-3-red.log`) | `git diff -- plugins/TripleOscillator/TripleOscillator.h plugins/TripleOscillator/TripleOscillator.cpp > logs/fix.patch && git apply -R logs/fix.patch` (exit 0) then `cmake --build build --target TripleOscillatorUserWaveTest` | 2 — test cannot compile: *"`class lmms::OscillatorObject` has no member named `setUserWave`"* (`TripleOscillatorUserWaveTest.cpp:60,107`) |
| RED, guard only removed (`test-2-seam-red.log`) | with `setUserWave` present but the two guard lines removed (`logs/noguard.patch`): `cmake --build build --target TripleOscillatorUserWaveTest` (exit 0) then `ctest -R '^TripleOscillatorUserWaveTest$' --output-on-failure` | 0 build; **8** test — `FAIL! ... '!escaped' returned FALSE. (OscillatorObject published a user waveform while a render period was in flight ...)` |
| Restore fix | `git apply logs/fix.patch` (restored diff byte-identical to `fix.patch`) | 0 |
| Affected family build (`build-6-family.log`) | `cmake --build build --target PluginPortsMigrationTest ScriptEngineTest` | 0 |
| Affected family (`test-4-family.log`) | `ctest -R '^(PluginPortsMigrationTest\|ScriptEngineTest)$' --output-on-failure` | 8 — `ScriptEngineTest` passed; in `PluginPortsMigrationTest`, 44 passed / 2 skipped and the `tripleoscillator` render is sample-exact (migrated sha256 `7b06600c…` = reference), but `nonRealtimeLadspaEffectsAreInvalid()` fails because the SWH Decimator fixture `decimator_1202.so` is not installed in this environment (unrelated pre-existing fixture gap) |
| Complexity gate (`gate-3-complexity.log`) | `bash tests/complexity-gate.sh --check` | 1 (pre-existing; see below) |
| File-length gate (`gate-2-filelength.log`) | `bash tests/file-length-gate.sh --check` | 1 (pre-existing; see below) |

The guard-removal RED is the proof that the test catches the bug: the only difference between
that tree and the fixed tree is the two `requestChangesGuard()` lines, and the assertion fails.
The full `git apply -R fix.patch` RED is a build error for the same reason.

## Pre-existing gate failures (not caused by this change)

Neither gate names a file this change touches. `tests/src/plugins/TripleOscillatorUserWaveTest.cpp`
is not listed by either gate, and `setUserWave` has CCN 1; the new file is under the 500-line
limit.

`complexity-gate.sh --check` exits 1 on functions that are over CCN 10 and absent from the
committed baseline, all in `fork-sources.txt` files this change never touches:

```
REGRESSION: new function over target: dotCentreAlong@tests/src/gui/XyPadPairTest.cpp (CCN 16)
REGRESSION: new function over target: lmms::interchange::readdetail::parseNotes@src/core/DawProjectReadTracks.cpp (CCN 13)
REGRESSION: new function over target: lmms::adjust@src/core/ControlCommandsMixerTools.cpp (CCN 12)
REGRESSION: new function over target: lmms::setStrip@src/core/ControlCommandsMixerStrip.cpp (CCN 11)
REGRESSION: new function over target: lmms::control::rotateProjectRevision@src/core/ProjectRevisions.cpp (CCN 11)
REGRESSION: new function over target: lmms::importedOverrides@src/gui/ControlCommandsShortcuts.cpp (CCN 11)
```

`git show HEAD:tests/complexity-baseline.tsv | grep -c parseNotes` returns 0, and the file was
last changed by the merged lane `bugs/qwen-new-p10-98` (`729fb9709`, 2026-10-14) after the
baseline's last refresh (commit `40104e236`, 2026-10-09), so the baseline is stale at this
worktree's HEAD independently of this finding. The same holds for `--scope all`.

`file-length-gate.sh --check` exits 1 listing 14 files over 500 code lines with no baseline
entry (e.g. `tests/src/core/DeviceDspTest.cpp` 752, `src/core/TakeLane.cpp` 520,
`tests/src/core/VideoTrackTest.cpp` 589) — again none touched here.

Refreshing either baseline would require `--reanchor`, which the task rules forbid, and fixing
the unrelated functions is outside "change only the buggy behaviour". The unpiped exit codes are
recorded above as the gates actually returned them.

## Files committed

- `plugins/TripleOscillator/TripleOscillator.h`
- `plugins/TripleOscillator/TripleOscillator.cpp`
- `tests/src/plugins/TripleOscillatorUserWaveTest.cpp`
- `tests/CMakeLists.txt`
- `tests/all-sources.txt`
- `tests/fork-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p12-275.md`
