FIXED

# Cluster new-p9-255: LB302 filter settings reach the audio thread only at the block boundary

## Cause

Before the fix, `vcf()` selected the filter by reading the model value
`m_db24Toggle.value()` on the audio thread:
`*m_vcfs[m_db24Toggle.value() ? 1 : 0]`. The control thread wrote `m_fs` and the
filter state (`recalcFilter()` updates the coefficients and `m_vcfEnvPos`)
directly whenever a knob moved. Two consequences:

* the audio thread could observe a half-written `m_fs` or filter coefficients
  mid-recalculation (data race on `m_fs` and the filter coefficients /
  `m_vcfEnvPos`);
* the *order* of adoption was whatever the control thread did, so a filter change
  could land in the middle of a block rather than at the next block boundary.

`m_db24Active` is new audio-owned state, not a pre-existing flag: it replaces the
audio thread's read of `m_db24Toggle.value()`, so the race was on `m_fs` and the
filter coefficients / `m_vcfEnvPos`, not on a pre-existing flag.

The old synchronous writes are now slots that only publish
(`plugins/Lb302/Lb302.cpp:296-323`): `filterChanged()` at :296, `decayChanged()`
at :299, `db24Toggled()` at :302 each call `publishFilterSettings()`
(:308-323), which builds one complete `Lb302FilterSettings` from the current
knobs and hands it to a seqlock mailbox. No audio state is touched on the control
thread.

## Fix

The snapshot is applied, and the active filter recalculated, at the top of
`process()` (`plugins/Lb302/Lb302.cpp:325-349`): `applyPublishedFilterSettings()`
(:325-337) takes the latest snapshot, copies its five `m_fs` fields (`cutoff`,
`reso`, `envmod`, `envdecay`, `dist`) into `m_fs`, copies the db24 flag into the
audio-owned `m_db24Active`, then calls `recalcFilter()`; `process()` calls it
first at :349.

The audio-owned settings, the `m_db24Active` flag and the `vcf()` selection live in
`plugins/Lb302/Lb302.h:222-235`: `m_fs` (:222), the mailbox
(`m_filterMailbox`), `m_db24Active`, the two filters `m_vcfs`, and `vcf()`
returning `*m_vcfs[m_db24Active ? 1 : 0]` (:235).

The seqlock mailbox, including the forced-mid-write take, is
`plugins/Lb302/Lb302FilterMailbox.h:36-110`. `publish()` marks the snapshot dirty and
retries until it holds an even version, so a writer that finds another mid-write marks the
snapshot dirty and the holding writer rebuilds from the current knobs. `take()` never
waits: it returns false while the version is odd (mid-write) or when the snapshot was
already adopted, so the reader keeps its current settings for one more block.

## Test evidence

* `test-3-red` (product change reverted, tests kept): **exit 8**. The mailbox test fails with
  `FAIL! : ... the LB302 does not publish filter settings to the audio thread` at
  `tests/src/plugins/Lb302FilterMailboxTest.cpp(162)`, and the four boundary tests fail:
  `lb302FilterControlsReachTheAudioThreadOnlyAtTheBlockBoundary` (Lb302NotePathTest.cpp:395),
  `lb302LatestFilterChangeWinsBeforeTheNextBlock` (:476), `lb302FilterAdoptionAllocatesNothing`
  (:511), `lb302LoadedFilterSettingsApplyAtTheNextBlock` (:554).
* `test-4-green` (product change restored): **exit 0**, 2/2 tests passed
  (`Lb302NotePathTest`, `Lb302FilterMailboxTest`).
* `test-5a` (focused re-run): **2/2**, exit 0.
* `test-5b-lb302-migration`: `PluginPortsMigrationTest` **7/7**, exit 0
  (`Totals: 7 passed, 0 failed, ... 1829ms`). `slice6PluginsAreLiveAndExact` consumes
  `m_migrated`, the vector populated by `migratedPluginsPreserveBehaviour`
  (`tests/src/plugins/PluginPortsMigrationTest.cpp:2377-2504`; slice6 at :5137). The
  `add_dependencies(PluginPortsMigrationTest ... ${PART_C_MIGRATED_TARGETS})` at
  `tests/CMakeLists.txt:4751-4755` builds the migrated modules but does not populate that
  vector. Invoking only the four LB302 slots (`lb302ProducerDoesNotWaitForEarlierClaim`,
  `lb302DecayTracksSampleRateChanges`, `lb302NoteEnqueueFailsafeStaysRealtimeSafe`,
  `slice6PluginsAreLiveAndExact`) therefore fails with `lb302 render missing`
  (`test-5b-four-only.log`: `Totals: 5 passed, 1 failed`), while the run that also invokes
  `migratedPluginsPreserveBehaviour` passes 7/7.
