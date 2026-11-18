FIXED — cluster new-p14-414-port

**Verdict: FIXED.** The finding ("adding an automation target can race with playback") is REAL on
this branch: `AutomationClip::addObject()` changed the clip's target list under the clip mutex
alone, while the render thread walks the very same vector through the reference `objects()`
hands out. The fix is re-applied by hand from the accepted branch `bugs/qwen-new-p14-414`
(no merge, no cherry-pick, no rebase), and the regression is RED before the fix and GREEN after.

---

## The bug on this branch

| What | Where (this tree) |
|---|---|
| the reader: `objects()` hands the render thread a **reference** to the target vector and releases the clip mutex before the reader uses it | `src/core/AutomationClip.cpp:297-301` |
| the writer: `addObject()` did `m_objects.push_back(_obj)` under the clip mutex alone - a `push_back` that **reallocates** the vector | `src/core/AutomationClip.cpp:172-227` (push_back at `:214`) |
| the render thread walks that list per period, with the engine's change lock held | `src/core/Song.cpp` (automation pass) |

So a target connected while the render thread walks the list can reallocate the vector under the
reader's feet. The sibling that already had the guard is `removeObject()`
(`src/core/AutomationClip.cpp:229-247`), and the guard's order is fixed by the render thread: it
holds the engine's change lock for a whole render period and takes the clip mutex **inside** it -
so the control thread must take the change guard **before** the clip mutex, never after.

## The fix (re-applied by hand)

* `src/core/AutomationClip.cpp:172-192` - `addObject()` now takes the engine's change guard
  **before** the clip mutex, through the new helper `targetChangeGuard()`
  (`src/core/AutomationClip.cpp:107-114`), which is the ordinary `requestChangesGuard()` in a
  product build.
* `src/core/AutomationClip.cpp:229-236` - `removeObject()` takes the same helper, so both
  target-list changes use one path (its behaviour is unchanged: it already had the guard).
