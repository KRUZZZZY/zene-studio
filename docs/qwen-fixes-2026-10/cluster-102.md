FIXED

# Cluster 102 — browser.preview decodes the entire file before limiting it to 30 seconds

Finding (Luna patrol, BUG 3): `src/core/ControlCommandsBrowserMedia.cpp` applied the 30-second cap
only after `SampleDecoder::decode()` had populated the full audio vector, so previewing a multi-hour
file decoded all of it before discarding everything past 30 s.

## Verdict: REAL (before the fix, in this worktree)

| Evidence | Where |
|---|---|
| the handler decoded the whole file: `std::optional<SampleDecoder::Result> decoded = SampleDecoder::decode(key);` then `decoded->data.resize(std::min(decoded->data.size(), static_cast<std::size_t>(PreviewSeconds * rate)));` | `src/core/ControlCommandsBrowserMedia.cpp:244-253` at base (the `git diff` in `fix.patch` shows the removed lines) |
| `decode()` has no frame limit and fans out to decoders that each read the file to the end | `src/core/SampleDecoder.cpp:393` (`SampleDecoder::decode`), `:145` (`sf_readf_float(sndFile, buf.data(), sfInfo.frames)` in `decodeSampleSF`) |
| a bounded decode already existed for exactly this purpose and reports the file's own length | `include/SampleDecoder.h:65` (`decodeLimited`), `src/core/SampleDecoder.cpp:408` |
| Q050-048 deliberately left the preview on the full decode at the time: "decode() and all existing callers (clips, preview, peaks) are unchanged" | `docs/050-features/Q050-048.md:17` |

The earlier attempt's verdict (`SKIPPED-SCOPE`) was correct about the cause and wrong about there
being no seam: `SampleDecoder::decodeLimited()` (`:408`) and its `LMMS_TESTING` full/limited call
counters (`include/SampleDecoder.h:67-74`, `src/core/SampleDecoder.cpp:434-444`) already existed.

## Fix (product code, 15 lines changed, 1 file)

`preview()` now asks the bounded decoder for the window and plays what it returned
(`src/core/ControlCommandsBrowserMedia.cpp:249-262`):

```cpp
std::optional<SampleDecoder::BoundedResult> decoded = SampleDecoder::decodeLimited(key, PreviewSeconds);
if (!decoded || decoded->window.data.empty() || decoded->window.sampleRate <= 0) { ... refused ... }
const std::vector<SampleFrame>& frames = decoded->window.data;
const int rate = decoded->window.sampleRate;
const PreviewPlan plan = planFor(monoMix(frames.data(), frames.size()), rate, match, key12);
const std::vector<SampleFrame> audio = stretched(frames, plan);
```

The post-hoc `resize()` is gone because the bound is now applied while reading. The DrumSynth
fallback inside `decodeLimited()` still synthesises whole and cuts to the window — a documented
limit of that decoder (`docs/050-features/Q050-048.md`, "Known limits"), unchanged here and not
made worse.

### Callers, siblings and the wider types (checked, consistent)

- `BrowserResultActions::preview` (`src/gui/BrowserResults.cpp:160-172`) is the only UI entry and it
  invokes this same `browser.preview` command — one path, not two.
- `browser.preview_stop` (`:273`) and the generation atomic are untouched.
- `browser.thumbnail` uses `BrowserPeakCache` (a different read path); `browser.load` loads a file
  into a track, where decoding the whole file is the point — both left alone.
- Other `SampleDecoder::decode()` callers (`src/core/SampleBuffer.cpp:95`,
  `src/core/ImportDetection.cpp:128`) are outside this bug and untouched.
- The command's args, result keys, error text and `seconds` (`audio.size() / declaredRate`) are
  unchanged, so no agent-visible contract moved and no saved data is affected (no save format
  touched). No header changed; `zene` was rebuilt anyway.
- Realtime: nothing that runs per period changed. The decode, the tempo/key detection and the
  stretch still run on the control thread before the play handle is created
  (`src/core/ControlCommandsBrowserMedia.cpp:252-265`); `PreviewPlayHandle::isFinished` reads an
  atomic, as before. No allocation, lock or log was added to an audio-thread path.

## Regression test

