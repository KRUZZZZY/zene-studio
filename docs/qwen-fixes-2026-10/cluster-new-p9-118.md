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
`include/EnvelopeAndLfoParameters.h:52-73`). The writer side builds one under
`m_paramMutex` (`rebuildAudioState()`, `src/core/EnvelopeAndLfoParameters.cpp:451`)
and hands it over with an atomic pointer plus a publication sequence
(`publishAudioState()`, `:625`). The audio path claims it with
`acquireAudioState()` (`:706`) - atomic loads and one atomic counter, no lock,
no wait, no allocation - and releases it when it is done (`:735`). A snapshot
that a reader may still hold is retired, not freed; it is freed when the reader
count is back to zero (`reclaimRetiredAudioStates()`, `:645`). The envelope
buffers of a retired snapshot are recycled (`takeEnvBuffer()`, `:684`), so a
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

new-p15-57: (filled in by the second commit)