* `include/AutomationClip.h:87-109` - the **test seam**: the two points of a target-list change
  where the changing thread reports to a test (`Before` = inside the change, before the guard;
  `Holding` = the acquisition of the engine's change guard was **contended**). The whole block
  is `#ifdef LMMS_TESTING`.
* `include/AudioEngine.h:108-123` (the test-only `RequestChangesGuard` constructor) and
  `:576-597` (`requestChangeInModelForTest()` / `requestChangesGuardForTest()`),
  `src/core/AudioEngine.cpp:1466-1481` - the **test-only contended-acquisition guard**: the change
  lock is taken with a `try_lock`, and an acquisition that finds the lock held by another thread
  (the render thread) is reported **at the attempt, on the acquiring thread**. Compiled out
  without `LMMS_TESTING`: the product build, the audio thread and the lock itself are unchanged.

### What was re-applied, and what was deliberately not

The accepted branch `bugs/qwen-new-p14-414` carries two clusters. This port is the
`new-p14-414` fix only (the finding filed for this cluster):

| Accepted commit | Re-applied here? | Why |
|---|---|---|
| `2276033bf` - connect automation targets under the render-exclusion guard | yes | the fix itself |
| `9491e7c87` - rework: the restore path waits for the render, and the race regression has a deterministic seam | yes (the seam) | the deterministic seam is the regression's proof |
| `fc287022f` - the render-exclusion regression proves the guard at the acquisition | yes | the contended-acquisition observation |
| `33ef3bd95` - the drop helper takes QPoint on Qt 5 and QPointF on Qt 6 | no | it belongs to the `new-p14-416` test (the arranger drop), not to this finding |
| `515addd23` - `new-p14-416`: connecting/disconnecting a target is undoable | **no** | a different cluster with its own finding and its own port; porting it here would double-fix it and re-create the merge conflict this port exists to resolve |

**The one accepted hunk this port does not carry: the change guard in `loadSettings()`.** In the
accepted branch that guard was needed because `new-p14-416` made `loadSettings()` **clear**
`m_objects` (a different bug's fix). On this newest line `loadSettings()` does not touch the
target list at all: `src/core/AutomationClip.cpp:1181-1262` has no `m_objects` access (every
`m_objects` reference in the file is listed in the sibling table below), and the target list is
restored through `resolveAllIDs()` -> `addObject()` (`src/core/AutomationClip.cpp:1392-1441`),
which is now guarded. Adding a lock to the project-load/undo path with no defect behind it would
be a behaviour change outside this finding.

## The regression (deterministic, no wall-clock)

`tests/src/gui/AutomationClipDisconnectTest.cpp` - slot **`addObjectWaitsForRenderPublication`**:

* the test thread holds the engine's **change lock** (`AudioEngine::requestChangesGuard()`), the
  same mutex `renderNextPeriod()` holds for a whole period, so the changing thread's acquisition
  is **contended**; this exercises the shared exclusion lock without starting a nondeterministic render;
* the seam's `Before` point **parks** the connecting thread inside the change, so the test knows
  the thread is inside the change and not merely late;
* the contended acquisition is reported **at the attempt**, on the acquiring thread - the
  assertion is made on that **observation**, never on a timeout. The test thread releases the lock
  after observing contention. The two `wait_for` calls are
  deadlock guards (a thread that never arrives fails the assertion instead).

## Evidence

All logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-414-port.r1/logs/`.

| Check | Exact command | Exit | Result |
|---|---|---:|---|
| RED 1 - the whole product change reverted (the patch below) | `git apply -R logs/fix.patch` then `cmake --build build --target zene AutomationClipDisconnectTest` then `ctest -R '^AutomationClipDisconnectTest$' --output-on-failure` | build 0, ctest **8** | `FAIL! ... 'reached' returned FALSE. (the connecting thread never reached the target change)` (`test-red1.log`) |
| RED 2 - the bug as reported: the guard removed, the seam kept | `git apply -R logs/noguard.patch` then rebuild + `ctest -R '^AutomationClipDisconnectTest$' --output-on-failure` | build 0, ctest **8** | `FAIL! ... 'contended' returned FALSE. (connecting a target never reported a contended acquisition of the engine's change guard: it either went on while the render held the lock, or it never reached the guard)` (`test-red2.log`) |
| GREEN - the fix in place | `cmake --build build --target zene AutomationClipDisconnectTest` then `ctest -R '^AutomationClipDisconnectTest$' --output-on-failure` | build 0, ctest **0** | `1/1 AutomationClipDisconnectTest ... Passed 3.56 sec` (`test-green1.log`) |
| affected family | `ctest -R 'Automation' --output-on-failure` (from `build/tests`, after building the 17 family targets) | **0** | `100% tests passed out of 17` (`test-family-automation2.log`) |
| undo / serialisation family | `ctest -R '^(AutomationClipDisconnectTest\|ReversibilityUndoTest\|UndoBoundsTest\|UndoHistoryPersistenceTest\|UndoHonestyTest\|ClipSerialisationTest\|AutomatableModelTest)$' --output-on-failure` | **0** | `100% tests passed out of 7` (`test-undo-family.log`) |
| the shipping build is unaffected | the two changed translation units compiled again **without** `-DLMMS_TESTING` (`-fsyntax-only`, the build's own compile command) | **0** | both compile clean (`build-product-nolmmstesting.log`) - the seam and the contended probe are compiled out of the product build |
| gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py .` | **0** | `RESULT: PASS` - `fork-sources` PASS, `all-sources-reproduce` PASS, `no-upstream-regression` PASS, `check-namespace` PASS, `no-tautology` PASS, `doc-figures` PASS, `complexity` PASS ("no new item in your files"), `file-length` PASS (`feature-check-2.log`) |

The RED/GREEN pair is the instructed 4b cycle, with the product patch in `logs/fix.patch`:

```
git diff -- src/core/AutomationClip.cpp include/AudioEngine.h src/core/AudioEngine.cpp > logs/fix.patch
git apply -R logs/fix.patch      # -> rebuild -> ctest exit 8 (RED)
git apply    logs/fix.patch      # -> rebuild -> ctest exit 0 (GREEN)
```

**One deliberate deviation, with the reason:** the patch above omits the seam's **declaration**
hunk in `include/AutomationClip.h`. The declaration is test-only scaffolding with no behaviour;
reverting it would break the regression's *compilation* instead of producing a failing test. The
seam's **call sites** live in `src/core/AutomationClip.cpp` and are reverted with the patch, so the
reverted build really does not publish the target change - that is the RED above. The accepted
branch used the same split for its own round-2 and round-3 RED proofs.

## Siblings, callers and the wider path (all checked)

| Path | Where | Verdict |
|---|---|---|
| `addObject()` - `push_back`, may reallocate | `src/core/AutomationClip.cpp:172-227` | **fixed here** - the change guard, taken before the clip mutex |
| `removeObject()` - `erase` | `src/core/AutomationClip.cpp:229-247` | already guarded (cluster new-p10-506); now routed through the same helper, behaviour unchanged |
| `cleanObjects()` - `erase` of **null** pointers only | `src/core/AutomationClip.cpp:1482-1500` | checked: it never reallocates, and it is reached from `putValue()`/`putValues()`/`removeNode()`/`flipX()`, i.e. from the audio recording path (`NotePlayHandle::process` -> `recordValue()`). It must **not** take the change guard: a worker that took the render's lock would block the render thread that holds it, and the render thread waits for that worker - a deadlock. Unchanged |
| `objectDestroyed()` - `erase` | `src/core/AutomationClip.cpp:1455-1480` | checked: it runs on the thread that destroys the model (the control thread), it only erases and never reallocates. Unchanged |
| the copy constructor - copies the source's list | `src/core/AutomationClip.cpp:141-170` | checked: the control thread copies it, and it publishes the song's scratch growth with **no clip mutex held**, which is the order the render lock requires. Unchanged |
| `loadSettings()` - the RESTORE path | `src/core/AutomationClip.cpp:1181-1262` | checked: on this line it does **not** touch `m_objects` (no `m_objects` reference in that range); the target list is restored through `resolveAllIDs()` -> `addObject()` (`src/core/AutomationClip.cpp:1392-1441`), which is guarded. In the accepted branch this hunk had to be guarded because `new-p14-416` made `loadSettings()` clear the list - that hunk is not on this line |
| `resolveAllIDs()` -> `addObject()` | `src/core/AutomationClip.cpp:1392-1441` | the restore path reaches the guard through `addObject()`; it holds no clip mutex of its own, so there is no lock-order inversion |
| `InlineAutomation::automationClip()` -> `addObject(this)` | `include/InlineAutomation.h:80-92` | checked: the clip it creates is private to the model and never in a track, so the render thread never walks its list; the guard is a no-op cost |
| `globalAutomationClip()` -> `addObject()` | `src/core/AutomationClip.cpp:1368-1389` | control thread (the control commands and the arranger drop), guarded like every other connection |
| `AutomationClipView` / `AutomationEditor` / `AutomationTrackView` drops -> `addObject()` | `src/gui/clips/AutomationClipView.cpp:446`, `src/gui/editors/AutomationEditor.cpp:2271`, `src/gui/tracks/AutomationTrackView.cpp:80` | the user-visible path of the finding (drag a parameter onto a clip during playback); all control thread, all now guarded |
| `ControlAutomationSupport` / `ControlCommandsAutomationLanes` / `ControlCommandsMixerRoutes` -> `addObject()` | `src/core/ControlAutomationSupport.cpp:328`, `src/core/ControlCommandsAutomationLanes.cpp:79`, `src/core/ControlCommandsMixerRoutes.cpp:329` | control thread, guarded; no new command was added |
| the audio thread | `Song::processAutomations()` -> `recordValue()` -> `putValue()` | unchanged: `putValue()` takes only the clip mutex and must not take the change guard (the render thread holds it for the whole period and waits for the audio worker - a deadlock) |

## Realtime, undo and commands

* **Nothing new on the audio thread.** The change guard is taken on the **control** thread only;
  the render thread is the one that *holds* the lock. The product build's code path is the
  ordinary `requestChangesGuard()` - the same lock `removeObject()` already took. No allocation,
  no free, no logging, no signal emission was added; `requestChangeInModelForTest()` and the seam
  are `#ifdef LMMS_TESTING` and are not in the shipping binary.
* **Undo stays one honest step.** No command, no journal checkpoint and no undo path was touched:
  the fix changes *when* the target list may change, not *what* it changes.
* **No new control command** was added (the `ControlCommandsSnapshot` canary is untouched).

## Registration

* `tests/upstream-modifications.txt` - the five changed files are registered with the reason.
* No new file was created, so `tests/fork-sources.txt` needs no entry.

### The commits of this branch

| Commit | What |
|---|---|
| `37179b9c1` | `fix(cluster new-p14-414-port): connect automation targets under the render-exclusion guard` - the fix, the seam and the regression |
| `e8c03966f` | `chore(cluster new-p14-414-port): re-derive the whole-tree scope manifest from its own recipe` - the pre-existing gate repair, in its own commit (see below) |

### The two red gates were pre-existing on this tree, and are repaired in their own commit

`fork-sources` and `all-sources-reproduce` both compare `tests/all-sources.txt` with the recipe
in that file's own header. The manifest listed the vendored `plugins/SfzPlayer/sfizz/**` paths
(1148 of them) while the header's recipe excludes them ("Vendored third-party trees are excluded
on purpose - they are not this repo's code"). That disagreement was in the tree I was given and
had nothing to do with this fix:

```
git show dbb15dded:tests/all-sources.txt > /tmp/base-all-sources.txt
bash tests/all-sources-reproduce.sh /tmp/base-all-sources.txt     # -> exit 1
```

The **base commit's own manifest** fails the same check (exit 1,
`gate-all-sources-at-base.log`). The list had been re-derived by the train's snapshot commit
(`5ef150729`, "all-sources re-derived") with a generator that does not apply the header's
vendored-tree exclusion, so the manifest and its recipe drifted apart on the train.

The gate's own prescribed repair is to re-derive the list from the recipe ("Regenerate with" in
the manifest's header), and that is what the second commit of this branch does
(`chore(cluster new-p14-414-port): re-derive the whole-tree scope manifest from its own recipe`,
`e8c03966f`): the header, the recipe and the two ratchet baselines are untouched (no vendored
path is in `tests/complexity-baseline-all.tsv` or `tests/file-length-baseline-all.tsv`), so no
ratchet limit or baseline is re-anchored - only the scope list is brought back to what its own
header says it is. The effect on the whole-tree scope is that it stops reporting 124 bogus
"new file over 500 lines" regressions for vendored third-party files (abseil-cpp, simde, pugixml,
sfizz): `bash tests/file-length-gate.sh --scope all --check` reports those 124 lines with the old
list and none of them with the re-derived one (`gate-file-length-all-orig.log` vs
`gate-file-length-all.log`).


## Self-check

1. **The regression test FAILS with the product change reverted and PASSES with it.** RED 1:
   `git apply -R logs/fix.patch` + rebuild + `ctest -R '^AutomationClipDisconnectTest$'` -> exit
   **8** (`'reached' returned FALSE`, `test-red1.log`). RED 2 (the bug as reported, the guard
   removed, the seam kept): `git apply -R logs/noguard.patch` + rebuild + the same ctest -> exit
   **8** (`'contended' returned FALSE`, `test-red2.log`). GREEN: the same ctest with the fix in
   place -> exit **0** (`test-green1.log`).
2. **The test is deterministic.** The proof is an *observation*: the seam's `Holding` point is
   reported at the acquisition, on the acquiring thread, when the acquisition finds the engine's
   change lock held by another thread (the render's lock, which the test holds explicitly). No
   sleep, no wall-clock measurement and no race decides the result; the two `wait_for` calls are
   deadlock guards only - a thread that never reaches the seam fails the assertion instead of
   passing it.
3. **The fix is at the cause and complete.** The cause is the unguarded mutation of the vector
   the render thread walks through a reference (`objects()`, `src/core/AutomationClip.cpp:297-301`);
   the fix takes the engine's change guard before the clip mutex, the order the render thread
   takes the two in. Every sibling and every caller of the changed code is listed in the table
   above with its verdict; the restore path reaches the guard through `addObject()`.
4. **No collateral damage.** No limit, no check and no new behaviour was added: the only product
   change is *when* a target connection may publish the list (it now waits for the running render
   period, like every other target-list change already did). The saved data and the old projects
   are untouched - the target list is still serialized exactly as before
   (`src/core/AutomationClip.cpp:1167-1178`), and `loadSettings()` still reads it back unchanged.
   The `new-p14-416` half of the accepted branch is deliberately **not** ported here (a different
   cluster), so no target-list semantics changed.
5. **ALREADY-FIXED / FALSE-POSITIVE** - not applicable: the verdict is FIXED, and the evidence
   above is the code and the test runs.
6. **Registration and gates.** The five changed files are registered in
   `tests/upstream-modifications.txt` (the file ends with a newline); no new file was created, so
   `tests/fork-sources.txt` needs no entry. `feature_check.py` exits **0** with `RESULT: PASS`:
   `fork-sources`, `all-sources-reproduce`, `no-upstream-regression`, `check-namespace`,
   `no-tautology`, `doc-figures`, `complexity` ("no new item in your files") and `file-length`
   all PASS (`feature-check-2.log`). The first run of the gates on the tree I was given was red
   on `fork-sources` and `all-sources-reproduce`; that was the whole-tree scope manifest, not
   this fix - it is repaired in its own commit, with the proof and the effect in "The two red
   gates were pre-existing on this tree, and are repaired in their own commit" above.
