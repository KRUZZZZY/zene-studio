FIXED

new-p15-125: Zero tempo reaches a division by zero (latent: no product path reaches it today)

Decision: REAL, latent. The finding's path is not reachable from the product, but the encoder is unguarded on its own input domain, so the defect as written is real in the function.

- The encoder divides by the tempo without a check and converts the result to `uint32_t`: `plugins/MidiExport/MidiFile.hpp:157` (`const auto usecs = uint32_t(60000000.0 / tempo + 0.5);`). With `tempo == 0` the quotient is +inf, and an infinite value has no `uint32_t` value, so the conversion is undefined behaviour.
- The reported trigger, a zero tempo reaching `addTempo()`, does not occur on any product path. The only tempo sources are:
  - the global tempo, `Song::getTempo()` = `m_tempoModel.value()` (`src/core/Song.cpp:1459-1461`), an `IntModel` ranged `MinTempo..MaxTempo` = 10..999 (`src/core/Song.cpp:122`, `include/Song.h:78-80`), so the model cannot hold 0;
  - the tempo map, whose every event enters through `TempoMap::addEvent()` -> `validEvent()` -> `tempoInRange()` (`src/core/TempoMap.cpp:97-101`, `:135-137`, bounds `include/TempoMap.h:46-47`), and `TempoMap::set()` and `loadSettings()` both go through `addEvent()` (`src/core/TempoMap.cpp:189-204`, `:366-404`).
  - The only caller of the encoder is `plugins/MidiExport/MidiExport.cpp:333` (`mtrack.addTempo(event.tempo, tick)`), fed by `interchange::exportEvents()` with `Song.cpp:3011` as the only `tryExport` call site.
  - The sibling writer already documents this invariant: `src/core/SmfInterchange.cpp:113-115` ("The engine's own bounds keep bpm >= 10").
- The finding's path and line were stale: the file is `plugins/MidiExport/MidiFile.hpp` (not `.h`), and the division is at line 157.

Fix (at the single entry point): `plugins/MidiExport/MidiFile.hpp:215-220`. `MIDITrack::addEvent()` refuses a `TEMPO` event whose tempo is 0. It is the only `events.push_back` in the class, so both `addTempo()` and a direct `addEvent()` are covered. Refusing in `writeToBuffer` instead would drop bytes but keep the event in the sorted list, and the delta times of the following events would be computed from a time that was never written, so the guard is in `addEvent`. Non-tempo events and every tempo from 1 to 2^32-1 that is nonzero encode exactly as before.

Regression: `tests/src/core/MidiExportTest.cpp:570-585`, `MidiExportTest::aZeroTempoIsNotEncoded()`, in the existing export test file for this area. It includes the header by relative path (`tests/src/core/MidiExportTest.cpp:62`, same pattern as `tests/src/plugins/WatsynParamHandoffTest.cpp:23`). It calls `addTempo(0, 0)`, then a direct `addEvent()` with a zero `TEMPO` event, then `addTempo(120, 480)`, and asserts the encoded bytes equal exactly the valid change: `83 60 FF 51 03 07 A1 20`. Injected values only: no clock, no sleep, no thread.

## Commands and results

All builds used `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp/ccache-tmp-p15-125` (see the note at the end). Logs are in the LOG folder `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-125.r1/logs`.

