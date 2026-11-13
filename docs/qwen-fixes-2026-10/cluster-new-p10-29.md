Verdict: FIXED

new-p10-29: FIXED - `controlReadChainPreset` never checked the chain-preset `version` attribute, so a `.zcp` document of another (or no) version was read with the current device layout. A versionless or unknown-version file, including an empty one that would clear the chain it is applied to, is now refused with `invalid_args`.

Branch: `bugs/qwen-new-p10-29` (worktree `zene-qwen-new-p10-29`). Finding: `src/core/ControlChainPresetSupport.cpp:276` (historical line; the reader is now at line 282).

## Evidence

Finding verified against the code on this branch:

- Writer sets the version: `src/core/ControlChainPresetSupport.cpp:262` (`root.setAttribute("version", chainPresetVersion())`; before the fix it was the literal `1` at line 239).
- Reader never looked at it: before the fix, `controlReadChainPreset` (line 259 at HEAD) checked only the root tag name and the `devices` count. It now checks the version at line 307.
- Every reader goes through this one function: `src/core/ControlCommandsChain.cpp:97,224` (chain.apply / chain.list) and `src/core/ControlCommandsChainEdit.cpp:79,262` (chain.rename). The only writer is `controlChainPresetDocument` (line 257), so the reader and writer share one version constant.
- Clearing: `controlApplyChainPreset` drops every effect in the live chain (`while (!chain->effects().empty())`, around line 472) and re-adds only the staged devices. A preset that reads as zero devices therefore empties the chain.
- No saved data is affected: `find . -name '*.zcp'` (outside `build/`) returns nothing in the repo, and the writer has only ever emitted version 1. I could not inspect user preset stores on disk from this sandbox.

Fix (the cause, not a symptom):

- `chainPresetVersion()` (`ControlChainPresetSupport.cpp:70`) is the single version constant; the writer (line 262) and the reader both use it.
- `checkChainPresetVersion()` (lines 122-131) accepts only the exact string of that version. A missing, empty, `0`, `2`, `1.0` or `invalid` version returns `invalid_args` and names the version.
- It is called in `controlReadChainPreset` at line 307, after the root-name check and before the device-count check.
- `include/ControlChainPresetSupport.h:116-119`: the declaration's doc comment now lists the version refusal.

Callers and siblings checked: all readers and the single writer listed above. No new control command was added; `ControlCommandsSnapshot` is untouched. The change is a read-side refusal plus a shared constant, so the writer's bytes are identical (`version="1"`).

## Regression test

`tests/src/core/ControlChainPresetTest.cpp`:

- `aPresetRefusesAnUnsupportedVersion` (line 429 data, line 442 body): five data rows (`missing`, `0`, `2`, `1.0`, `invalid`). Each writes a valid preset with the version attribute removed or replaced, then requires `controlReadChainPreset` to return false with `InvalidArgs` and an error naming "version".
- `aVersionlessEmptyPresetCannotClearAChain` (line 466): writes raw bytes `<zenechainpreset name="empty" devices="0"/>` (no version); requires refusal. Then writes the same document with `version="1"` and requires it to read as a valid empty preset (0 devices).

Deterministic: no timing, sleeps or threads. All inputs are hand-built byte strings or documents written into the test's preset directory and removed afterwards.

## Commands and results

