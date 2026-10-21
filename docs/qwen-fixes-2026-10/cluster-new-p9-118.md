# Cluster new-p9-118 / new-p15-57: EnvelopeAndLfoParameters audio path

Two findings in `src/core/EnvelopeAndLfoParameters.cpp`, one file, two commits.

new-p9-118: FIXED

**The finding.** `EnvelopeAndLfoParameters::fillLevel()` took `m_paramMutex`
(`QMutexLocker m(&m_paramMutex);`, `src/core/EnvelopeAndLfoParameters.cpp:309`
in the reported tree) while it generated the envelope and LFO levels. `fillLevel()`
is the audio path: it is called from `InstrumentSoundShaping` (`:100`, `:148`,
`:153`, `:230`), i.e. from the instrument render. `updateSampleVars()` takes the
same mutex (`:415`) and is called from the parameter side (the knob models'
`dataChanged`, `src/core/EnvelopeAndLfoParameters.cpp:136-165`, and
`loadSettings()`), so the audio thread could block behind a parameter update.

**Why it was REAL.** The lock was unconditional in the audio path: every
`fillLevel()` call took the mutex, and the writer held it for the whole of
`updateSampleVars()` (which re-derives the envelope tables and reallocates them).

**The fix.** The parameters the audio path reads are now published as an
immutable snapshot (`EnvelopeAndLfoParameters::AudioState`,
`include/EnvelopeAndLfoParameters.h:61-80`). The writer side builds one under
`m_paramMutex` (`rebuildAudioState()`, `src/core/EnvelopeAndLfoParameters.cpp:451`)
and hands it over with an atomic pointer plus a publication sequence
(`publishAudioState()`, `:570`). The audio path claims it with
`acquireAudioState()` (`:631`) - atomic loads and one atomic counter, no lock,
no wait, no allocation - and releases it when it is done (`:652`). A snapshot
that a reader may still hold is retired, not freed; it is freed when the reader
count is back to zero (`reclaimRetiredAudioStates()`, `:588`). The envelope
buffers of a retired snapshot are recycled (`takeEnvBuffer()`, `:610`), so a
parameter change still does not allocate a fresh buffer every time.

**The regression.** The tree's own real-time rule gate, the rt-safety sweep
(`tests/rt-safety-sweep.py`, registered as the CTest `RtSafetySweep`), now
declares the audio path of this file in `tests/rt-safety-scope.txt:59-63`:

    src/core/EnvelopeAndLfoParameters.cpp	EnvelopeAndLfoParameters::fillLevel	...
    src/core/EnvelopeAndLfoParameters.cpp	EnvelopeAndLfoParameters::fillLfoLevel	...
    src/core/EnvelopeAndLfoParameters.cpp	EnvelopeAndLfoParameters::lfoShapeSample	...
    src/core/EnvelopeAndLfoParameters.cpp	EnvelopeAndLfoParameters::updateLfoShapeData	...
    src/core/EnvelopeAndLfoParameters.cpp	EnvelopeAndLfoParameters::acquireAudioState	...
    src/core/EnvelopeAndLfoParameters.cpp	EnvelopeAndLfoParameters::releaseAudioState	...

The sweep is source-based and deterministic: it reads the declared bodies and
fails on a lock-guard, an allocation, a container growth or a synchronous stdio
write. Before the fix the declared `fillLevel` fails on the `QMutexLocker`;
after the fix the declared bodies are clean. The gate needed no allowlist
entry, because the fix removes the hit rather than tolerating it.

### Evidence table