| Step | Command (run from the worktree unless noted) | EXIT | Log |
| --- | --- | --- | --- |
| Baseline build (first try, no CCACHE_TEMPDIR) | `cmake --build build --target TempoMapTest -j4` | 2 (ccache temp dir not writable) | build-1.log |
| Baseline build | `CCACHE_TEMPDIR=... cmake --build build --target TempoMapTest -j4` | 0 | build-2.log |
| Baseline test (refusal of tempo 9 and 1000 at TempoMapTest.cpp:461-466) | `cd build/tests && ctest -R '^TempoMapTest$' --output-on-failure` | 0 (1 of 1 passed) | test-1.log |
| RED build (test added, fix not yet) | `CCACHE_TEMPDIR=... cmake --build build --target MidiExportTest -j4` | 0 | build-3-red.log |
| RED test | `cd build/tests && ctest -R '^MidiExportTest$' --output-on-failure` | 8 (`FAIL! : MidiExportTest::aZeroTempoIsNotEncoded() 'bytes == expected' returned FALSE`; 12 passed, 1 failed) | test-2-red.log |
| Fix applied; build | `CCACHE_TEMPDIR=... cmake --build build --target midiexport MidiExportTest -j4` | 0 | build-4-green.log |
| GREEN test | `cd build/tests && ctest -R '^MidiExportTest$' --output-on-failure` | 0 (100% passed) | test-3-green.log |
| 4b: save fix | `git diff -- plugins/MidiExport/MidiFile.hpp > .../logs/fix.patch` | 0 | (fix.patch) |
| 4b: revert fix | `git apply -R .../logs/fix.patch` | 0 | |
| 4b: rebuild reverted | `cmake --build build --target midiexport MidiExportTest -j4` | 0 | build-5-revert.log |
| 4b: test reverted (must FAIL) | `cd build/tests && ctest -R '^MidiExportTest$' --output-on-failure` | 8 (same assertion) | test-4-reverted.log |
| 4b: reapply fix | `git apply .../logs/fix.patch` | 0 | |
| 4b: rebuild reapplied | `cmake --build build --target midiexport MidiExportTest -j4` | 0 | build-6-reapplied.log |
| 4b: test reapplied (must PASS) | `cd build/tests && ctest -R '^MidiExportTest$' --output-on-failure` | 0 | test-5-reapplied.log |
| Family build (the family's other two targets were not built, so they showed "Not Run") | `cmake --build build --target SmfInterchangeTest SmfInterchangeRoundTripTest -j4` | 0 | build-7-smf.log |
| Family run | `cd build/tests && ctest -R 'MidiExport\|SmfInterchange' --output-on-failure` | 0 (3 of 3 ctest entries) | test-7-family.log |
| Family verbose | `cd build/tests && ctest -R 'MidiExport\|SmfInterchange' -V` | 0 | test-8-family-verbose.log |

Family test-function count, from the verbose run: SmfInterchangeTest 13 passed, SmfInterchangeRoundTripTest 18 passed, MidiExportTest 13 passed (includes `aZeroTempoIsNotEncoded` PASS). Total 44 passed, 0 failed. Nonzero, so not zero-tests.

Gates. `feature_check.py` could not run in this sandbox: it creates `<worktree>-feature-check/` beside the worktree (`feature_check.py:71`), which is outside the write-jail, and it fails with `PermissionError` (`gates-1.log`, exit 1). I ran the same gate commands it runs, individually, with logs in the LOG folder (final run, after the last edit):

| Gate | Command | EXIT |
| --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 |
| check-namespace | `tests/scripted/check-namespace` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 (6 pre-existing REGRESSION lines, none names `MidiFile.hpp` or `MidiExportTest.cpp`) |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 (22 pre-existing REGRESSION lines, none names the two changed files) |

The two ratchet lines are red on the base, not caused by this branch. By the script's rule (a red item counts only when it is new in a file the branch changed) they do not fail this change, but I did not get the script's own table, so this is by reading, not by running the script.

## Self-check

- **Regression fails without the fix and passes with it (exit codes):** RED `ctest -R '^MidiExportTest$'` EXIT 8 with the unfixed code (test-2-red.log, and again test-4-reverted.log after the 4b revert). GREEN EXIT 0 (test-3-green.log, test-5-reapplied.log).
- **Deterministic:** the test feeds fixed integers into the buffer encoder. No clock, sleep, thread or file. The expected bytes are computed by hand: 480 ticks = VLQ `83 60`; 120 bpm = 60000000/120 = 500000 = `07 A1 20`.
- **Fix at the cause, complete:** the one `events.push_back` in `MIDITrack` is in `addEvent` (`MidiFile.hpp:218-219`), so the guard covers `addTempo` (the production caller `MidiExport.cpp:333`) and any direct `addEvent`. The sibling interchange writer (`SmfInterchange.cpp:113-115`) relies on the same upstream bound and is not changed.
- **No collateral damage:** the guard only changes output for a TEMPO event with tempo 0, which no product path produces. Every nonzero tempo encodes byte-identically (the unchanged `MidiExportTest` tempo-map test and `SmfInterchangeTest::theTempoEncodingIsExactForEveryAcceptedBpm`, 44 passing across the family). No reader, project format or saved data changes. No limit was added.
- **Verdict evidence (REAL-latent):** `MidiFile.hpp:157` (the division), `MidiFile.hpp:215-220` (the guard). The reachability reading is in the Decision section above, with file:line for each guard upstream.
- **Realtime, undo, commands:** the MIDI writer runs only on export (`Song.cpp:3011`), never on the audio thread. No allocation was added on any audio path (the guard is a compare and return). No undo entry and no control command.
- **Registration:** no new files. `plugins/MidiExport/MidiFile.hpp` is already listed in `tests/upstream-modifications.txt:2200`. `tests/src/core/MidiExportTest.cpp` is already in `tests/all-sources.txt:2809` and the fork list. `all-sources-reproduce` EXIT 0.
- **Not addressed (out of scope, unreachable):** a tempo of 1 to 3 bpm gives microseconds above 0xFFFFFF, which `writeToBuffer` truncates to three bytes. The engine bounds are 10..999 (`TempoMap.h:46-47`), so no product path gets there. Left as is.

## Note on the build environment

The default ccache temp directory (`.../dsh/home/ccache-tmp-r6`) returns "Permission denied" from this sandbox, so the first builds failed with EXIT 2 (build-1.log). I set `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp/ccache-tmp-p15-125` for every later build. Compile flags and sources are unchanged; only the ccache scratch location moved.
