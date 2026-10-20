# Cluster new-p9-246-port: Mallets allocates and locks on the audio thread

Verdict: **FIXED**

## Finding

`MalletsInstrument::playNoteImpl()` built a `MalletsSynth` with `new` on the
render thread on the first period of every note, inside a `static QMutex`
critical section, and `deleteNotePluginData()` deleted it again on the render
owner. The constructor allocated the STK voice and its delay buffer and called
`Stk::setSampleRate`/`Stk::setRawwavePath`, so the callback allocated, blocked
on a lock and mutated shared global STK state.

Earlier commits on this branch moved the construction off the render thread
into preconstructed voices and then made the pool belong to the instrument
(`MALLETS_VOICE_LIMIT` = 128). Luna's r4 review found one blocker: the limit
was enforced **per pool**, so a device change could retain a pool full of
sustained notes and install a fresh pool whose 128 voices were claimed by new
notes - up to 256 voices for one instrument.

## Fix

* `plugins/Stk/Mallets/Mallets.h:248`: the instrument now owns one
  `std::atomic<int> m_voicesInUse` voice budget.
* `plugins/Stk/Mallets/Mallets.cpp:96` `acquireVoiceBudget()` and `:111`
  `releaseVoiceBudget()`: a lock-free compare-exchange/fetch-sub on that one
  budget. They are called on the render path and never allocate, lock, wait or
  log.
* `plugins/Stk/Mallets/Mallets.cpp:498`: `playNoteImpl()` reserves a unit of
  the one budget **before** it claims a prepared voice from the current pool.
  When the budget is full the note stays silent and waits, exactly as before
  when the pool was full; `:508` returns the reservation if the pool claim
  fails.
* `plugins/Stk/Mallets/Mallets.cpp:615`: `deleteNotePluginData()` returns the
  slot and its share of the one budget.
* `plugins/Stk/Mallets/Mallets.cpp:279-280`: the `sampleRateChanged` handler
  still retires the old pool and installs a fresh one, so sustained notes keep
  rendering through the pool they hold. The budget is *not* reset by the
  change, so the retired notes and the notes that start afterwards draw from
  the same 128 units.

The current pool and every retained pool therefore share one per-instrument
limit, while active notes keep their old voices across a device change.

## Regression test

`tests/src/plugins/PluginPortsMigrationTest.cpp::
malletsVoiceLimitSurvivesDeviceChange()`:

* starts `malletsVoiceLimit` (128) sustained notes on one instrument and
  proves every one is audible (a real prepared voice, measured from rendered
  audio);
