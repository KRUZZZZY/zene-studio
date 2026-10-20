Verdict: **FIXED**

new-p9-332: FIXED

# Cluster new-p9-332

Two findings in `plugins/Organic/Organic.cpp` were handled in this lane, each in
its own commit:

* **new-p9-332** - the control thread wrote the oscillator parameters the audio
  thread rendered from (a data race on the render path). FIXED.
* **new-p16-152** - the render dereferenced a null oscillator chain when the
  oscillator count was zero. FIXED (second commit of this lane).

## new-p9-332 - the oscillator parameters raced between the control and the audio thread

### Decision: REAL, and fixed at the cause

`OscillatorObject` held the per-oscillator volume and detuning in plain
`float` members. The control thread wrote them in `updateVolume()` and
`updateDetuning()` - the slots the volume, panning, harmonic and detune knobs
are wired to in `OrganicInstrument::OrganicInstrument()`
(`plugins/Organic/Organic.cpp:91-100`) - and the audio thread read the very
same floats through the `const float&` references the `Oscillator` instances
keep (`plugins/Organic/Organic.cpp:245-280`, `include/Oscillator.h:110-113`).
A knob move while a note sounded was therefore a data race on the render path.

### The fix

The control thread now **publishes** the parameters into atomics and the audio
thread **adopts** them at the start of the period:

* `plugins/Organic/Organic.h` - `OscillatorObject` gained the
  `PublishedParams` snapshot (four `std::atomic<float>`) and the declaration of
  `refreshAudioParams()`; the four floats the oscillators render from are
  documented as audio-thread-owned.
* `plugins/Organic/Organic.cpp:updateVolume()` and
  `plugins/Organic/Organic.cpp:updateDetuning()` - store into the snapshot
  (`std::memory_order_relaxed`); they no longer touch the rendered values.
* `plugins/Organic/Organic.cpp:refreshAudioParams()` - copies the snapshot into
  the rendered values; it runs on the audio thread only.
* `plugins/Organic/Organic.cpp:playNoteImpl()` - calls `refreshAudioParams()`
  for every oscillator at the start of the period, before the oscillators are
  built and before they render.

Nothing is allocated, freed, locked, waited on or logged on the audio thread:
the adoption is four relaxed atomic loads per oscillator per note.

### The regression test

`tests/src/plugins/OrganicParamRaceTest.cpp` compiles the plugin sources into
the test binary (the `ExprSynth` pattern) and drives the real code:

1. one render period - the audio thread adopts the published parameters
   (`m_volumeLeft` becomes the published volume);
2. the control thread publishes a new volume (`m_volModel.setValue(50)` fires
   `updateVolume()`);
3. the value the audio thread renders from must **not** have changed - the
   control thread publishes, it does not write in place;
4. the next render period adopts the published value.

The test is deterministic: no threads, no sleeps, no wall-clock timing - every
step is a direct call on the plugin's own code.

All build and test logs are in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-332.r1/logs`.

## Evidence (new-p9-332)

| Stage | Exact command | Exit / result |
|---|---|---|
| Original-code RED build | `cmake --build build --target OrganicParamRaceTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-332.r1/logs/build-4.log' 2>&1` | 0 |
| Original-code RED test | `ctest -R '^OrganicParamRaceTest$' --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-332.r1/logs/test-1.log' 2>&1` | 8; `controlThreadPublishDoesNotWriteTheAudioThreadsValue` failed: "the control thread wrote the value the audio thread renders from"; QtTest totals: 2 passed, 1 failed. CTest ran 1/1 target. |
| Fixed build | `cmake --build build --target OrganicParamRaceTest organic > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-332.r1/logs/build-5.log' 2>&1` | 0 |
| Fixed GREEN test | `ctest -R '^OrganicParamRaceTest$' --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-332.r1/logs/test-2.log' 2>&1` | 0; CTest passed 1/1. |
| Product patch for the proof | `git diff -- plugins/Organic/Organic.cpp plugins/Organic/Organic.h > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-332.r1/logs/fix.patch'` | 0 (133 lines) |
| Revert the product patch | `git apply -R '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-332.r1/logs/fix.patch'` | 0 |
| Reverted-code rebuild | `cmake --build build --target OrganicParamRaceTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-332.r1/logs/build-6.log' 2>&1` | 0 |
| Proof RED test | `ctest -R '^OrganicParamRaceTest$' --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-332.r1/logs/test-3.log' 2>&1` | 8; the same assertion failed again; QtTest totals: 2 passed, 1 failed. |
| Restore the product patch | `git apply '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-332.r1/logs/fix.patch'` | 0 |
| Restored-code rebuild | `cmake --build build --target OrganicParamRaceTest organic zene > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-332.r1/logs/build-7.log' 2>&1` | 0 |
| Proof GREEN test | `ctest -R '^OrganicParamRaceTest$' --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-332.r1/logs/test-4.log' 2>&1` | 0; CTest passed 1/1. |
| Organic family test | `ctest -R '^Organic' --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-332.r1/logs/test-7.log' 2>&1` | 0; CTest passed 2/2 (`OrganicParamRaceTest`, `OrganicZeroOscTest`). |
| Plugin render comparison (the family that renders the Organic plugin against the frozen upstream copy) | `cmake --build build --target PluginPortsMigrationTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-332.r1/logs/build-8.log' 2>&1` then `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-332.r1/logs/test-8.log' 2>&1` | 0 / 0; CTest passed 1/1 (44 QtTest cases). The Organic slice-5 render is still sample-exact against `tests/reference/Organic`. |
| LADSPA fixture needed by that family | `cmake --build build --target decimator_1202 > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-332.r1/logs/build-13.log' 2>&1` | 0; before this fixture was built the same family failed on `nonRealtimeLadspaEffectsAreInvalid` ("SWH Decimator test fixture was not loaded"), a missing build artefact unrelated to this change (`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-332.r1/logs/test-5.log`, exit 8). |

