Verdict: FALSE-POSITIVE

new-p23-143: "Lv2 instrument auxiliary output buses read the wrong ports" does not hold on `bugs/qwen-new-p23-143`.
`deliverOutputBuses` uses `left = 2 * (bus - 1)` as an index into the Lv2 *extra-output* list, not into the plug-in's flat
port list. The recorded offset `2 + 2 * (bus - 1)` is a flat-channel number, which the Lv2 path never uses as an index.
The current mapping is correct, and the finding's proposed fix would break it. No product code was changed.

## Evidence (file:line on this branch)

1. **Where the finding points now.** The finding's line 236 is `copyBuffersToCore(out.data(), out.size())` for the main
   pair (`plugins/Lv2Instrument/Lv2Instrument.cpp:236`). The aux mapping it describes is at
   `plugins/Lv2Instrument/Lv2Instrument.cpp:266` (`left = 2 * (bus - 1)`), with the copies at lines 268-269.
2. **What "extra" means.** `src/core/lv2/Lv2Proc.cpp:1083-1094`: the first two audio outputs of a plug-in go to the main
   pair `m_outPorts`; every later audio output goes to `addExtraPort` (line 1093).
3. **The extra list is extra-relative.** `src/core/lv2/Lv2Proc.cpp:393-395` (`addExtraPort` pushes into
   `m_extraOutPorts` in port order). `:401-404` `extraOutputCount()` returns `m_extraOutPorts.size()`.
   `:409-412` `copyExtraOutputToCore(index, ...)` reads `m_extraOutPorts[index]`. So index 0 is the first aux port, which
   is flat port 2.
4. **Contract in the header.** `include/Lv2Proc.h:166-168`: extra outputs are "past its main pair", and
   `copyExtraOutputToCore` "Copies extra output @a index".
5. **Publish side.** `plugins/Lv2Instrument/Lv2Instrument.cpp:245-251`: aux bus `k` (k = 1, 2, ...) takes extra ports
   `2(k-1)` and `2(k-1)+1`, and records `firstChannel = 2 + port` where `port = 2(k-1)`. `include/InstrumentOutputBus.h:39`
   defines `firstChannel` as "its first channel in the plug-in's flat output". So the recorded value is the flat index,
   and `2 * (bus - 1)` is the extra index for the same port. The two agree: extra index `i` is flat channel `2 + i`.
6. **The finding's symptom cannot occur from this code.** `copyExtraOutputToCore` only reads `m_extraOutPorts`, which never
   contains a main-pair port, so the main pair (or silence) cannot reach an aux bus through this path. Bus 1 reads extra
   index 0/1, which is flat 2/3, its own pair.
7. **Only the flat-index consumers use `firstChannel`.** `src/core/InstrumentOutputBuses.cpp:154-158` (route activity),
   `:236` (routedIndex) and `:278-281` (`deliver()` indexing `channels[firstChannel]`). `deliver()` is called only by
   `plugins/ClapInstrument/ClapInstrument.cpp:334` and `plugins/Vst3Instrument/Vst3Instrument.cpp:430`, not by the Lv2
   instrument, which calls `routedBuffer` and `send` directly (`Lv2Instrument.cpp:264-270`).
8. **Callers of the index.** `copyExtraOutputToCore` is called only at `plugins/Lv2Instrument/Lv2Instrument.cpp:268-269`
   (`git grep -n copyExtraOutputToCore -- plugins include src`).

## What the proposed fix would do

Take a plug-in with two stereo aux buses, so 4 extra ports at flat ports 2-5. Publish records Aux1 `firstChannel = 2` and
Aux2 `firstChannel = 4`. With `left = 2 + 2 * (bus - 1)`:

- bus 1: `left = 2`, which reads extra index 2 (flat port 4). That is Aux2's left, so Aux1 would play Aux2's audio.
- bus 2: `left = 4`, which is past `m_extraOutPorts.size() == 4`. `copyExtraOutputToCore` does nothing, so `send` mixes a
  stale block into Aux2.

That is a regression. The current code is correct, so the fix is not applied.

## Not verified here

- No runtime check. No LV2 plug-in with more than two outputs is available to the tests. The Lv2 ctest that is
  registered (`Lv2InstrumentKeyRegressionTest`, `tests/CMakeLists.txt:3090-3095`) runs only the key-refusal slots of
  `ControlDeviceCatalogueTest`, and does not touch bus mapping. The `^Lv2Instrument$` family named in the brief does not exist.
  I did not build or run it, because it cannot bear on this finding.
- `publishOutputBuses` runs once, from the constructor (`Lv2Instrument.cpp:115`). I did not check whether the `Lv2Proc`
  can be replaced later without republishing the layout. That would be a separate question.

## Commands run and exit codes

| Step | Command | EXIT |
| --- | --- | --- |
| Lv2 test family list | `cd build/tests && ctest -N -R 'Lv2' > logs/000-ctest-list-lv2.log 2>&1` | 0 (lists `Lv2InstrumentKeyRegressionTest`, #621) |
| Feature gate, as the brief specifies | `python3 .../qwen/feature_check.py <worktree> > logs/001-feature-check-before-report.log 2>&1` | 1. Not a gate result: the script's `main()` cannot create its log folder `<worktree>/../zene-qwen-new-p23-143-feature-check` (`PermissionError`, outside the write-jail). |
| Feature gate checks, same logic, logs in the LOG FOLDER | `python3 -I -c "<same steps as feature_check.main(), with logs in logs/feature-check/>" > logs/002-feature-check-gate-functions.log 2>&1` | 0. Rows: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures all PASS (exit 0); complexity and file-length PASS (no new item in changed files); RESULT PASS. Per-gate logs are in `logs/feature-check/`. |

Not run, and why: the RED and GREEN builds and ctest runs (no product change to test), and the step 4b reverse-apply
(nothing to reverse).

## Self-check

1. **Regression fails with the change reverted, passes with it.** Not applicable. No product change was made, so there is
   no RED/GREEN pair. The finding's own fix was not applied because it is wrong (see "What the proposed fix would do").
2. **Test is deterministic.** Not applicable. No test was added or changed.
3. **Fix at the cause, every caller and sibling checked.** Not applicable. The cause is not present. The only caller of
   `copyExtraOutputToCore` and of the `2 * (bus - 1)` index is `Lv2Instrument.cpp:266-269` (evidence 8). Its siblings are
   the publish side (`:245-251`) and the Clap/VST3 `deliver()` path (evidence 7), and all three agree on the mapping.
4. **No collateral damage.** No code changed, so old projects, saved data and other buses are unaffected.
5. **Realtime, undo, commands.** No change. `deliverOutputBuses` and `copyExtraOutputToCore` are untouched, and no
   command was added.
6. **Verdict evidence.** Given above at `Lv2Instrument.cpp:266-269` and `:245-251`, `Lv2Proc.cpp:393-412` and
   `:1083-1094`, `Lv2Proc.h:166-168` and `InstrumentOutputBus.h:39`.
