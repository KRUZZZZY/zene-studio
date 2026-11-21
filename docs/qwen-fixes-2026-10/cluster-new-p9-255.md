FIXED

# Cluster new-p9-255: LB302 filter settings reach the audio thread only at the block boundary

## Cause

`Lb302Synth` wrote the audio-thread filter state (`m_fs`) and the 24 dB/oct flag directly
from the control thread whenever a knob moved, and it read model values (`vcf()`,
`m_db24Active`) from the audio thread. Two consequences:

* the audio thread could observe a half-written `m_fs` / flag (data race);
* the *order* of adoption was whatever the control thread did, so a filter change could
  land in the middle of a block rather than at the next block boundary.

The old synchronous writes are now slots that only publish
(`plugins/Lb302/Lb302.cpp:296-325`): `filterChanged()` at :296, `decayChanged()` at :299,
`db24Toggled()` at :302 each call `publishFilterSettings()` (:308-323), which builds one
complete `Lb302FilterSettings` from the current knobs and hands it to a seqlock mailbox.
No audio state is touched on the control thread.

## Fix

The snapshot is applied, and the active filter recalculated, at the top of `process()`
(`plugins/Lb302/Lb302.cpp:325-349`): `applyPublishedFilterSettings()` (:325-338) takes the
latest snapshot, copies all six fields into `m_fs`, adopts `m_db24Active`, then calls
`recalcFilter()`; `process()` calls it first (:347-349).

The audio-owned settings, the `m_db24Active` flag and the `vcf()` selection live in
`plugins/Lb302/Lb302.h:225-235`: the mailbox (`m_filterMailbox`), `m_db24Active`, the two
filters `m_vcfs`, and `vcf()` returning `*m_vcfs[m_db24Active ? 1 : 0]`.

The seqlock mailbox, including the forced-mid-write take, is
`plugins/Lb302/Lb302FilterMailbox.h:36-110`. `publish()` marks the snapshot dirty and
retries until it holds an even version, so a writer that finds another mid-write marks the
snapshot dirty and the holding writer rebuilds from the current knobs. `take()` never
waits: it returns false while the version is odd (mid-write) or when the snapshot was
already adopted, so the reader keeps its current settings for one more block.

## Test evidence

* `test-3-red` (product change reverted, tests kept): **exit 8**. The mailbox test fails with
  `QFAIL("the LB302 does not publish filter settings to the audio thread")` at
  `tests/src/plugins/Lb302FilterMailboxTest.cpp(162)`, and the four boundary tests fail:
  `lb302FilterControlsReachTheAudioThreadOnlyAtTheBlockBoundary` (Lb302NotePathTest.cpp:395),
  `lb302LatestFilterChangeWinsBeforeTheNextBlock` (:476), `lb302FilterAdoptionAllocatesNothing`
  (:511), `lb302LoadedFilterSettingsApplyAtTheNextBlock` (:554).
* `test-4-green` (product change restored): **exit 0**, 2/2 tests passed
  (`Lb302NotePathTest`, `Lb302FilterMailboxTest`).
* `test-5a` (focused re-run): **2/2**, exit 0.
* `test-5b-lb302-migration`: `PluginPortsMigrationTest` **7/7**, exit 0
  (`Totals: 7 passed, 0 failed, ... 1829ms`). The build's dependency slot
  `add_dependencies(PluginPortsMigrationTest ... ${PART_C_MIGRATED_TARGETS})` is at
  `tests/CMakeLists.txt:4751-4755`. The literal four-only failure is logged in
  `test-5b-four-only.log`: `FAIL! : ...slice6PluginsAreLiveAndExact() 'lb302 != nullptr' returned
  FALSE. (lb302 render missing)` — `Totals: 5 passed, 1 failed`, i.e. adding only the four
  new slots without the migrated-target dependency reproduces the missing lb302 render.
* `gate-allsources`: `REPRODUCES: the entry list in all-sources.txt is the recipe's own output.`
* `feature-check`: exit 0, `RESULT: PASS`, all 8 rows PASS.

## Self-check

* **Regression test fails reverted, passes restored.** `test-3-red.log` exit **8**; after
  restoring the product change `test-4-green.log` exit **0**. Both run the focused ctest
  target pair (`Lb302NotePathTest`, `Lb302FilterMailboxTest`) in `build/tests`.
