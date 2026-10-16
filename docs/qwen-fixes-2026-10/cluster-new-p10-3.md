# Cluster new-p10-3 — the CLAP instrument view follows output layout changes

**Verdict: FIXED**

## Finding and change

`plugins/ClapInstrument/ClapInstrumentView.cpp:77-88` refreshes the audio-output label on
`ClapInstrument::outputLayoutChanged()`. `plugins/ClapInstrument/ClapInstrument.cpp:218-228`
emits that signal after `reprepare()` applies a changed layout. The signal is declared at
`plugins/ClapInstrument/ClapInstrument.h:137`. This preserves the view-refresh behavior from the
first commit.

The review found that the fixture announced its added port with `rescan(..., 0)`, while the host
did not set `is_rescan_flag_supported`. `plugins/ClapEffect/ClapHostInternals.h:406-421,535` now
advertises the six CLAP audio-port rescan flags handled by its deferred inactive scan. At
`tests/data/clap-test-plugin/clap-test-instrument.c:464-475,665`, the fixture checks support for
`CLAP_AUDIO_PORTS_RESCAN_LIST` and requests that flag when the new port appears in `activate()`
(`!active_state`), then requests a restart. The test in
`tests/src/plugins/ClapInstrumentViewTest.cpp:102-137` checks that the real instrument layout and
displayed count both change from 2 to 4.

## Evidence

The product-only proof used the prior commit's three view-refresh files as `fix.patch`; host rescan
support and fixture changes stayed present in both runs. The test failed without those product
hunks on the stale-label assertion, then passed after they were restored.

| step | command | result |
|---|---|---|
| Save product-only patch | `git diff HEAD^ HEAD -- plugins/ClapInstrument/ClapInstrument.cpp plugins/ClapInstrument/ClapInstrument.h plugins/ClapInstrument/ClapInstrumentView.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-3.r2/logs/fix.patch` | EXIT 0 |
| Product-only RED setup | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-3.r2/logs/fix.patch` | EXIT 0 |
| RED build | `cmake --build build --target ClapInstrumentViewTest -j12` | EXIT 0 (`build-1.log`) |
| RED test | `cd build/tests && ctest -R '^ClapInstrumentViewTest$' --output-on-failure` | EXIT 8; label stayed at `2 channel(s)` (`test-1-red.log`) |
| Restore product-only patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-3.r2/logs/fix.patch` | EXIT 0 |
| Targeted GREEN build | `cmake --build build --target zene ClapInstrumentViewTest -j12` | EXIT 0 (`build-2-green.log`) |
| Targeted GREEN test | `cd build/tests && ctest -R '^ClapInstrumentViewTest$' --output-on-failure` | EXIT 0; 1/1 passed (`test-2-green.log`) |
| CLAP family build | `cmake --build build --target ClapBusMapTest ClapHostTest ClapLoaderErrorTest ClapEffectIntegrationTest ClapInstrumentViewTest -j12` | EXIT 0 (`build-3-family.log`) |
| CLAP family tests | `cd build/tests && ctest -R '^Clap.*Test$' --output-on-failure` | EXIT 0; 5/5 passed (`test-3-family.log`) |
| Complexity ratchet | `bash tests/complexity-gate.sh --check` | EXIT 1; six unrelated new over-target functions; no changed file from this fix is listed (`gate-1-complexity.log`) |
| File-length ratchet | `bash tests/file-length-gate.sh --check` | EXIT 1; fourteen unrelated new over-limit files; no changed file from this fix is listed (`gate-2-file-length.log`) |

Both ratchet failures are existing baseline drift on this branch. The checks did not list the new
host callback or any file changed for this fix; no gate limit or baseline was changed.

## Files in this follow-up commit

- `plugins/ClapEffect/ClapHostInternals.h`
- `tests/data/clap-test-plugin/clap-test-instrument.c`
- `tests/src/plugins/ClapInstrumentViewTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-3.md`