| Check | Exact command and result |
| --- | --- |
| RED before fix | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-118.r1/logs/fix-new-p9-118.patch` then `python3 tests/rt-safety-sweep.py --tree . --scope tests/rt-safety-scope.txt --allowlist tests/rt-safety-allowlist.txt --check > .../red-p9-118-sweep-lockhit.log 2>&1; echo EXIT=$?`; **EXIT=1**: `FAIL: NEW rt-safety hit: src/core/EnvelopeAndLfoParameters.cpp:EnvelopeAndLfoParameters::fillLevel:lock-guard [locking] - 1 occurrence(s), first at src/core/EnvelopeAndLfoParameters.cpp:309: QMutexLocker m(&m_paramMutex);` |
| GREEN after fix | `git apply .../fix-new-p9-118.patch` then the same sweep command, output to `.../green-p9-118-sweep.log`; **EXIT=0**, `PASS: every hit on a declared audio-thread path is allowlisted with a reason and at its allowed count.` (77 declared pairs, 4 hits, all pre-existing and allowlisted) |
| RED, full revert | with the product files reverted and the two new helper declarations still in the scope file, the same sweep command, output to `.../red-p9-118-sweep.log`; **EXIT=1**: `FAIL: ...EnvelopeAndLfoParameters::acquireAudioState the declared symbol resolves to no body ...` (the scope declaration is bound to the fix) |
| Affected family | `ctest -R '^(EnvelopeAndLfoParametersTest\|ModulationLayerTest\|ModulationRateTest\|EnvelopeGraphTest\|LfoGraphTest\|RtSafetySweep)$' --output-on-failure` from `build/tests`, output to `.../test-1.log`; **EXIT=0**, 6/6 CTest targets passed |
| Build | `cmake --build build --target zene -j $(nproc)`; **EXIT=0** (`.../build-3.log`) |
| Complexity gate | **EXIT=0** (`PASS complexity - no new item in your files`) |
| File-length gate | **EXIT=0** (`PASS file-length - no new item in your files`) |

### Build and test commands

```text
cmake --build build --target zene -j $(nproc) > .../logs/build-1.log 2>&1; echo EXIT=$?   # EXIT=2, AudioState used before its declaration
cmake --build build --target zene -j $(nproc) > .../logs/build-2.log 2>&1; echo EXIT=$?   # EXIT=2, const-correctness of the reader counter
cmake --build build --target zene -j $(nproc) > .../logs/build-3.log 2>&1; echo EXIT=$?   # EXIT=0
cmake --build build --target EnvelopeAndLfoParametersTest ModulationLayerTest ModulationRateTest EnvelopeGraphTest LfoGraphTest -j $(nproc) > .../logs/build-tests-1.log 2>&1; echo EXIT=$?   # EXIT=0
ctest -R '^(EnvelopeAndLfoParametersTest|ModulationLayerTest|ModulationRateTest|EnvelopeGraphTest|LfoGraphTest|RtSafetySweep)$' --output-on-failure > .../logs/test-1.log 2>&1; echo EXIT=$?   # EXIT=0, 6/6
git diff -- src/core/EnvelopeAndLfoParameters.cpp include/EnvelopeAndLfoParameters.h > .../logs/fix-new-p9-118.patch   # 775 lines
git apply -R .../logs/fix-new-p9-118.patch; echo EXIT=$?   # EXIT=0
python3 tests/rt-safety-sweep.py --tree . --scope tests/rt-safety-scope.txt --allowlist tests/rt-safety-allowlist.txt --check > .../logs/red-p9-118-sweep.log 2>&1; echo EXIT=$?   # EXIT=1
python3 tests/rt-safety-sweep.py --tree . --scope tests/rt-safety-scope.txt --allowlist tests/rt-safety-allowlist.txt --check > .../logs/red-p9-118-sweep-lockhit.log 2>&1; echo EXIT=$?   # EXIT=1, the lock-guard hit in fillLevel
git apply .../logs/fix-new-p9-118.patch; echo EXIT=$?   # EXIT=0
python3 tests/rt-safety-sweep.py --tree . --scope tests/rt-safety-scope.txt --allowlist tests/rt-safety-allowlist.txt --check > .../logs/green-p9-118-sweep.log 2>&1; echo EXIT=$?   # EXIT=0
```

The required gates ran from the worktree root:

```text
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-118   # EXIT=0
```

Files committed for new-p9-118:

- `include/EnvelopeAndLfoParameters.h`
- `src/core/EnvelopeAndLfoParameters.cpp`
- `tests/rt-safety-scope.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-118.md`

new-p15-57: FIXED

**The finding.** `loadSettings()` wrote `m_userWave` (a `std::shared_ptr<const
SampleBuffer>`) without taking `m_paramMutex`, while the audio path read the
same shared pointer (`src/core/EnvelopeAndLfoParameters.cpp:394` in the
reported tree; `m_userWave.get()` in `lfoShapeSample()`). A concurrent
assignment is a data race on the shared pointer, and the last strong reference
can go away while the audio thread is sampling the buffer.

**Why it was REAL.** The wave was a plain member: the writer replaced it
(`m_userWave = SampleBuffer::fromFile(...)`) with no synchronisation at all,
and the reader took a raw pointer out of it (`Oscillator::userWaveSample(
m_userWave.get(), phase)`) and then sampled the buffer over the whole period.
The mutex that `fillLevel()` took did not help - `loadSettings()` never took
it - and after the new-p9-118 commit the audio path no longer takes it either,
so the wave has to travel with the published snapshot.

**The fix.** The user-defined wave is now part of the published snapshot
(`EnvelopeAndLfoParameters::AudioState::userWave`,
`include/EnvelopeAndLfoParameters.h:78`). The writer copies the wave it
holds into the snapshot it publishes (`rebuildAudioState()`,
`src/core/EnvelopeAndLfoParameters.cpp:557`), the audio path samples the
wave of the snapshot it holds (`lfoShapeSample()`,
`src/core/EnvelopeAndLfoParameters.cpp:236`), and the GUI accessor reports
the published wave (`getLfoUserWave()`, `:743`). A wave that is replaced is
no longer freed by the replacement: the snapshot that still carries it is
retired, not freed, and it is freed only when the reader count is back to zero
(`reclaimRetiredAudioStates()`, `:588`). The saved project is unchanged: the
wave file name still comes from the same member (`saveSettings()`,
`src/core/EnvelopeAndLfoParameters.cpp:381`).

**The regression.** `tests/src/core/EnvelopeAndLfoParametersTest.cpp`:

- `replacedUserWaveIsNotFreedWhileTheAudioPathHoldsIt()` (`:120`) stands in for
  the audio thread with the test seam `holdAudioState()` (the snapshot the
  audio path holds while it fills a level), replaces the wave, and requires the
  wave that is in use to still be alive (`!weakWave.expired()`). Without the
  fix the replacement drops the last strong reference and frees the buffer the
  audio path is sampling.
- `theAudioPathSamplesThePublishedUserWave()` (`:140`) requires the audio path
  to render the wave that was published with the parameters: replacing the wave
  changes what `fillLevel()` produces.

### Evidence table

| Check | Exact command and result |
| --- | --- |
| RED before fix | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-118.r1/logs/fix-new-p15-57.patch` then `cmake --build build --target EnvelopeAndLfoParametersTest -j $(nproc)` (**EXIT=0**) and `ctest -R '^EnvelopeAndLfoParametersTest$' --output-on-failure > .../logs/test-4-red.log 2>&1; echo EXIT=$?`; **EXIT=8**: `FAIL! : EnvelopeAndLfoParametersTest::replacedUserWaveIsNotFreedWhileTheAudioPathHoldsIt() '!weakWave.expired()' returned FALSE.` (5 passed, 1 failed) |
| GREEN after fix | `git apply .../fix-new-p15-57.patch` then the same build (**EXIT=0**) and `ctest -R '^(EnvelopeAndLfoParametersTest\|ModulationLayerTest\|ModulationRateTest\|EnvelopeGraphTest\|LfoGraphTest\|RtSafetySweep)$' --output-on-failure > .../logs/test-5-green.log 2>&1; echo EXIT=$?`; **EXIT=0**, 6/6 CTest targets passed |
| Affected family | 6 CTest targets executed (EnvelopeAndLfoParametersTest, ModulationLayerTest, ModulationRateTest, EnvelopeGraphTest, LfoGraphTest, RtSafetySweep), 6/6 passed; the EnvelopeAndLfoParametersTest binary itself runs 6 test functions, all passed |
| Build | `cmake --build build --target EnvelopeAndLfoParametersTest -j $(nproc)`; **EXIT=0** (`.../build-tests-4.log`, `.../build-tests-6.log`) |
| Complexity gate | **EXIT=0** (`PASS complexity - no new item in your files`) |
| File-length gate | **EXIT=0** (`PASS file-length - no new item in your files`) |

