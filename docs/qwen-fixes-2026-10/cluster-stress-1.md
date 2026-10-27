FIXED

# Cluster stress-1: startup segfault when a metronome sample fails to decode (no audio engine yet)

Logs: `~/.local/share/zene-bunny-fix/qwen/runs/stress-1.r1/logs/` (numbered files cited below).

## Verdict: REAL, fixed

The crash path is the one in the finding's backtrace. `main()` → `ControlRegistry::instance()` →
`registerRecordToolCommands` → `metronomeoptions::options()` → `load()`
(`src/core/MetronomeOptions.cpp:100-101`) → `SampleBuffer::fromFile`. When a sample does not decode, `fromFile`
returns `SampleBuffer::emptyBuffer()` (`src/core/SampleBuffer.cpp:115`). That function default-constructed a
`SampleBuffer`, and the default member initialiser at the old `include/SampleBuffer.h:101` was
`m_sampleRate = Engine::audioEngine()->outputSampleRate()`. At that point the engine is still null, so the process
crashed with SIGSEGV.

I reproduced it end to end on this tree with the fix reverted: a copy of `build/zene` in a scratch folder, with
`plugins` linked and `LMMS_DATA_DIR` unset, started headless. Log `14-reloc-e2e-reverted.log`:
`FAILED Blocked the instance exited (code -11) before its control socket was usable`.

Nothing else on that path needs the engine. `SampleDecoder`'s DrumSynth decoder already refuses when there is no
engine (`src/core/SampleDecoder.cpp:182-183`). `PathUtil` null-checks the song (`src/core/PathUtil.cpp:51-53`).
`ConfigManager` does not touch the engine. So once a default `SampleBuffer` no longer dereferences the engine,
metronome loading needs no engine at all.

## Fix (product files: `include/SampleBuffer.h`, `src/core/SampleBuffer.cpp`)

1. **A default `SampleBuffer` never touches a null engine.** There is a new private
   `SampleBuffer::engineSampleRate()` (`src/core/SampleBuffer.cpp:72-76`, declared at `include/SampleBuffer.h:102`).
   It returns the engine's output rate. With no engine it returns `SUPPORTED_SAMPLERATES.front()` (44100), which is
   the floor the engine itself falls back to with no configured rate (`src/core/AudioEngine.cpp:480-481`).
   - The default member initialiser uses it (`include/SampleBuffer.h:106`).
   - So do the sibling default arguments of the frames constructor (`:55`) and `fromBase64` (`:95`).
   - So does `fromBase64`'s fallback for an invalid rate (`src/core/SampleBuffer.cpp:138`).

   With an engine present, every value is the same as before.
2. **`emptyBuffer()` no longer freezes a rate from before the engine existed.** The shared static is built by
   whoever calls first. A caller from before `Engine::init()` now gets its own fresh empty buffer
   (`src/core/SampleBuffer.cpp:83`). The shared buffer is still built by the first caller that has an engine, so it
   carries the engine's rate exactly as it always has.
3. **A missing metronome sample is already a silent click with a logged warning:**
   - `fromFile` logs `Failed to load sample at path …` when there is no GUI (`src/core/SampleBuffer.cpp:107-111`).
   - `click()` returns `nullptr` for an empty buffer (`src/core/MetronomeOptions.cpp:166`).

   The test asserts both. The metronome samples still decode at registration on the main thread, so the click never
   decodes on the audio thread. That design is unchanged, and it no longer needs the engine.

## Regression test: `tests/src/core/SampleBufferNoEngineTest.cpp` (new, in-tree, no third-party plug-in needed)

It is one process that never brings an engine up before its last case. Before any case runs, the `data` search path
is pointed at an empty `QTemporaryDir`: this is the relocated binary of the report.

- `aDefaultBufferNeedsNoEngine`: a default buffer is empty and has rate 44100, with no engine. This is frame #0 of
  the backtrace.
- `theSharedEmptyBufferNeedsNoEngine`: `emptyBuffer()` before `Engine::init()`.
- `registeringTheCommandsWithNoSamplesAndNoEngineSurvives`: this is the backtrace itself.
  - `ControlRegistry::instance()` with no samples and no engine.
  - Exactly 8 `Failed to load sample at path` warnings must be logged (`QTest::ignoreMessage`).
  - `options()` returns the defaults and `click()` returns `nullptr`.
