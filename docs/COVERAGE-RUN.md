# COVERAGE-RUN — Gate 2's first measurement on the post-alpha scope

**Date:** 2026-09-12 · **Branch:** `post-alpha/coverage-run` (off `post-alpha/integration`,
`1ef366607`) · **Worktree:** `zene-pa-coverage` · **Lane:** coverage measurement, release
critical path.

This is the first time Zene Studio's own test coverage was measured on the current scope, and
the first time `tests/coverage-gate.sh` was run against a **real** tracefile produced by a real
instrumented build. The independent audit's note — *"No feature lane produced a coverage
measurement (Gate 2), and four of them did not say so"* — is answered here with a number.

---

## 1. Verdict

| item | result |
|---|---|
| **Measured fork-scope line coverage** | **81.46 % (3747 / 4600 lines) over 61 files with a record** |
| Baseline-scope line coverage (the 39 of 47 baseline files that still report) | 76.19 % (2298 / 3016) |
| Whole-tree manifest scope (same capture; 961 of 1113 manifest entries) | 36.44 % (28393 / 77913) |
| Gate 2 (`tests/coverage-gate.sh … --check`) | **exit 1 — FAIL** |
| `tests/run-all-gates.sh --with-coverage` | **exit 1 — FAIL** (gate 2 FAIL, gate 5 FAIL, gate 1 SKIP) |
| Test suite before the capture | **33/33 passed**, 89.64 s, 0 failures |
| Scope actually measured over | `tests/fork-sources.txt` = **130 entries**; of those, **61 were instrumented** |
| Files at 0.00 % | 18 (529 instrumented lines never executed) |
| Scope entries the instrumentation never saw | **69 of 130** |

Gate 2 is **not green**, and it is not green for real reasons: two new files entered at 0.00 %,
below the 50.00 % entry floor, and three baseline files report a fall. The ratchet target (85 %)
is *met by the headline* on the files that were measured — but the headline is over 61 of the
130 files in scope, so it is not a claim about the scope.

**The same numbers were produced twice, independently.** The second instrumented run (inside
`run-all-gates.sh --with-coverage`) reproduced every per-file `LF`/`LH` pair exactly: 0 differing
records out of 61, and the identical 81.46 % / 3747 / 4600. Only the `FNDA` execution counts
differ (see §6.5, counter accumulation).

---

## 2. Configuration (and the printed deviation)

The documented path was used verbatim:

```
COVERAGE_JOBS=4 bash tests/run-coverage.sh build-coverage -DWANT_QT6=ON
```

which expands to

```
cmake -B build-coverage -S . -DCMAKE_BUILD_TYPE=Debug -DWANT_QT6=ON -DWANT_COVERAGE=ON
cmake --build build-coverage -j4
ctest --output-on-failure --test-dir build-coverage/tests
lcov --capture --directory build-coverage --gcov-tool /usr/bin/gcov …
then one lcov --extract per tests/fork-sources.txt entry, accumulated with lcov --add-tracefile
```

* **`CMAKE_BUILD_TYPE=Debug`** — set by `run-coverage.sh` itself; the report "only means
  something" from a Debug build.
* **`-DWANT_COVERAGE=ON`** — the coverage instrumentation flag (`CMakeLists.txt:128`, applied to
  `CMAKE_*_FLAGS_DEBUG` at `CMakeLists.txt:890-898` and to the test targets at
  `tests/CMakeLists.txt:62-71`).
* **DEVIATION — `-DWANT_QT6=ON` is passed explicitly.** `run-coverage.sh` does not set it, and
  neither does the command in `tests/QA-GATES.md`; the repo default is `WANT_QT6=OFF` (Qt5).
  **No Qt5 development packages exist on this host** (`pkg-config --modversion Qt5Core` → absent),
  so the literal documented command cannot even configure here. Qt6 is what the CI matrix builds,
  so this run matches CI's toolkit rather than the literal command line. Toolchain: gcc/gcov
  13.3.0, lcov 2.0-1, cmake 3.28.3, Qt 6.4.2, build capped at `JOBS=4`.