### Build and test commands

```text
git diff -- src/core/EnvelopeAndLfoParameters.cpp include/EnvelopeAndLfoParameters.h > .../logs/fix-new-p15-57.patch   # 56 lines
git apply -R .../logs/fix-new-p15-57.patch; echo EXIT=$?   # EXIT=0
cmake --build build --target EnvelopeAndLfoParametersTest -j $(nproc) > .../logs/build-tests-5.log 2>&1; echo EXIT=$?   # EXIT=0
ctest -R '^EnvelopeAndLfoParametersTest$' --output-on-failure > .../logs/test-4-red.log 2>&1; echo EXIT=$?   # EXIT=8, RED
git apply .../logs/fix-new-p15-57.patch; echo EXIT=$?   # EXIT=0
cmake --build build --target EnvelopeAndLfoParametersTest -j $(nproc) > .../logs/build-tests-6.log 2>&1; echo EXIT=$?   # EXIT=0
ctest -R '^(EnvelopeAndLfoParametersTest|ModulationLayerTest|ModulationRateTest|EnvelopeGraphTest|LfoGraphTest|RtSafetySweep)$' --output-on-failure > .../logs/test-5-green.log 2>&1; echo EXIT=$?   # EXIT=0, 6/6
```

The required gates ran from the worktree root:

```text
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-118   # EXIT=0
```

Files committed for new-p15-57:

- `include/EnvelopeAndLfoParameters.h`
- `src/core/EnvelopeAndLfoParameters.cpp`
- `tests/src/core/EnvelopeAndLfoParametersTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-118.md`