* triggers the device-change path the product uses
  (`AudioDevice::setSampleRateForTesting` + `AudioEngine::sampleRateChanged`,
  which runs the instrument's pool-retiring handler);
* starts 50 more notes of the same instrument and renders all 178: the voiced
  count must be **<= 128**, the 128 sustained notes must still be audible, and
  the 50 new notes must be silent while the budget is full;
* releases 10 sustained notes and proves exactly 10 waiting notes take the
  freed budget - the shared budget is returned, not leaked;
* wraps both render phases and the cleanup with
  `partc::RealtimeAllocationWatch` and asserts zero allocations **and** zero
  deallocations.

On the unfixed product the second phase voices 178 notes, so the test fails
with "voiced 178 notes ... the product limit of one instrument is 128".

## Evidence table

All logs are under `build/fix-logs-r5/` in the worktree.

| Check | Exact command | Result |
| --- | --- | --- |
| RED before fix | `cmake --build build --target PluginPortsMigrationTest -j8` (log `build-red-2.log`) | **EXIT=0** |
| RED before fix | `cd build/tests && ./PluginPortsMigrationTest malletsVoiceLimitSurvivesDeviceChange` (log `test-red-1.log`) | **EXIT=1**: "Mallets instrument voiced 178 notes after the device change, the product limit of one instrument is 128" |
| GREEN after fix | `cmake --build build --target zene PluginPortsMigrationTest -j8` (log `build-green-1.log`) | **EXIT=0** |
| GREEN after fix | `./PluginPortsMigrationTest malletsVoiceLimitSurvivesDeviceChange` (log `test-green-1.log`) | **EXIT=0**: "Totals: 3 passed, 0 failed" |
| Existing limit test | `./PluginPortsMigrationTest malletsVoiceLimitIsEnforcedPerInstrument` (log `test-green-existing-1.log`) | **EXIT=0** |
| Reversal proof, save | `git diff -- plugins/Stk/Mallets/Mallets.cpp plugins/Stk/Mallets/Mallets.h > build/fix-logs-r5/fix.patch` | **EXIT=0** (116 lines) |
| Reversal proof, revert | `git apply -R build/fix-logs-r5/fix.patch` (log `build-reverted-1.log`, `test-reverted-1.log`) | **EXIT=0**, rebuild **EXIT=0**, test **EXIT=1**: "voiced 178 notes ... limit ... 128" |
| Reversal proof, restore | `git apply build/fix-logs-r5/fix.patch` (log `build-restored-1.log`, `test-restored-1.log`) | **EXIT=0**, rebuild **EXIT=0**, test **EXIT=0** |
| Affected family | `cd build/tests && ctest -R '^PluginPortsMigrationTest$' --output-on-failure` (log `ctest-family-2.log`) | **EXIT=0**: 1/1 test passed |
| LADSPA fixture needed by the suite | `cmake --build build --target decimator_1202 -j8` (log `build-ladspa-fixture-1.log`) | **EXIT=0**; the fixture was absent from the prebuilt tree, `ctest-family-1.log` **EXIT=8** before it was built |
| Feature gates (simple) | `bash tests/fork-sources-gate.sh`, `bash tests/all-sources-reproduce.sh`, `bash tests/no-upstream-regression-gate.sh`, `tests/scripted/check-namespace`, `bash tests/no-tautology-gate.sh`, `python3 tests/doc-figures-gate.py` (logs `gate-*.log`) | **EXIT=0** each |
| Feature gates (ratchets) | `bash tests/complexity-gate.sh --check`, `bash tests/file-length-gate.sh --check` (logs `gate-complexity.log`, `gate-file-length.log`) | **EXIT=1** (train is red on purpose); no regression line names a file changed by this branch |
| Feature gate (one table) | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/.cache/zene-tmp/gate-wt-r5` (log `feature-check-final.log`, per-gate logs in `feature-check/`) | **EXIT=0**: all 8 rows PASS |

The supplied run log folder
`~/.local/share/zene-bunny-fix/qwen/runs/new-p9-246-port.r5/logs` is not
writable from this session's file sandbox (`touch`/`mkdir`/redirection all
return `EACCES`, while the worktree and `TMPDIR` are writable), so the identical
logs are stored in `build/fix-logs-r5/` inside the worktree. `feature_check.py`
also writes its per-gate logs to a sibling of the worktree, which the sandbox
rejects; the one-table result was produced by running the unmodified script on
a clean git worktree of the same commit under writable `TMPDIR`, and the
individual gate commands were also run directly in this worktree above.

## Files in this commit

- `plugins/Stk/Mallets/Mallets.cpp`
- `plugins/Stk/Mallets/Mallets.h`
- `tests/src/plugins/PluginPortsMigrationTest.cpp`
- `tests/rt-safety-scope.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-246-port.md`

## Self-check

- **Regression fails reverted, passes restored:** reverted product hunks ->
  `test-reverted-1.log` **EXIT=1** ("voiced 178 notes ... limit ... 128");
  restored -> `test-restored-1.log` **EXIT=0**. The first RED run before the
  fix is `test-red-1.log` **EXIT=1**.
- **Deterministic:** no sleeps, no wall-clock timing, no worker-thread race.
  The device change is injected synchronously with
  `AudioDevice::setSampleRateForTesting` plus `emit
  AudioEngine::sampleRateChanged()`; every note is rendered into a cleared
  buffer and every count is an exact integer. The allocation seam is the
  harness' `partc::RealtimeAllocationWatch`.
- **Fix at cause and complete:** the budget is claimed/released at the only
  two entry points of a voice (`playNoteImpl`, `deleteNotePluginData`); both
  failure exits in `playNoteImpl` return the reservation; the pool-retiring
  handler does not reset the budget. Every caller/sibling is listed in
  `tests/rt-safety-scope.txt` and the changed-file history in
  `tests/upstream-modifications.txt`.
- **No collateral damage:** no save format or setting changed. The only
  behaviour change is the one the review requested: after a device change the
  current and retained pools share the same 128-voice per-instrument limit.
  Notes beyond the limit wait for a voice, which is the existing policy; they
  are not dropped and no other plugin is affected. The instrument still
  renders sample-exactly against the reference in
  `migratedPluginsPreserveBehaviour` (part of the focused CTest family,
  **EXIT=0**).
- **Realtime / undo / commands:** the added render-path code is one atomic
  compare-exchange loop and one atomic fetch-sub; no allocation, free, mutex,
  wait, I/O, logging or Qt call. The render and cleanup allocation watch
  asserts zero allocations and zero deallocations. No control command or undo
  step was added or changed.
- **Pre-submit check:** `python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py
  /home/kruzzzzy/.cache/zene-tmp/gate-wt-r5 new-p9-246-port` **EXIT=0**
  (`submit-check-gate-copy.log`): commits, clean tree, report, changed test and
  gates all PASS. The script was run on the identical committed tree copy
  because its `feature_check.py` child writes per-gate logs next to the
  worktree, which this session's sandbox rejects (the same reason the run log
  folder is replaced by `build/fix-logs-r5/`). The five ASK items are answered
  by the bullets above.
- **Gate table:** the unmodified `feature_check.py` reports PASS for
  fork-sources, all-sources-reproduce, no-upstream-regression,
  check-namespace, no-tautology, doc-figures, and PASS for complexity and
  file-length because no new ratchet item names a file changed by this branch
  (`feature-check-final.log` **EXIT=0**, all 8 rows PASS; per-gate logs in
  `feature-check/`); the direct ratchet runs are **EXIT=1** for the train-wide
  baseline only.
