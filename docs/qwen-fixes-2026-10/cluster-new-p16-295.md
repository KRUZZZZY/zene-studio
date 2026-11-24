FIXED

# Cluster new-p16-295 — the automation scratch built its ordered lists quadratically

Verdict: **FIXED** (one bug: REAL). The read now collects and sorts once, through reserved storage,
instead of inserting each item into an ordered vector.

## The finding is real

Branch base `9913d36d53`, `include/AutomationRenderScratch.h` — the lines the finding names:

- `:100-104` `insertClip()` — `m_clips.insert(std::upper_bound(...), clip)`: one `std::vector::insert`
  per clip, and every entry from the insertion point on is relocated.
- `:140-152` `set()` — `lower()` (binary search) then `m_entries.insert(it, Entry{...})`: the same per
  model value.
- `:185-187` `lower()` — `std::lower_bound` over the address-ordered list.

Both lists are filled from the audio thread and are reachable with an input order opposite their sort
order, so a read pays N²/2 relocations:

- `src/core/Song.cpp:748` `container->fillAutomatedValues(*valuesIn, &m_automationInner, ...)` and
  `:754-760` `clipsIn->insertClipsInRange(*track, 0, timeStart)` run per period on the render thread.
- `src/core/TrackContainer.cpp:531-543` `gatherAutomationClips`/`fillAutomatedValues` →
  `insertClipsInRange` → `insertClip`; a track whose clips start earlier than the ones already collected
  inserts at the front each time.
- `src/core/TrackContainer.cpp:552-620` `collectAutomatedValues` → `level.set(model, value)` per target,
  in the order the clips' `objects()` give them — unrelated to address order, so about half of them land
  in front. `:462-492` `applyRelativeClips` does the same `find` + `set` for relative clips.
- The capacity is not small: `RenderCapacity = 16384` and `Song::ensureAutomationScratchCapacity`
  (`src/core/Song.cpp:972-989`) grows it above the project's clip and target counts, so a real project
  reaches thousands of entries in one period.

Measured, not argued: the pre-fix build relocates **523776** entries for 1024 clips handed over in
reverse order (1024·1023/2) and the same for 1024 targets in descending address order — see the RED log
below. At the 16384-entry render capacity that is ~134M entry moves in one period.

## What changed

`include/AutomationRenderScratch.h` only (one product file, header-only class; every caller keeps its
call signature and its meaning):

- **Clips** (`clips()` :134, `insertClip()` :145): `insertClip` appends and only notes whether the arrival
  order broke the position order; `clips()` stable-sorts once, through `m_clipSortBuffer` (:389), when it
  did. Stable bottom-up merge sort equals the ordered insert it replaces: sorted by start position, and
  clips at the same tick keep the order they arrived in. An input that already arrives in order — the
  common single-track read — moves nothing, exactly as before.
- **Values** (`set()` :199, `find()` :190, `entries()` :177): `set` finds-or-replaces through a reserved
  open-address index (`IndexSlot` :229, `entrySlot` :330, `placeInIndex` :348, `slotCountFor` :316),
  which is O(1) instead of a search plus a shift. The index is cleared by bumping an epoch
  (`clearIndex` :362), so `clear()` stays O(1). `entries()` sorts once by model address and re-points the
  index (`reindex` :371).
- Nothing new allocates on the audio thread: the two sort buffers, the index and the entries are all
  reserved together in `reserve()` (:95-103), which the control thread calls
  (`Song::ensureAutomationScratchCapacity`); a bounded scratch can never reach the one resizing branch in
  `set()` (:209-212 — it is guarded by the same `canGrow` that refuses at capacity, and the table is
  always twice the capacity). No lock, no I/O, no logging, no Qt call was added.
- Overflow semantics are untouched: past capacity the extra item is still counted in `overflows()` and
  dropped, and `appendClip()` (:154) still keeps the caller's order unsorted, as before.

The work is now observable: `clipMoves()` (:126) and `valueMoves()` (:129) count the relocations actually
done (an ordered build counts every entry it shifts, a sort every element it moves; one add per item).
They were committed first, in `4267702a3b`, with no behaviour change, so that this fix's diff touches only
the container and so that reverting it leaves a *measurable* quadratic build rather than a compile error.

## Test matrix