- `afterEngineStartTheClickIsSilentAndTheEmptyBufferFollowsTheEngine`:
  - Configure 48000, restore the data path, then `Engine::init(true)`.
  - Both clicks are silent (`nullptr`), not a crash.
  - `emptyBuffer()->sampleRate()` and `SampleBuffer{}.sampleRate()` equal the engine's rate (48000). This catches
    fix part 2 if it is lost.

The test is deterministic: there is no timing, sleep or thread. Its seams are the `data` search path, which is
redirected to an empty directory, and the configured `audioengine/samplerate`, which is injected as 48000.

## Evidence (exact commands, exit codes as seen)

| Step | Command | Result |
|---|---|---|
| RED (test written first, product untouched) | `cmake --build build --target SampleBufferNoEngineTest -j4` → `cd build/tests && ctest -R '^SampleBufferNoEngineTest$' --output-on-failure` | build EXIT=0 (`04`); ctest **EXIT=8**, `***Exception: SegFault`, right after `PASS initTestCase` (`05-test-red.log`) |
| GREEN | `cmake --build build --target zene SampleBufferNoEngineTest AudioFileProcessorWaveViewTest LfoControllerUserWaveTest TripleOscillatorUserWaveTest RecordToolsNoEngineTest RecordToolsTest SampleBufferResampleTest -j4` → same ctest | build EXIT=0 (`06`); ctest **EXIT=0**, 1/1 (`07`); direct run `Totals: 6 passed, 0 failed` (`09`) |
| Family | `ctest -R '^(AudioFileProcessorWaveViewTest\|LfoControllerUserWaveTest\|TripleOscillatorUserWaveTest\|RecordToolsNoEngineTest\|RecordToolsTest\|SampleBufferResampleTest)$'` | **EXIT=0**, 6/6 passed (`08`) |
| 4b revert | `git diff -- include/SampleBuffer.h src/core/SampleBuffer.cpp > logs/fix.patch && git apply -R logs/fix.patch` | EXIT=0 |
| 4b rebuild + run | `cmake --build build --target zene SampleBufferNoEngineTest -j4` → ctest as above | build EXIT=0 (`12`); ctest **EXIT=8**, SegFault (`13-test-reverted.log`) |
| 4b end to end, reverted | `python3 -I logs/reloc_check.py <worktree> $TMPDIR/stress1-reloc` (relocated `zene`, no `LMMS_DATA_DIR`) | **EXIT=1**: instance exited with **code -11** before its socket (`14`) |
| Part 2 only | fix applied but the `emptyBuffer()` pre-engine guard line deleted; rebuild test; run it | **EXIT=1**: `afterEngineStart…` fails, `Actual 44100 / Expected 48000`, 5 passed 1 failed (`15`, `16`) |
| 4b restore | `git checkout -- <both product files> && git apply logs/fix.patch` (diff identical to `fix.patch`: `PATCH_IDENTICAL`) → rebuild the full target list above | apply EXIT=0, build EXIT=0 (`17`); ctest of the regression + family **EXIT=0**, 7/7 (`18`) |
| End to end, fixed | `python3 -I logs/reloc_check.py …` | **EXIT=0**: `control.ping` ok, `control.quit`, process exit 0 (`10`, `11`, `19`) |
| Gates, before the first commit | `feature_check.py <worktree>`, with only its log folder redirected (see below) | EXIT=0, all 8 PASS (`21-gates.log`). This was misleading: the new test was still untracked, so the `git ls-files` recipe did not see it. |
| Gates, after the first commit | same | EXIT=1: fork-sources and all-sources-reproduce FAIL, `tests/all-sources.txt` lacks the new test (`24-gates-head.log`) |
| Re-derive | header lines 1-157 of `tests/all-sources.txt` kept, its own "Regenerate with" recipe output written below them | the diff against the old list is exactly `+tests/src/core/SampleBufferNoEngineTest.cpp` |
| Gates, final | same | **EXIT=0**, all 8 PASS (`25-gates.log`, `26-gates-final.log`) |