* `gate-allsources`: `REPRODUCES: the entry list in all-sources.txt is the recipe's own output.`
* `feature-check`: exit 0, `RESULT: PASS`, all 8 rows PASS.

## Commands and results

All commands were run from the work tree
`/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-255`
unless the command `cd`s into `build/tests` itself. The log paths point at
`/home/kruzzzzy/.config/kads/zene/state/runs/new-p9-255.r1/logs`, as run.

Builds:

* `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-255 && rm -f build/tests/CMakeFiles/Lb302FilterMailboxTest.dir/src/plugins/Lb302FilterMailboxTest.cpp.o build/tests/Lb302FilterMailboxTest && CCACHE_DISABLE=1 /home/kruzzzzy/.local/share/zene-bunny-fix/duet/quiet-run rebuild-mailbox3 -- bash -c 'cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-255 && CCACHE_DISABLE=1 cmake --build build --target Lb302FilterMailboxTest -- -j12 > /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-255.r1/logs/build-mailbox3.log 2>&1; echo EXIT=$?' && strings build/tests/Lb302FilterMailboxTest | grep -c 'does not publish filter settings'` — exit **0** (the rebuilt binary contained the new QFAIL string).
* `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-255 && CCACHE_DISABLE=1 cmake --build build --target zene lb302 Lb302NotePathTest Lb302FilterMailboxTest -- -j12 > /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-255.r1/logs/build-2g.log 2>&1; echo EXIT=$?` — exit **0**.
* `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-255 && rm -f build/tests/CMakeFiles/Lb302FilterMailboxTest.dir/src/plugins/Lb302FilterMailboxTest.cpp.o* && CCACHE_DISABLE=1 cmake --build build --target zene lb302 Lb302NotePathTest Lb302FilterMailboxTest -- -j12 > /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-255.r1/logs/build-3.log 2>&1; echo EXIT=$?` — exit **0** (RED rebuild, product change reverted).
* `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-255 && rm -f build/tests/CMakeFiles/Lb302FilterMailboxTest.dir/src/plugins/Lb302FilterMailboxTest.cpp.o* && CCACHE_DISABLE=1 cmake --build build --target zene lb302 Lb302NotePathTest Lb302FilterMailboxTest -- -j12 > /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-255.r1/logs/build-4.log 2>&1; echo EXIT=$?` — exit **0** (GREEN rebuild, product change restored).

Product patch used for the RED/GREEN swap:

* `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-255 && git add -N plugins/Lb302/Lb302FilterMailbox.h && git diff -- plugins/Lb302/Lb302.cpp plugins/Lb302/Lb302.h plugins/Lb302/Lb302FilterMailbox.h > /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-255.r1/logs/fix.patch && rg -n 'new file mode|Lb302FilterMailbox.h' /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-255.r1/logs/fix.patch > /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-255.r1/logs/patch-check.log 2>&1; echo EXIT=$?; cat /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-255.r1/logs/patch-check.log` — exit **0**.
* `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-255 && git apply -R /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-255.r1/logs/fix.patch; echo EXIT=$?; test ! -e plugins/Lb302/Lb302FilterMailbox.h; echo HEADER_GONE=$?` — exit **0**.
* `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-255 && git apply /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-255.r1/logs/fix.patch; echo EXIT=$?; test -e plugins/Lb302/Lb302FilterMailbox.h; echo HEADER_BACK=$?` — exit **0**.

Focused regression tests:

* `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-255/build/tests && ctest -R '^(Lb302NotePathTest|Lb302FilterMailboxTest)$' --output-on-failure > /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-255.r1/logs/test-3-red.log 2>&1; echo EXIT=$?` — exit **8** (RED; 2/2 tests failed).
* `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-255/build/tests && ctest -R '^(Lb302NotePathTest|Lb302FilterMailboxTest)$' --output-on-failure > /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-255.r1/logs/test-4-green.log 2>&1; echo EXIT=$?` — exit **0** (GREEN; 2/2 tests passed).

p5 checks:

* `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-255/build/tests && ctest -R '^(Lb302NotePathTest|Lb302FilterMailboxTest)$' --output-on-failure > /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-255.r1/logs/test-5a.log 2>&1; echo EXIT=$?` — exit **0**, 2/2.
* `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-255/build/tests && QT_QPA_PLATFORM=offscreen ./PluginPortsMigrationTest lb302ProducerDoesNotWaitForEarlierClaim lb302DecayTracksSampleRateChanges lb302NoteEnqueueFailsafeStaysRealtimeSafe slice6PluginsAreLiveAndExact > /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-255.r1/logs/test-5b-lb302-migration.log 2>&1; echo EXIT=$?` — exit **1**, `Totals: 5 passed, 1 failed`; the log was preserved with `cp .../test-5b-lb302-migration.log .../test-5b-four-only.log` before the next run.
* `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-255/build/tests && QT_QPA_PLATFORM=offscreen ./PluginPortsMigrationTest migratedPluginsPreserveBehaviour lb302ProducerDoesNotWaitForEarlierClaim lb302DecayTracksSampleRateChanges lb302NoteEnqueueFailsafeStaysRealtimeSafe slice6PluginsAreLiveAndExact > /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-255.r1/logs/test-5b-lb302-migration.log 2>&1; echo EXIT=$?` — exit **0**, `Totals: 7 passed, 0 failed`.
* `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-255 && bash tests/all-sources-reproduce.sh > /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-255.r1/logs/gate-allsources.log 2>&1; echo EXIT=$?; cat /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-255.r1/logs/gate-allsources.log` — exit **0**, `REPRODUCES`.
* `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-255 && python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-255 > /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-255.r1/logs/feature-check-1.log 2>&1; echo EXIT=$?; tail -25 /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-255.r1/logs/feature-check-1.log` — exit **0**, `RESULT: PASS`.

Submit checks:

* `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-255 && python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-255 new-p9-255 > /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-255.r1/logs/submit-check-1.log 2>&1; echo EXIT=$?; grep -E "FAIL|WARN|RESULT" /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-255.r1/logs/submit-check-1.log` — exit **0**, `RESULT: PASS - submit`.
* `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-255 && python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-255 new-p9-255 > /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-255.r1/logs/submit-check-2.log 2>&1; echo EXIT=$?; grep -E "FAIL|WARN|RESULT" /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-255.r1/logs/submit-check-2.log; echo ---; git status --porcelain; echo "STATUS_EXIT=$?"; git rev-parse HEAD; python3 -c "import json;d=json.load(open('/home/kruzzzzy/.config/kads/zene/state/runs/new-p9-255.r1/logs/proof.json'));print(sorted(d), [sorted(a) for a in d['artifacts']])"` — exit **0**, `RESULT: PASS - submit`, empty `git status --porcelain`.

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
  `db24Toggled` (Lb302.cpp:296-302) all just publish; the `loadSettings` path (:275-293)
  still writes the models then lets the slots publish (:291-292); `sampleRateChanged`
  recomputes the decay knob and publishes; the note-start `recalcFilter()` (:388) still
  recalcs after a fresh snapshot; `vcf()` (Lb302.h:235) reads only `m_db24Active`, which
  only `applyPublishedFilterSettings()` writes on the audio thread; both filters
  (`m_vcfs[0]`, `m_vcfs[1]`) are kept loaded and selected by that flag.
* **No collateral.** `slice6PluginsAreLiveAndExact` still passes (7/7), and the LB302 slot
  tests (`lb302ProducerDoesNotWaitForEarlierClaim`, `lb302DecayTracksSampleRateChanges`,
  `lb302NoteEnqueueFailsafeStaysRealtimeSafe`) pass. Save/load round-trip is unchanged:
  `saveSettings` (:258-272) and `loadSettings` (:275-293) still write/read `shape`, `slide`,
  `dead` and the filter knobs exactly as before.
* **Nothing added allocates, frees, locks, waits, does I/O or emits a signal on the audio
  path.** `applyPublishedFilterSettings()` (Lb302.cpp:325-337) only copies five floats and
  the db24 flag and calls `recalcFilter()`; mailbox `take()` (Lb302FilterMailbox.h:73-86)
  only loads atomics and unpacks a fixed-size array. Proven by
  `lb302FilterAdoptionAllocatesNothing` (exit 0).
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
  (`Lb302.cpp:439`, `m_waveShape.value()`), the slide-decay knob (`:430`,
  `m_slideDecKnob.value()`), the slide toggle (`:386`, `:608`, `:616`,
  `m_slideToggle.value()`), the dead toggle (`:365`, `m_deadToggle.value()`), plus the model
  reads that a later cluster should publish the same way.