## Self-check

* **The regression test FAILS with the product change reverted and PASSES with
  it** - exit 8 on the original code and on the reverted code
  (`test-1.log`, `test-3.log`), exit 0 with the fix (`test-2.log`,
  `test-4.log`); the reverted-code run uses the same test binary rebuilt from
  the reverted sources (`build-6.log`).
* **The test is deterministic** - no threads, no sleeps, no wall-clock timing.
  The seam is the plugin's own publish/adopt path: the control side is the
  model's `setValue()` (which fires `updateVolume()`), the audio side is
  `playNoteImpl()`. Both are direct calls on the same thread as the test.
* **The fix is at the cause and complete** - the writers are `updateVolume()`
  and `updateDetuning()` (the only writers of the four floats), the readers
  are the `Oscillator` instances that hold the references; both writers now
  publish and the single reader-side copy (`refreshAudioParams()`) runs on the
  audio thread. The siblings were checked: `updateAllDetuning()`
  (`plugins/Organic/Organic.cpp:353`) calls `updateDetuning()`, so it
  publishes like the knob path; `randomiseSettings()` writes the models, which
  publish through the same slots; the sample-rate path
  (`updateAllDetuning()`, wired to `sampleRateChanged()`) publishes too. No
  other code reads the four floats (`grep -n 'm_volumeLeft\|m_volumeRight\|m_detuningLeft\|m_detuningRight' plugins/Organic/Organic.cpp`).
* **No collateral damage** - the published values are numerically identical to
  the ones the control thread used to write, so a project renders the same:
  `PluginPortsMigrationTest` still renders the Organic plugin sample-exactly
  against the frozen upstream copy (`test-8.log`). The only behavioural
  difference is *when* a parameter change reaches the render: at the next
  period boundary instead of in the middle of the current one. Old projects
  and saved data are untouched - `saveSettings()`/`loadSettings()` are not
  changed, the new members are not serialised.
* **Realtime** - the audio thread only performs four relaxed atomic loads per
  oscillator per note (`refreshAudioParams()`); no allocation, no free, no
  mutex, no wait, no logging, no signal emission, no allocating Qt call.
* **Undo and commands** - untouched: no control command was added or changed,
  and the change adds no user-visible state (the knobs keep their behaviour
  and their automation models).
* **Portability** - `std::atomic<float>` is C++20 standard library only; no
  Qt-6-only API, no `windows.h` macro, no `std::filesystem`. The atomic is
  asserted lock-free at compile time
  (`static_assert(std::atomic<float>::is_always_lock_free)`), so the render
  path can never fall back to a lock.

## Files committed (commit 1 - new-p9-332)

- `plugins/Organic/Organic.h` (the published snapshot and the adoption)
- `plugins/Organic/Organic.cpp` (publish in the two slots, adopt in the render)
- `tests/src/plugins/OrganicParamRaceTest.cpp` (the regression test)
- `tests/CMakeLists.txt` (the test target)
- `tests/fork-sources.txt`, `tests/all-sources.txt` (the new test source is registered)
- `tests/upstream-modifications.txt` (the two inherited files and why they changed)
- `docs/qwen-fixes-2026-10/cluster-new-p9-332.md` (this report)