**Gate note.** `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` exits 1 with
`PermissionError` (`20-gates.log`). It writes its logs to `<worktree>-feature-check`, a sibling of the worktree, and
this lane's write-jail refuses that. I did not edit the gate. I ran its unchanged source through `exec`, with one
string replaced: the log folder became `logs/feature-check`. The result: fork-sources, all-sources-reproduce,
no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity ("no new item in your files") and
file-length all PASS, `RESULT: PASS`.

`submit_check.py` runs the same unredirected `feature_check.py`. Its "gates (feature_check.py)" line therefore
reports FAIL with an empty detail: the script dies with the `PermissionError` before any gate runs (`22`, `23`). That
FAIL comes from the sandbox, not from a gate. The redirected run of the unchanged gate source is the real result.

**Build note.** A direct `cmake build` reconfigure is refused by the lane guard (`01-cmake.log`). Building an
existing target regenerated the tree for the new test. That first build failed only because ccache's default temp
dir is outside the jail (`03`). Every build after that ran with `CCACHE_TEMPDIR=$TMPDIR/ccache-stress-1`.

## Registration

- `tests/CMakeLists.txt`: `SampleBufferNoEngineTest` registered next to `RecordToolsNoEngineTest`.
- `tests/fork-sources.txt`: the new test, in sorted position.
- `tests/all-sources.txt`: re-derived from its own recipe, which adds only the new test.
- `tests/upstream-modifications.txt`: entries for `include/SampleBuffer.h`, `src/core/SampleBuffer.cpp` and
  `tests/CMakeLists.txt`, each with a reason.

## Self-check

- **The regression fails without the product change and passes with it.** Without it, ctest EXIT=8 (SegFault,
  `13`) and the relocated binary exits -11 (`14`). With it restored, ctest EXIT=0 (`18`) and the relocated binary
  answers ping and exits 0 (`19`). Fix part 2 on its own is also caught (`16`: 44100 vs 48000).
- **The test is deterministic.** There is no wall-clock timing, sleep, thread or race. Its seams are
  `QDir::setSearchPaths("data", {empty QTemporaryDir})` and the injected `audioengine/samplerate = 48000`. Case
  order is QtTest's fixed declaration order.
- **The fix is at the cause and complete.** Every engine dereference inside `SampleBuffer` now goes through
  `engineSampleRate()`: the member initialiser (`.h:106`), both default arguments (`.h:55`, `.h:95`) and the
  `fromBase64` fallback (`.cpp:138`). All `emptyBuffer()` callers (`.cpp:90,115,125,157`, plus
  `Sample::m_buffer`'s default at `include/Sample.h:148`, which makes a default `Sample` safe before the engine too)
  go through the guarded function. Metronome loading's other dependencies were checked and are already engine-safe
  (`SampleDecoder.cpp:182-183`, `PathUtil.cpp:51-53`).
  - Left alone on purpose: `Sample(const SampleFrame*, size_t, int sampleRate = Engine::audioEngine()->…)` at
    `include/Sample.h:97`. It is a different class, and its only caller (`src/core/ControlCommandsBrowserMedia.cpp:261`)
    passes an explicit rate behind an engine command.
- **No collateral damage.** With an engine, `engineSampleRate()` returns exactly
  `Engine::audioEngine()->outputSampleRate()`, which is the old expression. The shared empty buffer is still built by
  the first caller that has an engine, so a project's empty `SampleClip` still saves the same `sample_rate`
  (`src/core/SampleClip.cpp:941`), and loading is unchanged.
  - The only behaviour that changes is in processes with no engine, which used to crash. Now they get a 44100 empty
    buffer.
  - Realtime: `emptyBuffer()` called on the audio thread always has an engine, so it takes the old static path. The
    extra work is one pointer load and a null compare, with no allocation, lock or log. No command is added, and undo
    is not touched.
  - Family tests 6/6 pass (`08`, `18`).
- **ALREADY-FIXED / FALSE-POSITIVE evidence:** not applicable, the verdict is FIXED.
- **Not checked:** the whole ctest suite. The header change recompiles every target that includes `SampleBuffer.h`;
  I rebuilt `zene` and the 7 tests listed above. The relocated run used `build/plugins` through a symlink, not the
  third-party plug-in farm, which this crash does not depend on.