* Other flags as resolved by this configure (`build-coverage/CMakeCache.txt`):
  `WANT_WASM=ON` (wasmtime absent), `WANT_STEM_SPLIT=OFF`, `WANT_SESSION_VIEW=OFF`.
* Wall time for the primary run (configure + build + suite + capture): **20 m 47 s**, `JOBS=4`.
* Submodules were initialised locally in this worktree (`git submodule update --init --recursive`)
  before configuring — necessary for any build here, no network fetch was needed.

Environment detail: `docs/coverage-run/environment.txt`.

---

## 3. The scope, as measured — and the state of the manifests

| manifest | entries in the tree | `docs/CONVENTIONS.md` says | brief says |
|---|---|---|---|
| `tests/fork-sources.txt` | **130** | 99 | 125–128 |
| `tests/all-sources.txt` | **1113** | 1,095 | 1,110–1,112 |

Both docs numbers are stale; the tree has moved past the brief's audit figures too. The scope
size this measurement was taken over is **130 fork entries**, of which **61 produced a coverage
record**.

Two defects in the manifests themselves, found while checking the scope:

1. **`tests/all-sources.txt` is stale by 20 tracked files.** Running the header's own documented
   regeneration command (`git ls-files '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hpp' | grep -vE …`)
   yields **1133** entries against the committed 1113, and the committed file is missing 20
   tracked first-party sources that exist, are compiled, and are in `fork-sources.txt`:
   `include/LoudnessReport.h`, `include/LufsMeter.h`, `include/MidiLearn.h`,
   `include/MidiLearnGui.h`, `include/ScriptApiVersion.h`, `include/ScriptConsole.h`,
   `include/ScriptLuaQtTypes.h`, `include/ScriptPackage.h`, `include/SessionModel.h`,
   `src/core/LoudnessReport.cpp`, `src/core/LufsMeter.cpp`, `src/core/MidiLearn.cpp`,
   `src/core/ScriptApiVersion.cpp`, `src/core/ScriptCommandQueue.cpp`, `src/core/ScriptConsole.cpp`,
   `src/core/ScriptPackage.cpp`, `src/core/SessionClip.cpp`, `src/core/SessionModel.cpp`,
   `src/core/SessionModelPrivate.h`, `src/gui/MidiLearnGui.cpp`. So `all-sources.txt` is not a
   superset of `fork-sources.txt`: any gate that reads it is blind to those 20 files. (Gate 9
   `fork-sources-gate.sh` passes on this tree, so it does not check the reverse containment.)