| Path | Test | State |
| --- | --- | --- |
| normal, worst order (clips) | `clipsHandedOverInReverseOrderAreBuiltInBoundedWork` | regression (RED pre-fix) |
| normal, worst order (values) | `targetsSetInReverseAddressOrderAreBuiltInBoundedWork` | regression (RED pre-fix) |
| normal, ordered input | `anOrderedReadMovesNothing` | moves nothing, both builds |
| real gather route | `aTracksRangeIsHandedOutInStartOrder` | `insertClipsInRange` over a whole track |
| ordering contract | `clipsAtTheSameTickKeepTheirOrder`, `aModelSetTwiceKeepsTheLastValue` | stability, last write wins |
| exhaustion | `aFullScratchDropsAndCountsInsteadOfGrowing` | drops and counts, still sorted |
| reconfiguration | `reserveKeepsWhatTheScratchHolds` | growth keeps every entry and the index |
| audio thread | `theBoundedReadAllocatesNothingOnTheAudioThread` | 0 allocations, 0 frees inside `RealtimeScope` |
| platform | header-only C++20 + Qt 5/6-neutral (no Qt API, no platform code, `<cstdint>`/`<functional>` only) | builds on the configured Linux tree |

New file `tests/src/core/AutomationScratchWorkTest.cpp` (the existing `AutomationRenderAllocationTest.cpp`
is already at 520 lines, over the 500-line gate), registered in `tests/CMakeLists.txt`,
`tests/fork-sources.txt` and `tests/all-sources.txt`.

## Commands and EXIT codes

Seam commit (counters only, no behaviour change):

    cmake --build build --target zene AutomationRenderAllocationTest -j4      EXIT=0   (build-1-seam.log)
    cmake --build build --target AutomationScratchWorkTest -j4                EXIT=0   (build-3-red-test.log)
    cd build/tests && ctest -R AutomationScratchWorkTest --output-on-failure  EXIT=8   (test-2-red.log)

RED (only the seam in place):

    FAIL!  : clipsHandedOverInReverseOrderAreBuiltInBoundedWork() 'scratch.clipMoves() <= budget'
             returned FALSE. (filling 1024 clips in reverse moved 523776 scratch entries; the O(N log N)
             bound is 45056)
    FAIL!  : targetsSetInReverseAddressOrderAreBuiltInBoundedWork() 'scratch.valueMoves() <= budget'
             returned FALSE. (setting 1024 targets in reverse address order moved 523776 scratch entries;
             the O(N log N) bound is 45056)
    Totals: 9 passed, 2 failed, 0 skipped, 0 blacklisted, 493ms

GREEN (with the fix):

    cmake --build build --target zene AutomationScratchWorkTest AutomationRenderAllocationTest
                          AutomationRenderLockTest -j4                       EXIT=0   (build-6-fix.log)
    cd build/tests && ctest -R AutomationScratchWorkTest --output-on-failure  EXIT=0   (test-3-green.log)

Family (all 22 `ctest -R Automation` tests, after building their targets):

    cmake --build build --target <17 automation test targets> -j4             EXIT=0   (build-7-family.log)
    cd build/tests && ctest -R Automation --output-on-failure                  EXIT=0   (test-5-family-green.log)
    100% tests passed out of 22

Render path the scratch serves (allocation/behaviour, from the header's own notes):

    cd build/tests && ctest -R "RecordingLatencyTest|DummyPacingTest|SessionSchedulerRenderTest"
                                                                              EXIT=0   (test-6-render.log)
    100% tests passed out of 3

Step 4b proof, product file only (`git diff -- include/AutomationRenderScratch.h`):

    git diff -- include/AutomationRenderScratch.h > .../logs/fix.patch         EXIT=0
    git apply -R .../logs/fix.patch                                            EXIT=0
    cmake --build build --target AutomationScratchWorkTest -j4                 EXIT=0   (build-9-proof-reverted.log)
    cd build/tests && ctest -R AutomationScratchWorkTest --output-on-failure   EXIT=8   (test-7-proof-red.log)
      same two failures, same 523776 counts: Totals: 9 passed, 2 failed
    git apply .../logs/fix.patch                                               EXIT=0
    cmake --build build --target AutomationScratchWorkTest zene -j4            EXIT=0   (build-10-proof-restored.log)
    cd build/tests && ctest -R "AutomationScratchWorkTest|AutomationRenderAllocationTest|AutomationRenderLockTest"
                                                                              EXIT=0   (test-8-proof-green.log)
    100% tests passed out of 3

Gates:

    python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>     EXIT=0   (gates-1.log)

      PASS  fork-sources            exit 0
      PASS  all-sources-reproduce   exit 0
      PASS  no-upstream-regression  exit 0
      PASS  check-namespace         exit 0
      PASS  no-tautology            exit 0
      PASS  doc-figures             exit 0
      PASS  complexity              no new item in your files
      PASS  file-length             no new item in your files
      RESULT: PASS

Logs: `/home/kruzzzzy/.config/kads/zene/state/runs/new-p16-295.r1/logs` (the file names above).

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.** Reverted:
  `ctest` EXIT=8, "523776 scratch entries; the O(N log N) bound is 45056" (both regression slots);
  restored: EXIT=0. Commands and logs above, step 4b.
- **Deterministic, no wall clock.** The test reads `clipMoves()`/`valueMoves()` — the scratch's own count
  of the elements its ordered storage relocated — so the assertion is about the algorithm's work, not the
  machine's speed. No sleeps, no timers, no threads, no timestamps: the input order (last clip first,
  highest model address first) is the injected condition, and both builds run the same 1024 items.