* **Every acceptance criterion proved by a test I ran.** (1) *Control-side changes do not
  touch audio state and land at the next block* — `lb302FilterControlsReachTheAudioThreadOnlyAtTheBlockBoundary`,
  exit 0. (2) *The latest change wins before the next block* — `lb302LatestFilterChangeWinsBeforeTheNextBlock`,
  exit 0. (3) *The mailbox publishes a complete snapshot* — `lb302PublishesFilterSettingsToTheAudioThread`,
  exit 0. (4) *Adoption allocates nothing* — `lb302FilterAdoptionAllocatesNothing`, exit 0.
  (5) *Loaded filter settings apply at the next block* — `lb302LoadedFilterSettingsApplyAtTheNextBlock`,
  exit 0. (6) *No collateral on the plugin migration* — `slice6PluginsAreLiveAndExact`, 7/7.
* **Deterministic.** No threads, sleeps or wall-clock timing. The interleaving is forced by
  calling `take()` from the `make()` hook that runs inside `publish()`
  (`tests/src/plugins/Lb302FilterMailboxTest.cpp:38-39,111`), so the mid-write case is scripted.
  The boundary tests drive `process()` block by block with injected values.
* **Fix at the cause; every caller/sibling walked.** `filterChanged`, `decayChanged`,
  `db24Toggled` (Lb302.cpp:296-302) all just publish; `loadSettings`/`loadSettings` path
  (:265-286) still writes the models then lets the slots publish; `sampleRateChanged` recomputes
  the decay knob and publishes; the note-start `recalcFilter()` (:388) still recalcs after a
  fresh snapshot; `vcf()` (:340 header) reads only `m_db24Active`, which only
  `applyPublishedFilterSettings()` writes on the audio thread; both filters (`m_vcfs[0]`,
  `m_vcfs[1]`) are kept loaded and selected by that flag.
* **No collateral.** `slice6PluginsAreLiveAndExact` still passes (7/7), and the LB302 slot
  tests (`lb302ProducerDoesNotWaitForEarlierClaim`, `lb302DecayTracksSampleRateChanges`,
  `lb302NoteEnqueueFailsafeStaysRealtimeSafe`) pass. Save/load round-trip is unchanged:
  `saveSettings` (:265-270) and `loadSettings` (:284-286) still write/read `shape`, `slide`,
  `dead` and the filter knobs exactly as before.
* **Nothing added allocates, frees, locks, waits, does I/O or emits a signal on the audio
  path.** `applyPublishedFilterSettings()` (Lb302.cpp:325-338) only copies six floats and
  calls `recalcFilter()`; mailbox `take()` (Lb302FilterMailbox.h:73-86) only loads atomics and
  unpacks a fixed-size array. Proven by `lb302FilterAdoptionAllocatesNothing` (exit 0).
* **No commands or undo change.** The diff touches no `ControlCommands*`, registry,
  reversibility-table or undo file; the fix has no user-facing command, so no command or undo
  step was added.
* **Lock-free for the platform row.** `Lb302FilterMailbox.h` asserts
  `static_assert(std::atomic<float>::is_always_lock_free, ...)` in the header, so the payload
  is lock-free on the platform that builds it.
* **ALREADY-FIXED / FALSE-POSITIVE:** not applicable; verdict is FIXED.

## Other findings

* `PluginPortsMigrationTest` reports environmental failures unrelated to this fix: the STK
  fixtures under `thirdparty/stk-rawwaves/` (`marmstk1.raw`, `sinewave.raw`) are missing, and
  the LADSPA fixture paths referenced by the migrated-plugin targets do not resolve in this
  checkout. The seven LB302/plugin-migration tests still pass; only the fixture-dependent
  renders log `FileRead::open: could not open or find file`.
* Remaining out-of-scope audio-thread model reads (not changed here): wave shape
  (`Lb302.cpp:439`, `m_waveShape.value()`), slide decay/slide toggle (`:608`, `:616`,
  `m_slideToggle.value()`), the dead toggle (`:365`, `m_deadToggle.value()`), plus the model
  reads that a later cluster should publish the same way.