`tests/src/core/BrowserPreviewWindowTest.cpp` (new binary; `tests/CMakeLists.txt`,
`tests/all-sources.txt`, `tests/fork-sources.txt`). It is a separate file because
`tests/src/core/LibraryBrowserTest.cpp`, which already covers the preview playing and stopping
(`aMatchedPreviewPlaysAndStops`), is at the file-length ratchet: 492 code lines of the 500 allowed,
so the same split used by neighbouring suites was required, not a moved gate (see gates run 1 below).

Deterministic: no clock, no sleeps, no threads. The command is synchronous, so the decoder's own
`LMMS_TESTING` counters are reset immediately before the `browser.preview` invoke and read
immediately after it returns.

Test matrix:

| Path | Slot | Assertion |
|---|---|---|
| normal (long file) | `aLongPreviewDecodesOnlyItsWindow` | a 35 s file: `full == 0` (the whole file is never decoded), `limited >= 1`, `29.0 < seconds <= 30.0`, `playing` true |
| normal (short file) | `aShortPreviewIsPlayedWhole` | a 2 s file: `full == 0` and `seconds == 2.0` — the decode limit does not cut what the file has |
| failure | `anUnreadableFileIsRefusedWithoutAFullDecode` | a text file named `.wav`: typed `Refused`, and `full == 0` |
| exhaustion / reconfiguration | n/a | the window is the compile-time `PreviewSeconds` (`:63`); the handler has no configuration knob, and the decoder's own refusal of bad windows is `SampleDecoderLimitTest` |
| audio thread | n/a | no per-period code changed; the window is built on the control thread (above) |
| platforms | — | the test uses 16-bit PCM WAV through libsndfile, the decoder path every supported platform builds; no Qt-6-only API |

## Commands and exit codes (copied from the logs in the run's log folder)

First attempt, with the test inside `LibraryBrowserTest.cpp` — RED and GREEN, then the gate refused it:

