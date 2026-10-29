IMPLEMENTED

# Q050-089.1 - Expose hardware insert latency in PDC (stage 1 of 3)

Branch `feat/050-Q050-089.1-oc`, cut from `5c1c6715d` (050/m0-train-19). Commits: `47252ef9cb` (code + tests),
then the docs/snapshot commit, then this report. Details in `docs/050-features/Q050-089.1.md`.

`pdc.report`'s per-channel objects now carry `hardware_insert_latency_frames` and
`hardware_insert_latency_source` (`off` / `bypassed` / `override` / `measured` / `unmeasured`). The frames
come from `hardwareio::insertLatency()`, the same function `Mixer.cpp:2270` and `:2403` count for PDC. The
strip tooltip (`MixerStripInfo.cpp` `describeStrip`, through `mixerLatencyText`) names the same frames and
milliseconds, marked "measured" or "typed".

Logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-089.1.r1/logs/` (build-N / test-N).

## Acceptance

| Criterion | Test (command) | Exit with change | Exit with product hunks reverted |
| --- | --- | --- | --- |
| With a measured latency and with an override, `pdc.report` lists the effective insert latency per channel. | `ctest -R '^ControlHardwareIo$'`: real `zene` on the Dummy loopback; measured 812 (`measured`), override 300 (`override`), undo back to 812 | 0 (test-1, test-2, test-7) | 8: the 3 new checks FAILED (test-5-reverted-hwio) |
| (same, in process) | `ctest -R '^MixerInsertLatencyTest$'`: GUI app, ping over a cable, override, disable | 0 (test-1, test-7) | 8: all 4 cases FAIL (test-4-reverted-gui) |
| The strip PDC tooltip shows the same effective value as the report. | `ctest -R '^MixerInsertLatencyTest$'`: the tooltip from `MixerView::syncWithMixer()` on that strip matches the report's frames and ms | 0 | 8 |
| (wording) | `ctest -R '^MixerLatencyTextTest$'` | 0 | build of the test fails, exit 2 (build-4-reverted-texttest: too many arguments) |

Regression runs with the change (test-7, test-8, test-9): `HardwareIoTest` 0, `PdcMixerTest` 0,
`ControlPdcCommands` 0, `ControlBusCommands` 0, `ControlFeedbackCommands` 0 (after building the `amplifier`
module it needs), `ReversibilityContractTest` 0.

`ControlFeedbackCommands` checks `pdc.report` against a byte-exact baseline. I added the two new keys
(`0`, `"off"`) to each channel in that baseline and changed nothing else in it. Without `amplifier` built,
two other checks in it fail (a loaded effect, a saved-project sha256). They fail the same way with the
product reverted (test-6-reverted-feedback), so this change does not cause them.

## Self-check

- **Each new test fails reverted and passes restored.** I saved the `src/` + `include/` diff to
  `/tmp/q089-product.patch`, ran `git apply -R`, rebuilt `zene MixerInsertLatencyTest` (EXIT=0), then ran
  `MixerInsertLatencyTest` (exit 8), `ControlHardwareIo` (exit 8) and `ControlFeedbackCommands` (exit 8:
  the baseline now expects the new keys). `MixerLatencyTextTest` does not compile reverted (exit 2). I
  restored with `git apply`, rebuilt (build-5 EXIT=0), and all of them exit 0 (test-7).
- **Every criterion proven by a test I ran:** see the table above. Each criterion has a socket transcript
  (`ControlHardwareIo`) and an in-process GUI test (`MixerInsertLatencyTest`), all exit 0.
- **One undo step / reversibility:** no new command and no new mutation. `pdc.report` stays
  `not_mutating`. Only its description text changed; its result schema did not. `ReversibilityContractTest`
  exit 0. The transcript proves the override that `mixer.hardware_insert` sets (`true_inverse`, unchanged)
  is undone in one `control.undo`.
- **Audio thread:** nothing added runs on it. `insertLatencySource`, `channelLatencyJson` and
  `describeStrip` run on the interface thread (control handler, mixer view sync timer). They read
  settings written on that thread under the change lock, plus the insert's existing atomics.
  `hardwareio::insertLatency` was already called from the mixer and is unchanged. Local TSan cannot run
  here, so this is reasoned from the code, not measured.
- **Old projects:** no file format change; nothing is saved or loaded differently. `HardwareIoTest` (which
  covers insert save/load) exit 0. In `ControlFeedbackCommands`, the project-sha256 check passes with
  `amplifier` built.
- **Portability:** only `QString`/`QLatin1String`/`QCoreApplication::translate` and a defaulted
  `const QString&` parameter, all available in Qt 5.15. No Qt-6-only API, no `std::filesystem`, no POSIX
  call, no reserved identifiers (`small`/`near`/`far`/`interface`/`min`/`max`). The test uses `std::span`
  (C++20), as `HardwareIoTest` already does.
- **Snapshot / figures:** I regenerated the snapshot live with `snapshot_live.py` (EXIT=0, 868 ids / 76
  groups; only the `pdc.report` description and capture metadata changed). `doc-figures-gate.py` exit 0.
  No new id, so the canary figure is unchanged (the submit check's WARN lists ids it found in the diff
  text, not new ids).
- **Gates:** `feature_check.py` cannot run as shipped from this lane: it creates
  `<worktree>/../zene-feat-q050-089.1-feature-check`, which is outside my write area (PermissionError).
  For the same reason `submit_check.py` shows its "gates" row as FAIL. I ran a copy
  (`/tmp/q089fc/feature_check.py`) whose only change is the log directory, pointed at my run folder. It
  reported all 8 rows PASS, RESULT: PASS, exit 0 (feature-check-2.log). These are fork-sources,
  all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, and
  complexity and file-length (no new item in my files).
- **Registration:** `tests/src/gui/MixerInsertLatencyTest.cpp` is in `tests/fork-sources.txt` and
  `tests/CMakeLists.txt` (GUI list + offscreen env), and `tests/all-sources.txt` was re-derived
  (prints REPRODUCES).
- **Docs:** manual (`data/manual/index.html`, Hardware I/O paragraph), `docs/050-features/Q050-089.1.md`,
  and `docs/KNOWN-LIMITATIONS.md` (the "pdc.report does not list the insert's latency" bullet is marked
  lifted; export bypass is still listed).
- **Not done (later stages):** real-time export through the insert, and its UI (Q050-089.2 / .3).
  Offline export still bypasses the insert. The report now names that as `bypassed` while an export runs.
  I did not run a test of that bypass path in this stage.