Each row is an exact command. Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-29.r1/logs/`.

| Step | Command (run from the worktree unless noted) | Log | EXIT |
| --- | --- | --- | --- |
| Build test target, first attempt | `cmake --build build --target ControlChainPresetTest -j4` | build-1.log | 2 (ccache could not write its temp file under the sandbox's `dsh/` path; environmental, not code) |
| Build test target | `CCACHE_TEMPDIR=$TMPDIR cmake --build build --target ControlChainPresetTest -j4` | build-2.log | 0 |
| RED (fix absent) | `cd build/tests && ctest -R '^ControlChainPresetTest$' --output-on-failure` | test-1-red.log | 8 |
| RED, QTest count | (same binary) `Totals: 16 passed, 6 failed, 4 skipped` | test-1-red.log | - |
| Build with fix | `CCACHE_TEMPDIR=$TMPDIR cmake --build build --target zene ControlChainPresetTest -j4` | build-3.log | 0 |
| GREEN (fix present) | `cd build/tests && ctest -R '^ControlChainPresetTest$' --output-on-failure` | test-2-green.log | 0 |
| GREEN, QTest count | `./ControlChainPresetTest` | test-2-green-binary.log | 0; `Totals: 22 passed, 0 failed, 4 skipped` |
| Proof step 4b: save fix as patch | `git diff -- src/core/ControlChainPresetSupport.cpp include/ControlChainPresetSupport.h > logs/fix.patch` | - | 0 |
| Proof step 4b: revert | `git apply -R logs/fix.patch` | - | 0 |
| Proof step 4b: rebuild reverted | `CCACHE_TEMPDIR=$TMPDIR cmake --build build --target zene ControlChainPresetTest -j4` | build-4b-reverted.log | 0 |
| Proof step 4b: test reverted (must FAIL) | `./ControlChainPresetTest` | test-4b-reverted.log | 6; `Totals: 16 passed, 6 failed, 4 skipped`; the 6 failures are the 5 version rows and the versionless-empty test |
| Proof step 4b: re-apply | `git apply logs/fix.patch` | - | 0 |
| Proof step 4b: rebuild fixed | `CCACHE_TEMPDIR=$TMPDIR cmake --build build --target zene ControlChainPresetTest -j4` | build-4b-applied.log | 0 |
| Proof step 4b: test fixed (must PASS) | `./ControlChainPresetTest` | test-4b-applied.log | 0; `Totals: 22 passed, 0 failed, 4 skipped` |
| Final GREEN after re-apply | `ctest -R '^ControlChainPresetTest$'` and `./ControlChainPresetTest` | test-9-green-ctest.log, test-9-green-binary.log | 0 and 0; 22 passed, 0 failed, 4 skipped |

Affected family (`ctest -R '^ControlChainPresetTest$'`): one ctest entry, 26 QTest cases (22 pass, 4 skip) with the fix. Four skips are the instrument-dependent cases: `this build exposes no loadable built-in instrument` (test-9-green-binary.log lines 24, 26, 29, 31). This means the apply/reversibility paths are not exercised in this build; the regression does not depend on them.

Wider chain-preset family (`ctest -R '(ControlChainPreset|ChainPresetMenu)'`):

| Run | Tree | ControlChainPresetTest | ChainPresetMenuTest | ControlChainPresets | Log |
| --- | --- | --- | --- | --- | --- |
| A | fix; LADSPA host not built | Pass | Pass | Fail: `plugin.load` refused, "the LADSPA host plugin 'ladspaeffect' is not in this build" | test-5-family.log |
| B | fix; `ladspaeffect` built (`cmake --build build --target ladspaeffect -j4`, EXIT 0, build-6-ladspa.log) | Pass | Fail: "Compared lists have different sizes" | Fail: "the chain.* socket proof has 13 failed check(s)" | test-6-family.log (EXIT 8) |
| C | fix reverted (`git apply -R`); `ladspaeffect` built | Fail (the new tests, expected) | Fail: same assertion | Fail: same "13 failed check(s)" | test-7-baseline-family.log (EXIT 8) |

Run B and run C give the same failures for the two non-regression tests with and without the fix, so these two failures are not caused by this change. The mechanism is visible in the transcript (test-6-family.log, `chain.apply` response): `device 0 of preset 'socket chain': this build has no device 'amp_mono (amp)'`. The apply path refuses the device after the version check has already passed. The cause is an identity mismatch I observed but did not confirm: the catalogue reports the LADSPA file as `amp.so` (`src/core/ControlDeviceCatalogue.cpp:132` sets `entry.file = key.first`; plugin.list prints `"file":"amp.so"`), while the saved identity's `hosted_file` is `amp` (written from the key's `file` attribute at `src/core/ControlDeviceSupport.cpp:480`), and `entryMatchesIdentity` compares them for equality (`src/core/ControlChainPresetSupport.cpp:364`). This is outside the finding and was not changed. It needs its own finding and its root cause confirmed.

Gates. `feature_check.py` could not run from this sandbox: its first step creates a log folder beside the worktree (`<worktree-parent>/<name>-feature-check`, `PermissionError`, log `gate-feature-check-1.log`; EXIT 1), and it also writes a base cache under the bunny-fix tree. I did not change the script. Instead I ran each of its gate commands directly from the worktree, with the script's own arguments, logging to the run folder:

| Gate | Command | Log | EXIT |
| --- | --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | gate-fork-sources.log | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | gate-all-sources-reproduce.log | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | gate-no-upstream-regression.log | 0 |
| check-namespace | `tests/scripted/check-namespace` | gate-check-namespace.log | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | gate-no-tautology.log | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | gate-doc-figures.log | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | gate-complexity.log | 1 (regressions only in other files: `WasmSandbox.cpp`, `XyPadPairTest.cpp`, `DawProjectReadTracks.cpp`, `ControlCommandsMixerStrip.cpp`, `ControlCommandsMixerTools.cpp`, `ControlCommandsSessionRecordLand.cpp`) |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | gate-file-length.log | 1 (regressions only in other files, e.g. `Vst3Host.cpp`, `TakeLane.cpp`, `WasmSandbox.cpp`) |

The two ratchets are red on the train on purpose. Under the wrapper's rule (fail only on an item new in a file this branch changed), neither changed file is listed: `ControlChainPresetSupport.cpp`, its header and `ControlChainPresetTest.cpp` appear in neither log. The file-length gate counts code lines only (blank and comment lines excluded, `tests/file-length-gate.sh:9-17`), so the file's growth to 548 physical lines does not count against the 500 limit. I did not change any gate limit or baseline.

Registration: the header is fork-authored and listed in `tests/fork-sources.txt:829` (no change). The `.cpp` (`tests/fork-sources.txt:1392`) and the test file were already listed in `tests/upstream-modifications.txt` (lines 811 and 4917) for earlier changes. The policy there requires a reason per deliberate change, so this cluster added one line for each (the last two lines of that file). `no-upstream-regression-gate.sh` (EXIT 0) and `fork-sources-gate.sh` (EXIT 0) pass after the addition (logs `gate-no-upstream-regression-2.log`, `gate-fork-sources-2.log`).

Files committed for this verdict: `src/core/ControlChainPresetSupport.cpp`, `include/ControlChainPresetSupport.h`, `tests/src/core/ControlChainPresetTest.cpp`, `tests/upstream-modifications.txt`, `docs/qwen-fixes-2026-10/cluster-new-p10-29.md`.

## Self-check

- **RED / GREEN proof.** The regression test fails without the fix and passes with it: step 4b rows above. Revert: `ControlChainPresetTest` EXIT 6, 6 failed, 16 passed, 4 skipped (test-4b-reverted.log). Fix: EXIT 0, 22 passed, 0 failed, 4 skipped (test-4b-applied.log).
- **Deterministic.** No wall-clock, sleep, thread or race in either new test. Inputs are hand-built XML byte strings or documents written into the test's preset directory and removed after each check.
- **Fix at the cause and complete.** The reader is the only path that parses a `.zcp`; all three callers (chain.apply, chain.list, chain.rename) go through it. The writer uses the same constant, so the two cannot drift. The version check runs before the device count and the device parse, so an unknown version never reaches the current-format layout.
- **No collateral damage.** The writer's output is byte-identical (`version="1"`). No `.zcp` files exist in the repo, and the writer has only ever produced version 1, so saved presets that this build wrote still load. The only refusal added is for a version other than exactly `1`. The two non-regression family tests fail identically with and without the change (runs B and C above), so no behaviour outside this bug changed. Limitation: I could not inspect user preset stores on disk.
- **Gate wrapper not run.** `submit_check.py` still reports `gates (feature_check.py)` as FAIL: the wrapper cannot create its log folder `zene-qwen-new-p10-29-feature-check` beside the worktree (`PermissionError`, `gate-feature-check-1.log`), which is outside this sandbox's write-jail, and it also writes a base cache under the bunny-fix tree. Its gate commands were all run individually (table in Gates above); six pass and the two ratchets are red only in files this branch did not change. A re-run of `feature_check.py` from an environment that can write beside the worktree is the remaining step.
- **Verdict basis.** FIXED: the finding's cause is confirmed at `ControlChainPresetSupport.cpp:262` (writer) and `:282` (reader, no check at HEAD). Not ALREADY-FIXED and not FALSE-POSITIVE.

## Notes for the owner

- The environment needed `CCACHE_TEMPDIR=$TMPDIR` for every build here; without it ccache failed to write temp files in the sandbox. This is a machine setting, not a code change.
- Four cases in `ControlChainPresetTest` skip on this build ("no loadable built-in instrument"), so the apply and reversibility checks are not exercised here.
- The identity mismatch described above (`amp` vs `amp.so`) is a separate, unconfirmed defect that makes chain.apply refuse valid LADSPA presets once the LADSPA host is built. It is not part of this finding.
