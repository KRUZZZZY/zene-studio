NEEDS-CONTEXT

# cluster new-p16-231: Automation scratch overflow can allocate on the render thread

## What the code does (seen this session)
- `src/core/Song.cpp:737-751`: `processAutomations` reads into the reserved `m_automationLevel` / `m_automationInner`
  scratch. If either one overflows, it redoes the read into the local, unbounded `spillValues` / `spillInner`, which
  allocates on the audio thread.
- `src/core/Song.cpp:763-772`: the clips for the recording pass use the same fallback (`spillClips`).
- `src/core/Song.cpp:849-854` and `892-898`: `m_oldAutomatedValues` and `m_recordedModels` grow past their capacity,
  which allocates and increments `m_automationScratchOverflows`.
- `include/Song.h:406-433` and `include/AutomationRenderScratch.h` (class comment) document this as the contract:
  "The caller MUST treat a non-zero overflows() as 'this read is incomplete' and redo it on an unbounded scratch".
- The finding is accurate: if a block ever runs past the reserve, the audio thread allocates.

## Why I did not fix it
1. The allocating redo is a deliberate, reviewed last resort. Commit fb89c74f47 ("Luna rework r2") made every
   edit path that adds clips or targets reserve the scratch beforehand: `Track::addClip`, `AutomationClip::addObject`
   and the copy constructor all call `noteAutomationGrowth`, which leads to `ensureAutomationScratchCapacity`
   (`src/core/Song.cpp:920-954`). It then states "The overflow redo stays as a counted last resort." The fallback
   is only reached when an edit happens on the render thread or inside a RealtimeScope (`Song.cpp:926`), or when
   the reserve fails with out-of-memory (`Song.cpp:928`).
2. An existing test pins the current behaviour:
   `tests/src/core/AutomationRenderAllocationTest.cpp` `anOverflowingReadIsRedoneAndStillAppliesEveryClip` forces a
   capacity of 8 with 200 clips. It requires the overflow counter to rise AND every clip to be applied within 4
   periods, with no control-thread step in between.
3. A realtime-safe overflow path cannot also stay complete. With no allocation, the audio thread cannot hold more
   entries than were reserved, so it must either (a) drop the extra clips or values (wrong automation, and models
   left stuck on automation control), or (b) skip or hold the tick and have a control-thread hook grow the scratch.
   Option (b) means automation goes stale until that hook runs. The only control-thread drains today are
   `applyPendingAutomationWrites`, called from `stop()` (`Song.cpp:1293`) and save (`Song.cpp:2470`), not
   periodically. Either option breaks the existing test in point 2. The rules forbid weakening or editing that
   test, and choosing between correctness and realtime safety for the out-of-memory or in-RealtimeScope-edit case
   is a product and design decision.

## What is needed to proceed
A decision from the owner of the automation render path (Luna rework r2 / fb89c74f47):
- either accept "hold the previous automation values for the overflowing tick, count it, and grow the scratch from
  a periodic control-thread hook (new)", and approve changing `anOverflowingReadIsRedoneAndStillAppliesEveryClip`
  to match that contract;
- or confirm that the counted allocating redo stays as the documented last resort. In that case this finding is
  FALSE-POSITIVE by design and should be recorded as a verified fact.

## Commands run
Read-only investigation (rg / sed / git log / git show on the files above). Nothing was built or tested, because no
product or test change was made.

## Self-check
- Verdict backed by evidence: the file:line citations above, read this session.
- Regression test: none. No fix was made (NEEDS-CONTEXT).
- Product code changed: none. Only this report was added.
- Proof manifest: not required for NEEDS-CONTEXT.

## Other findings
- The `m_oldAutomatedValues` / `m_recordedModels` growth branches (`Song.cpp:849-854`, `892-898`) can only be
  reached once the read has already spilled. With the reserve sized by `ensureAutomationScratchCapacity`, they share
  the same design question as above.

### Answers to submit_check's "?" items
- Fails without the fix, passes with it: not applicable. No product change, so no regression test (NEEDS-CONTEXT).
- Deterministic: not applicable, no test added. The existing `anOverflowingReadIsRedoneAndStillAppliesEveryClip`
  uses the `resizeAutomationScratchForTest` seam, not timing.
- Fix at the cause, siblings checked: no fix. I read all four overflow sites (`Song.cpp:744`, `763`, `852`, `896`)
  and the reserve path (`Song.cpp:920-967`). Their shared design question is described above.
- No collateral damage: no product file changed. The commit adds only this report.
- ALREADY-FIXED / FALSE-POSITIVE code reference: not my verdict. The contract that may make this by design is
  `include/AutomationRenderScratch.h` (class comment) and `include/Song.h:406-433`.
- Sibling commands (app.*, arrangement.*): not affected. Those test files come from earlier commits on this
  branch, and this session changed no command file.
- submit_check run this session: "RESULT: PASS - submit", EXIT=0. Its gates row (feature_check.py) was PASS.