- **Fix at the cause, callers and siblings checked.** Callers of everything I changed:
  `Song::processAutomations` (`clips()`, `entries()`, `contains`, `size`, `overflows`),
  `Song::holdOverflowingAutomationTick` (`clips().size()`), `TrackContainer::gatherAutomationClips`
  (`insertClipsInRange`, `appendClip`), `TrackContainer::collectAutomatedValues` (`clips()`, `set`),
  `applyRelativeClips` (`find`, `set`), `TrackContainer::automatedValuesAt` (`entries`),
  `PatternStore::fillAutomatedValues` → `TrackContainer::fillAutomatedValues`.
  The clip side has two siblings and both keep their old meaning: `insertClip` = "in position order" and
  `appendClip` = "caller's order, never sorted" (the pattern editor's one clip per track, which the old
  code also never sorted). `clips()` and `entries()` are no longer `const` (they may now sort); every
  caller is a non-const scratch reference, which the build of `zene` and 25 test targets confirms.
  The other ordered insert in the tree, `Track::addClip` (`src/core/Track.cpp:1223`), is control-thread
  code and out of this finding's scope; it is unchanged.
- **No collateral damage.** Order is identical by construction: a stable merge sort over arrival order
  equals repeated `upper_bound` inserts (same start position ⇒ same arrival order), `entries()` is still
  ordered by model address, and `appendClip` still never sorts. Overflow, drop and `reserve()` semantics
  are unchanged; the bounded-exhaustion and growth slots above pin them. Nothing is added to the save
  format, no project data or capacity limit changed, and no model value, clip span or automation-order
  decision moved (the 22 automation tests and the 3 render tests pass unchanged).
- **ALREADY-FIXED / FALSE-POSITIVE:** not claimed — the finding is real, see the RED counts above.
- **Realtime, undo and commands.** Nothing added here allocates, frees, locks, waits, logs or emits on the
  audio thread: the sort buffers, the index and the entry lists are reserved by `reserve()` on the control
  thread, the sort is an in-place merge through those buffers, and the index is cleared by an epoch bump.
  `theBoundedReadAllocatesNothingOnTheAudioThread` measures 0 allocations and 0 frees inside a
  `RealtimeScope` over the sorting path. No undo step and no command is involved: this cluster changes no
  control command, no `ControlCommands*` file and no reversibility entry — undo stays one step for the
  features it belongs to.
- **Sibling commands in the command files you changed:** none. This cluster changed no command file at
  all (the two new-p16-295 commits touch `include/AutomationRenderScratch.h`, the new test file, its
  three registrations and this report). The command ids the pre-submit check lists (`app.*`,
  `arrangement.*`, …) come from the train that the branch is based on, not from this work, so there is
  nothing to keep consistent with the fix.
- **Scope of the check's file list.** `base..HEAD` on this branch also contains the train merge and the
  other lanes' commits; the files this cluster added or changed are exactly the five named in
  `## Commands and EXIT codes` plus this report (commits `4267702a3b` and `9411fa0ec4`).

## Other findings

- `src/core/Track.cpp:1223` (`Track::addClip`) inserts into the track's position-ordered clip vector the
  same way. It runs on the control thread from an edit, not from the render read, so it is not this
  finding and is unchanged here.
- `Song::holdOverflowingAutomationTick` is `noexcept` and reaches `m_automationRecordClips.clips()`; the
  sort it may now trigger cannot allocate there because that scratch is bounded and its buffers are
  reserved with it. Worth remembering if that member is ever turned into an unbounded scratch.
