ALREADY-FIXED

# Cluster new-p8-283: unprepared ClapInstrument leaves output untouched

## Evidence
- `plugins/ClapInstrument/ClapInstrument.cpp:320-331`: `processImpl` calls `silenceOutput(out)` in the `!m_plugin.isPrepared()` branch before returning, and still records the reprepare request.
- `plugins/ClapInstrument/ClapInstrument.cpp:112-119`: `silenceOutput` zeroes every frame of every channel; no allocation, lock or log (audio-thread safe).
- Fixed by commit 80368e6bbf ("cluster 5550 - silence the whole output when the plug-in errors or is unprepared"), an ancestor of HEAD (`git merge-base --is-ancestor` printed `ancestor`).
- Existing regression test: `tests/src/plugins/ClapInstrumentViewTest.cpp:289` `unpreparedPluginClearsOutputAndRequestsReprepare` (prefills the planes with kPrefill, releases the plug-in, processes, expects every sample 0 and needsReprepare()).

## Commands (logs in the run log folder)
- `cmake --build build --target zene ClapInstrumentViewTest -j4` > build-1.log: EXIT=0
- `cd build/tests && ctest -R ClapInstrumentView --output-on-failure` > test-1.log: EXIT=0 (1/1 passed)
- `./ClapInstrumentViewTest unpreparedPluginClearsOutputAndRequestsReprepare` > test-2.log: EXIT=0 (PASS, 0 skipped)
- Mutation check (temporarily commented out line 329 `silenceOutput(out);`, uncommitted, reverted with `git checkout --`): rebuild EXIT=0 (build-2.log), test EXIT=1 (test-3-mutant.log): `channel 0 frame 0: 0.5, expected 0`.
- After restoring: rebuild EXIT=0 (build-3.log), full `./ClapInstrumentViewTest` EXIT=0, 9 passed (test-4.log).

No product change was made; the report is the only change. The finding's line numbers (295-302) are historical.
