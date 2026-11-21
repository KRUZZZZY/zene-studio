# Cluster new-p10-3 — CLAP instrument view output count

**Verdict: FIXED**

## Finding and change

The view's output label is refreshed from `ClapInstrument::outputLayoutChanged()` at
`plugins/ClapInstrument/ClapInstrumentView.cpp:77-88`. `ClapInstrument::reprepare()` emits that signal
when the host reads a changed audio-port layout at `plugins/ClapInstrument/ClapInstrument.cpp:218-228`,
declared at `plugins/ClapInstrument/ClapInstrument.h:137`.

The regression test is registered as product-new in `tests/fork-sources.txt:2454-2455`. Its claim is
now limited to the reprepare behavior it exercises. The fixture requests `CLAP_AUDIO_PORTS_RESCAN_LIST`
and a restart from `activate()`. The test calls `reprepare()` directly: the first call makes the
announcement and `prepare()` clears that restart request; the second consumes the inactive rescan and
reads the four-channel layout. Assertions at `tests/src/plugins/ClapInstrumentViewTest.cpp:128-142`
check both phases and the refreshed label. This test does not claim to cover `poll()` automatically
servicing a runtime restart.

## Evidence

The RED proof used only the three product files from commit `e7c5f44be` as `fix.patch`; the CLAP host
rescan support and fixture remained present. The test failed on the stale-label assertion without those
product changes and passed after restoration.

| Step | Command | Result |
|---|---|---|
| Save product-only patch | `git diff e7c5f44be^ e7c5f44be -- plugins/ClapInstrument/ClapInstrument.cpp plugins/ClapInstrument/ClapInstrument.h plugins/ClapInstrument/ClapInstrumentView.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-3.r3/logs/fix.patch` | EXIT 0 |
| Remove product patch | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-3.r3/logs/fix.patch` | EXIT 0 |
| RED build | `cmake --build build --target zene ClapInstrumentViewTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-3.r3/logs/build-01-red.log 2>&1` | EXIT 0 |
| RED test (run from `build/tests`) | `ctest -R '^ClapInstrumentViewTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-3.r3/logs/test-01-red.log 2>&1` | EXIT 8; 1 test failed because the label remained at 2 channels |
| Restore product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-3.r3/logs/fix.patch` | EXIT 0 |
| GREEN build | `cmake --build build --target zene ClapInstrumentViewTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-3.r3/logs/build-02-green.log 2>&1` | EXIT 0 |
| GREEN test (run from `build/tests`) | `ctest -R '^ClapInstrumentViewTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-3.r3/logs/test-02-green.log 2>&1` | EXIT 0; 1/1 passed |
| CLAP-family build | `cmake --build build --target zene clapeffect clapinstrument ClapBusMapTest ClapHostTest ClapLoaderErrorTest ClapEffectIntegrationTest ClapInstrumentViewTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-3.r3/logs/build-03-family.log 2>&1` | EXIT 0 |
| CLAP-family tests (run from `build/tests`) | `ctest -R '^Clap.*Test$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-3.r3/logs/test-03-family.log 2>&1` | EXIT 0; 5/5 passed |
| Fork-source registration gate | `bash tests/fork-sources-gate.sh > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-3.r3/logs/gate-03-fork-sources.log 2>&1` | EXIT 0; all tracked sources registered |
| Complexity ratchet | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-3.r3/logs/gate-04-complexity-final.log 2>&1` | EXIT 1; six unrelated new over-target functions; no changed file listed |
| File-length ratchet | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-3.r3/logs/gate-05-file-length-final.log 2>&1` | EXIT 1; fourteen unrelated new over-limit files; no changed file listed |

## Files in this rework commit

- `tests/fork-sources.txt`
- `tests/src/plugins/ClapInstrumentViewTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-3.md`