2. **`tests/fork-sources.txt` contains two non-source entries** — `tools/local-ci.sh` and
   `tools/stem-export-demo.py`. They are shell/Python; no C++ instrumentation can ever see them,
   so they are unfalsifiable members of a coverage scope. Proposed, not applied (the manifest is
   not this lane's to narrow): drop them from the *coverage* scope, or record them as
   deliberately uninstrumentable.

---

## 4. The 0 % files

18 files in the fork scope report instrumented lines and **zero** hits — 529 lines the suite
never executes. Every one of them has no registered test: `grep -rn` across `tests/src/` and
`tests/CMakeLists.txt` for `NeuralAmp|Rnnoise|StemSplitController|PinConnector|DpiHelper|MidiLearnGui`
returns **no matches**.

| file | lines at 0.00 % |
|---|---|
| `src/gui/PinConnector.cpp` | 269 |
| `plugins/NeuralAmp/NeuralAmpEffect.cpp` | 88 |
| `plugins/NeuralAmp/NeuralAmpControlDialog.cpp` | 43 |
| `plugins/NeuralAmp/NeuralAmpControls.cpp` | 17 |
| `plugins/NeuralAmp/NeuralAmpControls.h` | 6 |
| `plugins/NeuralAmp/NeuralAmpEffect.h` | 2 |
| `plugins/NeuralAmp/NeuralAmpControlDialog.h` | 2 |
| `plugins/RnnoiseDenoiser/RnnoiseDenoiserEffect.cpp` | 37 |
| `plugins/RnnoiseDenoiser/RnnoiseDenoiserControls.cpp` | 7 |
| `plugins/RnnoiseDenoiser/RnnoiseDenoiserControls.h` | 6 |
| `plugins/RnnoiseDenoiser/RnnoiseDenoiserControlDialog.cpp` | 4 |
| `plugins/RnnoiseDenoiser/RnnoiseDenoiserEffect.h` | 2 |
| `plugins/RnnoiseDenoiser/RnnoiseDenoiserControlDialog.h` | 1 |
| `src/gui/MidiLearnGui.cpp` | 30 |
| `include/DpiHelper.h` | 7 |
| `include/PinConnector.h` | 5 |
| `include/LmmsPolyfill.h` | 2 |
| `include/MidiLearnGui.h` | 1 |

This **confirms the audit's predecessors numerically**: `plugins/NeuralAmp` = 158 instrumented
lines at 0.00 % and `plugins/RnnoiseDenoiser` = 57, exactly the figures recorded. Both plugin
modules are compiled and linked but no test ever loads them.

`src/gui/MidiLearnGui.cpp` (30) + `include/MidiLearnGui.h` (1) are **new since the baseline** and
are the two files Gate 2 refuses on the entry floor. The MIDI-learn *core* is tested
(`src/core/MidiLearn.cpp` 95.45 %, `tests/src/core/MidiLearnTest.cpp` 98.31 %) — the GUI half
arrived with no test at all.

### Files whose coverage fell against the recorded baseline

| file | baseline | now | gate's arithmetic |
|---|---|---|---|
| `include/AudioPorts.h` | 91.43 % | **56.65 %** | 81 of 233 covered lines lost |
| `include/AudioPlugin.h` | 85.48 % | **71.43 %** | 14 of 98 covered lines lost |
| `include/RemotePluginAudioPorts.h` | 100.00 % | **79.52 %** | 17 of 83 covered lines lost |

22 files improved or entered above the floor (2 improved — `src/core/ScriptBindings.cpp` 97.52→97.80,
`src/core/ScriptEngine.cpp` 90.40→90.98 — and 20 new entries, e.g. `src/core/LufsMeter.cpp`
100 %, `src/core/MidiLearn.cpp` 95.45 %, `src/core/ScriptCommandQueue.cpp` 100 %).

**Read the three "regressions" with care — one of them is not attributable to a source edit.**
`git log -1` per file: `include/AudioPlugin.h` last changed 2026-09-10 and
`include/RemotePluginAudioPorts.h` 2026-09-11, both *after* the 2026-09-09 baseline, so those two
drops can be genuine new inline code arriving under-tested. `include/AudioPorts.h` last changed
**2026-09-08**, i.e. before the baseline was taken, so a 34.8-point fall on an unchanged file
cannot be explained by the file's content. The gate compares hit lines against
`round(baseline% × current instrumented-line count)`; for a header whose instrumented-line count
depends on *which TUs are compiled* (template instantiations, inline code), a change in the
compiled TU set moves the denominator without any test changing. Cause **UNVERIFIED** — the
baseline stores only percentages, so the old line count cannot be recovered from the repo. What
is verifiable: the instrumented line count of `AudioPorts.h` in this configuration (233) is the
number the gate divides by, and its unhit lines are template/`AudioPortsRouter<>` paths.

### 8 baseline files dropped out of the ratchet entirely

`include/StemSeparation/{StemSeparator,StemTypes}.h`, `include/StemSplitController.h`,
`src/core/{ExternalProcessStemSeparator,StemJobManager,StemModelStore,StemTrackBuilder}.cpp`,
`src/gui/StemSplitController.cpp`. All eight still exist **and are all in `fork-sources.txt`** —
they produced no record because `WANT_STEM_SPLIT=OFF` (its default), so neither the sources nor
`tests/src/core/{StemJobManager,StemModelStore,StemSplitPipeline}Test.cpp` are compiled at all.
Gate 2 reports them as `removed` and stops watching them (see §6.2).

---

## 5. What the instrumentation never saw — 69 of 130 scope entries

The fork tracefile contains **61** `SF:` records for a **130**-entry scope. Gate 2 never mentions
the other 69: a scope file that produces no record is not a failure, not a warning, and not a
`100 %` — it is simply absent from every number. Classification (script:
`docs/coverage-run/classify-unseen.py`; full output: `unseen-classification.txt`):

| class | count | what it means |
|---|---|---|
| **not-compiled** | 29 | no `.gcno` exists: the file was in no binary this run. Feature-gated or dependency-absent. |
| **header-no-code** | 38 | a header with no executable lines of its own; gcov emits no record, none expected (24 of these belong to the not-compiled modules). |
| **non-source-entry** | 2 | `tools/local-ci.sh`, `tools/stem-export-demo.py` — never instrumentable. |
| compiled-but-never-run | 0 | no file was compiled and then skipped. |

The 29 not-compiled files, by feature gate:

* **CLAP — 7 files** (`plugins/ClapEffect/*.cpp`): CLAP headers absent. Documented
  (`tests/QA-GATES.md`), and enabling it is known to fail to compile.
* **VST3 — 7 files** (`plugins/Vst3Effect/*.cpp`): VST3 SDK absent. Documented.
* **WASM — 5 files** (`plugins/WasmEffect/*.cpp` 3, `src/wasm/Wasm{,Worker}Sandbox/…` 2):
  `WANT_WASM=ON` but wasmtime absent. Documented.
* **Stem separation — 6 files** (`src/core/ExternalProcessStemSeparator.cpp`,
  `OnnxRuntimeStemSeparator.cpp`, `StemJobManager.cpp`, `StemModelStore.cpp`,
  `StemTrackBuilder.cpp`, `src/gui/StemSplitController.cpp`): `WANT_STEM_SPLIT=OFF`. **This is
  the one that changes the ratchet** — six of these had *measured baseline entries* (e.g.
  `StemModelStore.cpp` 62.02 %) which are now gone as `removed`, and `StemTrackBuilder.cpp`
  last measured 90.00 %.
* **Session View — 3 files** (`src/core/SessionModel.cpp`, `SessionClip.cpp`,
  `tests/src/core/SessionModelTest.cpp`): `WANT_SESSION_VIEW=OFF`.
* **`plugins/RnnoiseDenoiser/testdata/rnn_harness.c`** — not built, as documented.

**This is the ratchet's structural gap.** A file the configuration cannot instrument is
indistinguishable, to the gate, from a file that does not exist. 53 % of the fork scope (69 of
130) is outside the measurement, and 14 of those (the stem module) *fell out of the baseline* in
the process. Gate 9 (`fork-sources-gate.sh`) covers only the "in no scope list at all" sibling
failure; nothing covers "in scope but never instrumented".

---

## 6. Gate 2 on a real tracefile: documented vs actual behaviour

Command and unpiped exit code (evidence: `coverage-gate-check.log`):

```
bash tests/coverage-gate.sh build-coverage/coverage/coverage-fork.info --check   →  EXIT=1
```

Output summary: `REGRESSION` ×3 (§4), `FAIL: 2 new file(s) entered below the 50.00 % coverage
entry floor: include/MidiLearnGui.h: 0.00 %, src/gui/MidiLearnGui.cpp: 0.00 %`,
`tracefile line coverage: 81.46 % (3747/4600 lines over 61 files)`,
`entry floor: 50.00 % … (0 declared exemption(s))`,
`baseline-scope line coverage: 76.19 % (2298/3016 lines over 39 files in the baseline)`.

Five controls were run against the **real** tracefile (harness `gate-controls.sh`, output
`gate-controls.log`, **exit 0 — 5 passed, 0 failed**):

| control | expectation | result |
|---|---|---|
| 1 real tracefile vs the repo baseline | exit 1 | exit 1 ✓ |
| 2 real tracefile vs a baseline re-anchored from it | exit 0 | exit 0 ✓ — the gate is not failing for an environmental reason |
| 3 that baseline with `include/AudioPorts.h` raised 20 points | exit 1 | exit 1, `REGRESSION 76.65 % → 56.65 % (47 of 233 lost)` ✓ — inverted check, the gate is not a rubber stamp |
| 4 real tracefile with `src/gui/PinConnector.cpp` rewritten to 0 instrumented lines | the documented `unmeasured` line | ✓ — `unmeasurable … 0 instrumented lines … (recorded as n/a, never as 100%)` **and** `unmeasured … baseline 0.00% but this run reports 0 instrumented lines` |
| 5 real tracefile plus a zero-line record for `include/CrashReporter.h` (a scope file absent from the baseline) | admitted as `n/a`, not refused by the floor | ✓ — reported `unmeasurable`, classified as `new … recorded as n/a`, and **not** listed as below-floor |

**Findings, in order of consequence:**

1. **The zero-instrumented-line rule is inert on this real tracefile.** All 61 records have
   `LF > 0` — **0 files** in scope have zero instrumented lines. So on the real data the
   documented "recorded as `n/a`, never as `100.00%`" path never fires, and every `100.00 %`
   entry in this tracefile is genuine (`LH == LF` on all 23). The rule itself behaves exactly as
   documented when it is given a real-tracefile input — controls 4 and 5 above, which mutate the
   measured tracefile rather than a synthetic tree — so the earlier fix is sound, but the real
   run neither exercises nor contradicts it. **It remains exercised only by fixtures plus these
   two mutations; the real scope has no such file.**
2. **The entry floor fires on real data for the first time** and behaves as documented: it named
   both below-50 % newcomers and failed the gate (exit 1). No divergence.
3. **"Removed sources drop out of the baseline" is not only about deleted sources.** Eight files
   that still exist and are still in `fork-sources.txt` were classified `removed` purely because
   `WANT_STEM_SPLIT=OFF` (its default) did not compile them. The docs describe this rule as if a
   removal meant a deleted source. Consequences a reader of the docs would not expect: (a) the
   ratchet silently stops watching eight files; (b) a **write-mode** run would delete their
   baseline entries permanently — and when the feature is switched on again,
   `src/gui/StemSplitController.cpp` and `include/StemSplitController.h` (both 0.00 % in the
   baseline) would re-enter as *new* files and be **refused by the entry floor**. This lane ran
   `--check` only, so nothing was written.
4. **The hit-line comparison assumes a stable instrumented-line count, which headers/templates
   do not have.** See §4: `include/AudioPorts.h` is unchanged since before the baseline yet is
   reported as an 81-line regression. Either the gate should compare like-for-like (store the
   baseline's `LF` alongside the percentage) or the doc should state that a header's "drop" is
   only meaningful when the compiled TU set is unchanged. Proposed, not applied — the gate was
   not touched.
5. **`run-coverage.sh` does not zero the counters** (`lcov --zerocounters` is never called), so a
   second run inherits the first run's `.gcda` counts: in the umbrella run every `FNDA` value was
   exactly doubled (e.g. `AudioBus::bus()` 5521 → 11038). `LF`/`LH` — and therefore the
   percentage — were unaffected and reproduced exactly, but the tracefile is not byte-reproducible
   and the capture log carries "negative count" warnings attributable to counters shared across
   runs and threads. Worth a `--zerocounters` before `ctest`.
6. **Cosmetic:** the `run-coverage.sh` "Per-directory summary" prints full absolute source paths
   under a column headed `directory` (its `dirname()` helper returns the whole path for an
   absolute input). The numbers are right; the label is wrong.
7. The 18 `0.00 %` files are **not** reported as a class by the gate — it stays silent about any
   file that has no registered test as long as the file already sits at 0.00 % in the baseline.
   Only a *new* 0 % file trips the floor. Baseline files parked at 0.00 % (NeuralAmp,
   RnnoiseDenoiser, `PinConnector`, `DpiHelper`, `LmmsPolyfill`) can stay there indefinitely.

---

## 7. Umbrella gate run

```
bash tests/run-all-gates.sh --with-coverage     →  EXIT=1   (5 m 09 s)
```

| gate | name | result |
|---|---|---|
| 1 | ctest | **SKIP** — no `build/` directory configured in this lane |
| 2 | coverage | **FAIL** (exit 1) |
| 3 | no-tautology | PASS |
| 4 | complexity | PASS |
| 5 | mutation | **FAIL** — `mutation-gate: no build/` (environment, not a product result) |
| 6 | upstream-regression | PASS |
| 7 | file-length | PASS |
| 8 | duplication | PASS |
| 9 | fork-sources | PASS |

Gates 1 and 5 reported non-pass **only because this lane never configured the plain `build/`
directory** that both require — this is an artifact of a coverage-only lane, not a defect found.
Gate 9 exists on this branch and ran (PASS). Gate 2's verdict inside the umbrella run is
identical to the direct invocation, which is the reproducibility evidence in §1.

---

## 8. Committed evidence — where it lives

All evidence is committed on `post-alpha/coverage-run` under **`docs/coverage-run/`**. Nothing
was parked in `/tmp` (the reason this requirement exists is the earlier lane whose `/tmp`
evidence a disk reclaim destroyed).

| file | size | what it is |
|---|---|---|
| `coverage-fork.info` | 256,768 B | **the measured tracefile** — 61 records, the artifact Gate 2 ratchets on |
| `coverage-fork-run-all-gates.info` | 257,698 B | the second independent run's tracefile (same `LF`/`LH`; proof of reproducibility) |
| `coverage-whole-tree.info.gz` | 1,528,795 B | the full lcov capture, gzipped (raw: 15,706,025 B) — every record, before fork filtering |
| `run-coverage.log.gz` | 2,294,671 B | the complete primary run log, gzipped (raw: 41,295,681 B) — configure, build, ctest, capture |
| `run-all-gates.log.gz` | 2,292,843 B | the complete umbrella run, gzipped — all nine gate banners and the summary table |
| `ctest-primary.log` | 3,925 B | the 33-test ctest transcript extracted from the primary log |
| `coverage-gate-check.log` | 2,388 B | Gate 2's full output **and `GATE_EXIT=1`** |
| `gate-controls.sh` / `gate-controls.log` | 6,445 / 1,558 B | the five controls of §6 and their transcript |
| `analyze-tracefile.py` | 5,575 B | per-file totals, 0 % files, scope gaps, baseline deltas |
| `classify-unseen.py` | 3,561 B | the not-compiled / header-no-code / compiled-norun classification |
| `unseen-classification.txt` | 2,757 B | that classification's output |
| `scope-analysis-fork.txt` | 5,768 B | the fork-scope analysis output |
| `environment.txt` | 1,660 B | toolchain, flags, invocation, the Qt6 deviation |

**Deliberately left out for size, and why it is reproducible:** the `genhtml` HTML report
(`build-coverage/coverage/html/`, 3.8 MB across 215 files) — regenerable from the committed
tracefile with one `genhtml docs/coverage-run/coverage-fork.info`; the uncompressed 15.7 MB
capture and the 41 MB raw run log (their gzipped forms are above); and the `build-coverage/`
tree itself (6 GB, ignored by `/build*/`).

Reproduce this measurement:

```
git worktree add ../zene-pa-coverage post-alpha/coverage-run
cd ../zene-pa-coverage && git submodule update --init --recursive
COVERAGE_JOBS=4 bash tests/run-coverage.sh build-coverage -DWANT_QT6=ON     # ~21 min
bash tests/coverage-gate.sh build-coverage/coverage/coverage-fork.info --check
bash docs/coverage-run/gate-controls.sh . docs/coverage-run/coverage-fork.info
```

---

## 9. Limits — what this run does NOT establish

* **The headline is over 61 of 130 scope files.** 81.46 % is a true statement about the files the
  instrumentation reached; it is not a statement about the scope. The 69 unseen entries (§5) are
  outside it, and 8 of them left the baseline.
* **The 85 % aspiration is not satisfied by this run** in any ratchet sense. The measured fork
  figure (81.46 %) is below it, and Gate 2's own criteria test only that coverage does not fall —
  which it does, so the gate fails regardless.
* **Gates 1 and 5 were not evaluated** (no `build/`); a second lane must run them if the umbrella
  verdict is what the release needs.
* **The cause of the `include/AudioPorts.h` drop is UNVERIFIED** (§4). The other two regressions
  follow source changes dated after the baseline.
* **Nothing was written to the baseline.** Gate 2 ran in `--check` mode; the recorded baseline is
  unchanged by this lane. Re-anchoring is the owner's call and needs `--reanchor` with a real
  reason.
* **The manifest was not narrowed and the gate was not weakened.** The two proposals in §3 and
  §6.4 are proposals.
* `ai_kos_search` was not reachable from this lane's toolset, so prior-art retrieval used the
  repository itself; no knowledge-base writes were made.