| Command | Exit | Log |
|---|---:|---|
| `cmake --build build --target LibraryBrowserTest -j4` | 0 | `build-1.log` |
| `ctest -R LibraryBrowserTest --output-on-failure` (buggy handler) | **8** — `FAIL! aPreviewDecodesOnlyItsWindow ... Actual (longCalls.full): 1, Expected: 0` | `test-1-red.log` |
| `cmake --build build --target zene LibraryBrowserTest -j4` (fix applied) | 0 | `build-2.log` |
| `ctest -R LibraryBrowserTest --output-on-failure` | **0** | `test-2-green.log` |
| `./LibraryBrowserTest aPreviewDecodesOnlyItsWindow` | 0 — `PASS` | `test-3-green-slot.log` |
| `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | **1** — `FAIL file-length NEW: new file over 500 lines: tests/src/core/LibraryBrowserTest.cpp (529)` | `gates-1.log` |

That FAIL is why the test moved to its own file. Measured with the gate's own count
(`FILE_LINE_LIMIT=10 bash tests/file-length-gate.sh --check`, `gates-3-counts.log`):
`REGRESSION: new file over 10 lines: tests/src/core/LibraryBrowserTest.cpp (492)` — 8 code lines of
headroom, not enough for a fixture-writing test.

Procedure step 4b on the in-file test (revert → fail, reapply → pass):

| Command | Exit | Log |
|---|---:|---|
| `git diff -- src/core/ControlCommandsBrowserMedia.cpp > .../logs/fix.patch` | 0 | (31-line patch) |
| `git apply -R .../logs/fix.patch` | 0 | |
| `cmake --build build --target zene LibraryBrowserTest -j4` | 0 | `build-4-revert.log` |
| `ctest -R LibraryBrowserTest --output-on-failure` | **8** — `Actual (longCalls.full): 1` | `test-5-revert.log` |
| `git apply .../logs/fix.patch` | 0 | |
| `cmake --build build --target zene LibraryBrowserTest -j4` | 0 | `build-5-reapply.log` |
| `ctest -R LibraryBrowserTest --output-on-failure` | **0** | `test-6-reapply.log` |

The same 4b loop on the delivered test (`tests/src/core/BrowserPreviewWindowTest.cpp`):

| Command | Exit | Log |
|---|---:|---|
| `bash tests/all-sources-reproduce.sh` (new file registered) | 0 — `REPRODUCES` | `gates-4-all-sources.log` |
| `cmake -S . -B build` | **97** — `bunny-guard: configuring is the parent's job` (expected; the parent owns configuration) | `configure-1.log` |
| `cmake --build build --target LibraryBrowserTest -j4` (regenerates CMake's cache) then `ctest -N -R BrowserPreviewWindowTest` | 0 — `Test #383: BrowserPreviewWindowTest` | `build-7-probe.log` |
| `git apply -R .../logs/fix.patch` | 0 | `test-7-red.log` head |
| `cmake --build build --target BrowserPreviewWindowTest -j4` (buggy handler) | 0 | `build-8-red.log` |
| `ctest -R BrowserPreviewWindowTest --output-on-failure` | **8** — 3 failures, each `full: 1, Expected: 0` (lines 114, 133, 149) | `test-8-red.log` |
| `git apply .../logs/fix.patch` | 0 | `build-9-green.log` |
| `cmake --build build --target zene BrowserPreviewWindowTest -j4` | 0 | `build-9-green.log` |
| `ctest -R BrowserPreviewWindowTest --output-on-failure` | **0** — `100% tests passed out of 1` | `test-9-green.log` |
| `ctest -R 'LibraryBrowserTest\|BrowserPreviewWindowTest\|ControlBrowserCommandsTest\|BrowserTempoKeyTest\|BrowserFeaturesWindowTest\|SampleDecoderLimitTest\|SampleDecoderOggChannelTest\|SampleDecoderShortReadTest\|SampleDecoderOggStateTest'` | **0** — 9/9 | `test-10-family.log` |

Gates, final (`python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>`, unpiped):

| Gate | Exit |
|---|---:|
| fork-sources | 0 |
| all-sources-reproduce | 0 |
| no-upstream-regression | 0 |
| check-namespace | 0 |
| no-tautology | 0 |
| doc-figures | 0 |
| complexity (no new item in your files) | 0 |
| file-length (no new item in your files) | 0 |
| `RESULT: PASS` / script exit | **0** |

Log: `gates-5.log`.

## Self-check

Answers to the pre-submit check's ASK items, each with the evidence in this report.

- **The regression test FAILS with the product change reverted and PASSES with it.** Delivered test:
  with `git apply -R logs/fix.patch` in place, `cmake --build build --target BrowserPreviewWindowTest -j4`
  (exit 0) and `ctest -R BrowserPreviewWindowTest --output-on-failure` gave **exit 8** with three
  failures, each `Actual (…full): 1 / Expected: 0` (`test-8-red.log`); after `git apply logs/fix.patch`,
  `cmake --build build --target zene BrowserPreviewWindowTest -j4` (exit 0) and the same ctest gave
  **exit 0**, `100% tests passed out of 1` (`test-9-green.log`). The same loop was run first on the
  in-file version of the test (`test-5-revert.log` exit 8, `test-6-reapply.log` exit 0).
- **The test is deterministic: no wall-clock timing, sleeps or races.** The seam is
  `SampleDecoder::resetDecodeCallCountsForTesting()` and `SampleDecoder::decodeCallCountsForTesting()`
  (`include/SampleDecoder.h:67-74`, `src/core/SampleDecoder.cpp:434-444`), read immediately around a
  *synchronous* `ControlRegistry::instance()->invoke()` (`tests/src/core/BrowserPreviewWindowTest.cpp:110-115`).
  There is no timer, no sleep, no thread and no tolerance window; the only numeric tolerance is a
  range on the played length (`29.0 < seconds <= 30.0`), which is a contract, not a duration.
- **The fix is at the cause and complete: every caller and sibling checked.** See "Callers, siblings
  and the wider types" above: the only caller of `preview()` is `browser.preview` through the
  registry (`src/gui/BrowserResults.cpp:160-172`), the whole-file `decode()` call is gone from the
  handler, `decodeLimited()`'s return type (`BoundedResult::window`, `totalFrames`) is used
  correctly, and the other `SampleDecoder::decode()` callers (`SampleBuffer.cpp:95`,
  `ImportDetection.cpp:128`) are untouched and outside this bug.
- **No collateral damage.** Old projects and saved data load as before: no serialisation, save
  format, command schema or result key changed, and `browser.preview` creates no project state. The
  30 s limit is the same figure the handler already enforced (only its position moved), so no other
  caller is newly rejected; a 2 s file is still played whole (slot `aShortPreviewIsPlayedWhole`), and
  the sibling suite `SampleDecoderLimitTest`, `BrowserFeaturesWindowTest`, `LibraryBrowserTest`,
  `ControlBrowserCommandsTest`, `BrowserTempoKeyTest` and the three `SampleDecoderOgg*/ShortRead`
  suites all pass (`test-10-family.log`, 9/9, exit 0).
- **ALREADY-FIXED / FALSE-POSITIVE evidence.** Not applicable: the verdict is FIXED. The code that
  showed it was REAL is quoted in "Verdict" above (`src/core/ControlCommandsBrowserMedia.cpp:244-253`
  at base, `src/core/SampleDecoder.cpp:393`, `:145`).
- **Sibling commands in the changed command file, and the rest of the browser group.** Only
  `src/core/ControlCommandsBrowserMedia.cpp` changed, and only its `preview()` handler
  (`:244-265`). None of the others go through `SampleDecoder::decode()`:

| Sibling | Where it lives | Why it stays consistent |
|---|---|---|
| `browser.preview` | `ControlCommandsBrowserMedia.cpp:244` | the fixed handler: reads the window, plays it, reports the same keys |
| `browser.preview_stop` | `:273` | bumps the generation atomic; no decode, unchanged |
| `browser.thumbnail` | `:116` | draws from `BrowserPeakCache`, writes a PNG; no `SampleDecoder` call, unchanged |
| `browser.load` | `:315` | builds a track/clip; loading a file whole is its job (the clip must play all of it), unchanged |
| `browser.browse`, `browser.history`, `browser.history_go` | `ControlCommandsBrowserNavigation.cpp:158` | history records and re-runs a query; no audio decode |
| `browser.categories`, `browser.category.set`, `browser.category.clear` | `ControlCommandsBrowser.cpp:339`, `ControlCommandsBrowserTags.cpp:222-270` | tag/category store edits, one undo step each; no audio decode |
| `browser.collections`, `browser.collection_save`, `browser.collection_run`, `browser.collection_remove` | `ControlCommandsBrowserCollections.cpp:207` | saved query definitions; a run re-runs the query, no audio decode |
| `browser.index_query`, `browser.index_status` | `ControlCommandsBrowserIndex.cpp:207-243` | read the library index; measurements come from `BrowserFeatures::analyse` (`decodeLimited`, unchanged) |
| `browser.peaks`, `browser.peaks_start` | `ControlCommandsBrowser.cpp:360`, `:393` | waveform peak cache; no `SampleDecoder::decode()` |

- **Undo / control commands.** No control command was added or removed, no undo step touched
  (`browser.preview` is session audio and records nothing). The ControlCommandsSnapshot canary is
  therefore unmoved — the `complexity`/`file-length` gates report "no new item in your files" and
  `ControlBrowserCommandsTest` (the surface: schemas, typed refusals, the A16 undo proof) passes.
- **Registration.** The new test is in `tests/CMakeLists.txt`, `tests/all-sources.txt` (the recipe
  reproduces, `gates-4-all-sources.log`) and `tests/fork-sources.txt` (with a note naming the
  cluster); the changed inherited file `tests/CMakeLists.txt` is in `tests/upstream-modifications.txt`
  with its reason. No product file was added; the changed product file was already registered
  (`tests/fork-sources.txt:1720`).
- **GUI.** No `src/gui` change, so no screenshot/recording artifact. The preview's GUI entry
  (`BrowserResultActions::preview`) calls the same command and its behaviour is unchanged.
- **Platforms.** Libsndfile WAV, the path every supported platform builds; the test uses no
  Qt-6-only API, no `QString::midRef`, and never sets `LMMS_PLUGIN_DIR`.
- **What is not verified here.** A real multi-hour file was not previewed by hand; the claim rests on
  the decoder's counters and on `decodeLimited()`'s implementation (its own suite covers a 10-minute
  file in `BrowserFeaturesWindowTest`). TSan does not run on this box, so the audio-thread reasoning
  above is static, not measured.

## Other findings

- `SampleDecoder::decodeLimited()`'s DrumSynth branch still synthesises the whole file before cutting
  it to the window (`src/core/SampleDecoder.cpp:420-431`); a preview of a very long `.ds` file can
  still grow with the enclosing duration. Documented as a known limit of Q050-048; not touched here.
- `docs/050-features/Q050-048.md:17` still states that the preview is unchanged; it is the record of
  that feature, so it was left as written rather than rewritten by this cluster.
