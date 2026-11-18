FIXED

# gui-1105-tab-mixer-return - a new Return strip was not labelled Return

## Verdict: REAL

`MixerView::syncWithMixer` (src/gui/MixerView.cpp:530-540 before the fix) called `refreshDisplay()` when the
strip count or the channel-order signature changed, then `return`ed. The type badges, strip info and mode bar are
applied only further down that function (`mixerstriptags::applyAll`, MixerView.cpp:558-561), so the strips the
rebuild had just created had empty badges until the 500 ms timer's next pass (MixerView.cpp:298).
The Return button handler (MixerView.cpp:238-243) calls `bus.create` and then `syncWithMixer()` once, so the user
saw the new strip without its "Return" badge. `ChannelType::Return` comes from `channel.isBus()`
(MixerStripInfo.cpp:58), so the model side was already right.

## Fix

Removed the early `return` in `syncWithMixer`, so the pass that rebuilt the strips goes on to the overlays,
strip info and tags. `refreshDisplay()` already stores the new VCA and order signatures, so the later VCA check is
a no-op after a rebuild. Every other caller (timer, `ShellSelection::changed`, `MixerModeBar::changed`, tests)
only gains the labelling it would get on its next pass. No new command, nothing on the audio thread.

## Evidence

| Step | Command (cwd) | Exit |
| --- | --- | --- |
| RED build | `cmake --build build --target MixerSelectionIdentityTest -j4` (worktree) | 0 |
| RED run | `ctest -R '^MixerSelectionIdentityTest$' --output-on-failure` (build/tests) | 8 - `Actual (strip->typeBadge()->text()): ""`, `Expected: "Return"`; 7 passed, 1 failed |
| GREEN build | `cmake --build build --target zene MixerSelectionIdentityTest MixerBusFreezeMenuTest MixerInsertLatencyTest -j4` | 0 |
| GREEN family | `ctest -R '^Mixer(SelectionIdentity\|BusFreezeMenu\|InsertLatency)Test$' --output-on-failure` (build/tests) | 0 - 3/3 passed |
| 4b revert | `git diff -- src/gui/MixerView.cpp > fix.patch && git apply -R fix.patch`, rebuild, `ctest -R '^MixerSelectionIdentityTest$'` | revert 0, test 8 (FAILS without the fix, same assertion) |
| 4b reapply | `git apply fix.patch`, rebuild, same ctest | reapply 0, test 0 (PASSES) |
| Gates | feature_check (see note) | 0 - all 8 PASS |

Test: `MixerSelectionIdentityTest::aNewReturnStripIsLabelledReturnAtOnce` clicks the real `newReturnBtn`
(`bus.create`) and, with no extra sync of its own, checks the new strip is a bus and its badge reads "Return". The
test also sets `ControlRegistry::setReady(true)` in `initTestCase` (reset in `cleanupTestCase`) because the button runs a
command. The existing tests call `syncMixer()` twice, which is why they never saw the gap.

## Environment notes (honest)

- The jail refused ccache's temp dir (`.../zene-bunny-fix/dsh/home/ccache-tmp-r6`), so every build ran with
  `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-gui1105`. This changes only where ccache keeps temp files.
- `feature_check.py` writes its logs to `<worktree>-feature-check/`, outside the write jail (PermissionError, exit 1,
  `logs/gates-1.log`). I ran a copy with only `ROOT` pinned to the real qwen dir and the log dir moved into the run folder
  (`feature_check_local.py`); the gate list and logic are identical. Result exit 0, `logs/gates-2.log`.
- Files committed: src/gui/MixerView.cpp, tests/src/gui/MixerSelectionIdentityTest.cpp, tests/upstream-modifications.txt,
  this report.

## Self-check

- **Test fails reverted, passes fixed:** reverted run exit 8 (`typeBadge()->text()` is `""`), fixed run exit 0; see the 4b rows above
  (`logs/test-4b-revert.log`, `logs/test-4b-reapply.log`).
- **Deterministic:** no timer, sleep or wall clock. It calls `syncWithMixer()` directly (the seam the button's own handler uses) and
  clicks the button with `QTest::mouseClick`; the 500 ms timer never has to fire.
- **Cause and callers:** the only early exit in `syncWithMixer` was the one removed (MixerView.cpp:530-545). Callers: timer
  (:298), `ShellSelection::changed` and `MixerModeBar::changed` (:115-117), the Return handler (:241) - all want the full pass. The
  sibling path `addNewChannel` (:415-433) already calls `updateMixerChannel` and records the signature, so the next pass has no
  rebuild and tags it as before. `engineDeletedChannel` -> `refreshDisplay` is followed by the timer's pass and was not part of the
  finding; left unchanged.
- **Collateral:** view-only change; no saved data, project loading, command or model is touched. The extra work after a rebuild
  (overlays, strip info, tags) is what every non-rebuild pass already does, and the loop guards null channels/views.
- **Gates:** `feature_check.py` and therefore `submit_check.py`'s gates row exit 1 inside this session because the script writes
  `<worktree>-feature-check/` outside the write jail (PermissionError, `logs/gates-1.log`). The same gates through a copy with the
  log dir moved passed 8/8 (`logs/gates-2.log`). Please re-run `submit_check.py` outside the jail.
- **Verdict is FIXED**, not ALREADY-FIXED/FALSE-POSITIVE.
