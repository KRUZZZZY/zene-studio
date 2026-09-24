# BUG-BASE-2 — `ControlMeterCommands` "the live tap measures a playing project" — 2026-09-24

**Verdict: not a metering defect and not a live-render defect. The fixture was silent
because its instrument module was not in the build tree.** The offline render of the
*same* fixture is equally silent in this tree, which is what splits the fork: the tap,
`LufsMeter` and the engine's live mix are all correct, and were measured correct.

## The decisive measurement

Both lines are the product's own `render` CLI on the test's own generated fixture
(`tests/data/loudness/make-fixtures.py` → `tone-23.mmp`), same binary, same tree:

    build/plugins WITHOUT libaudiofileprocessor.so
      "The plugin \"audiofileprocessor\" wasn't found or could not be loaded!
       Reason: \"Plugin not found.\""
      Loudness: -inf LUFS-I (target -23.0), short-term max -inf, true peak -inf dBTP
                -> NOT MEASURED (no measurable signal)                       RENDER_EXIT=0

    after `cmake --build build --target audiofileprocessor`
      Loudness: -23.04 LUFS-I (target -23.0), short-term max -23.00, true peak -22.99
                dBTP -> PASS [deviation -0.04 LU]                            RENDER_EXIT=0

The task's premise "the OFFLINE render of the same fixture is LOUD" is **false in this
build tree**. It was measured loud once, in `docs/LUFS-WIRING.md` §4.1 (lane
`post-alpha/lufs-wire`, base `ccd07f490`), in a tree whose plugin directory was built.

## What the fork actually was

* **(A) the live path feeds a zero buffer — yes, and that is the whole defect.** The
  master mix is all-zero, so `LufsMeter` correctly answers with the sentinel. The live
  render path is *not* broken: the fixture's instrument never loaded. `PluginFactory`
  scans `<binary dir>/plugins` (relative `plugins`, `src/core/PluginFactory.cpp:299`),
  and that directory held three modules (`libamplifier.so`, `libtripleoscillator.so`,
  `libvst3instrument.so`), not four. `Instrument::instantiate()` returns a
  **DummyInstrument** when a module is not found, so the project opened with
  `errors: []`, `not_loaded: []` and played nothing.
* **(B) the meter/window contract is wrong — refuted.** `true_peak_dbtp` needs no window
  at all, and it is null too. `AudioEngine::renderStageMix()` feeds the tap the
  just-mixed `m_outputBufferWrite` after `swapBuffers()` (`src/core/AudioEngine.cpp:459`),
  which is the block the period produced; with a playing fixture the same field now reads
  `-22.99 dBTP`. Nothing in `LufsMeter::processBlock`'s frame/channel contract changed.
* **(C) the command reads a different tap — refuted.** `blocks_fed` (680) and `frames_fed`
  (174080 = 680 × 256) climb on the very instance the command reads.

## Load-bearing proof

The load-bearing artefact is the module, not a source hunk, so the pair is made by moving
the module rather than by reverting code:

| state | command (from `build/tests`) | result |
| --- | --- | --- |
| `libaudiofileprocessor.so` moved aside | `CTEST_JOBS=1 ctest -R '^ControlMeterCommands$'` | `EXIT=8`, 0% tests passed |
| module restored | same | `EXIT=0`, 1/1 Passed, 13.41 s |

Green readings off the wire (the run's own transcript, `meter.get_state` after 4 s of
playback): tone-23 `integrated_lufs -23.003`, `true_peak_dbtp -22.99`; tone-33
`integrated_lufs -33.003`, `true_peak_dbtp -32.99` — the EBU Tech 3341 case-1/case-2
values, 10.000 LU apart. The live tap measures the fixture's real level.

## The fix

* `cmake --build build --target audiofileprocessor` — the module the fixture plays
  through. This is what turns the check green.
* `tests/control-meter-commands.py` — `fixture_instrument_missing()` names the module
  before any check runs and exits 2 ("cannot run") when this build cannot load it, so
  this failure can never again read as a metering defect. It cannot turn a green run red:
  a fixture with no instrument always fails the live checks (the pre-fix transcript has
  exactly that shape, `loud=None quiet=None` with `blocks_fed` 680), so it only fires
  where the run was already failing, and says why. Verified both ways — module present
  `PY_GREEN_EXIT=0`; module moved aside `PY_NOPLUGIN_EXIT=2` printing
  `cannot run: this build has no loadable 'audiofileprocessor' module`.

No production source is touched: `LufsMeter.cpp`, `MasterLoudnessTap.cpp`,
`AudioEngine.cpp` and the command surface are byte-identical to the integration tip.

## Final battery (from `build/tests`, `CTEST_JOBS=1`, unpiped)

| test | exit |
| --- | --- |
| `ControlMeterCommands` | `EXIT=0` (1/1 Passed, 13.42 s) |
| `MidiOutQueueTest`, `RetroMidiRingTest` (collateral) | `EXIT=0` (2/2 Passed) |
| `ControlCommandsSnapshot` (untouched) | `EXIT=0` (1/1 Passed) |

## Registration

`tests/control-meter-commands.py` is the existing fork test its ctest registers
(`tests/CMakeLists.txt:2879`), and this commit adds no new file, so no
`tests/fork-sources.txt` / `tests/upstream-modifications.txt` entry is owed. No
`tests/CMakeLists.txt` edit was made either — it is a contended file owned by the parent —
so the prerequisite is named in the test and in its docstring instead of in the ctest
comment.

## Unverified

* That a *fresh* build tree produces `audiofileprocessor` before the battery runs: a full
  build does; the targeted builds this lane uses do not, which is how the module came to
  be missing in the first place.
* That the CI runner's plugin directory is populated — CI builds the default target set,
  which is a different question from this tree's state.
