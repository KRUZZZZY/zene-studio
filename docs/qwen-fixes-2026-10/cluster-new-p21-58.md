FALSE-POSITIVE

# Cluster new-p21-58: sample.generate dereferences createClip result without null check

Verdict: FALSE-POSITIVE (new-p21-58). No product code changed; no regression test added, because there is no defect to prove fixed.

Finding (Luna, `src/core/ControlCommandsSample.cpp:252`): `sampleTrack->createClip(...)` is dereferenced without a null check, and the claim is that it "returns nullptr (allocation/track failure)" so the handler crashes instead of returning a typed error.

## Why the claim does not hold

1. **`createClip` on the only track type this handler can reach never returns nullptr.**
   - `src/core/ControlCommandsSample.cpp:229-232`: `track` is null-checked, then `sampleTrack` is obtained with `dynamic_cast<SampleTrack*>` and null-checked again (refused on a non-sample track). So `sampleTrack` is non-null at the call.
   - `src/tracks/SampleTrack.cpp:318-323`: the sole production body is
     ```cpp
     Clip * SampleTrack::createClip(const TimePos & pos)
     {
     	auto sClip = new SampleClip(this);
     	sClip->movePosition(pos);
     	return sClip;
     }
     ```
     A plain `new` cannot yield nullptr. Its failure mode is a thrown `std::bad_alloc`, not a null return.
   - `include/SampleTrack.h:66` is the only `createClip` override on `SampleTrack`. The base `include/Track.h:135` is pure virtual. No production subclass of `SampleTrack` exists. The two subclasses in the tree are test probes, `tests/src/core/ArrangeToolsTest.cpp:124` (`GuardProbeSampleTrack`, which also returns a non-null `new`) and `tests/src/core/AutomationRenderAllocationTest.cpp:61` (`FrozenTakeProbeTrack`, which does not override it).
   - "Track failure" cannot produce a null clip either: the track pointer is already validated, and `createClip` does not look up a track.

2. **Allocation failure does not crash the handler; it already becomes a typed error.**
   - A `bad_alloc` from `new SampleClip` propagates out of the handler. The handler is run by `runHandlerGuarded` (`src/core/ControlRegistry.cpp:444-456`), which catches `std::exception` and `...` and turns them into `controlErrorFromException(...)`. It is called from `src/core/ControlRegistry.cpp:514` inside the frame that ran it, and the outer `invoke()` boundary (`src/core/ControlRegistry.cpp:311-315`) is a second catch. So the agent receives a typed failure ("command 'sample.generate' failed: ..."), not a crash. This is the contract stated in the comments at `src/core/ControlRegistry.cpp:297-306` and `:510-513`.

So the null dereference the finding describes is unreachable on the current tree, and the failure path the finding worries about is already a typed error, not a crash.

## Commands run (exit codes)

| # | Command | Log | EXIT |
|---|---------|-----|------|
| 1 | `grep`/`sed` evidence extraction (createClip declarations, SampleTrack body, call-site guards, SampleTrack subclasses, exception boundary) | `runs/new-p21-58.r1/logs/evidence-1.log` | 0 |
| 2 | `python3 .../qwen/feature_check.py <worktree>` | `runs/new-p21-58.r1/logs/feature-check-1.log` | 1 (`PermissionError`: the script writes its logs to `<worktree>/../<name>-feature-check`, outside the write-jail; no gate was run by it) |
| 3 | `bash tests/fork-sources-gate.sh` | `gate-fork-sources-1.log` | 0 |
| 4 | `bash tests/all-sources-reproduce.sh` | `gate-all-sources-1.log` | 0 |
| 5 | `bash tests/no-upstream-regression-gate.sh` | `gate-no-upstream-1.log` | 0 |
| 6 | `tests/scripted/check-namespace` | `gate-check-namespace-1.log` | 0 |
| 7 | `bash tests/no-tautology-gate.sh` | `gate-no-tautology-1.log` | 0 |
| 8 | `python3 tests/doc-figures-gate.py` | `gate-doc-figures-1.log` | 0 |
| 9 | `bash tests/complexity-gate.sh --check` | `gate-complexity-1.log` | 1 (train-red: 7 `REGRESSION` lines, none in a file this branch changes) |
| 10 | `bash tests/file-length-gate.sh --check` | `gate-file-length-1.log` | 1 (train-red: 19 `REGRESSION` lines, none in a file this branch changes) |

Rows 3-10 are the gate commands `feature_check.py` runs, executed from the worktree because the wrapper could not start (row 2). The branch's only changed file is this report, so the complexity and file-length rows have no item in a changed file and pass under the wrapper's rule (`feature_check.py`, `item_file(l) in changed`). Equivalent table:

| Gate | Result |
|------|--------|
| fork-sources | PASS (exit 0) |
| all-sources-reproduce | PASS (exit 0) |
| no-upstream-regression | PASS (exit 0) |
| check-namespace | PASS (exit 0) |
| no-tautology | PASS (exit 0) |
| doc-figures | PASS (exit 0) |
| complexity | PASS - no new item in a changed file (script exit 1 is the train baseline) |
| file-length | PASS - no new item in a changed file (script exit 1 is the train baseline) |

No build and no ctest were run: nothing in the product tree changed, so there is no new binary to test and no regression test to run RED/GREEN. The RED/GREEN proof in the brief applies to a fix; this report claims no fix.

## Self-check

- **Verdict is a not-real claim with code evidence (DoD 1):** `SampleTrack::createClip` is `new SampleClip(this)` (`src/tracks/SampleTrack.cpp:318-323`), and is the only override (`include/SampleTrack.h:66`) on the track type the handler reaches after the `dynamic_cast` guard (`src/core/ControlCommandsSample.cpp:229-232`). Allocation failure throws `std::bad_alloc`, which `runHandlerGuarded` (`src/core/ControlRegistry.cpp:444-456`, called at `:514`) turns into a typed error. Evidence log: `evidence-1.log`.
- **Regression test (DoD 2):** not applicable, no fix. Nothing to prove fails before and passes after.
- **Fix at cause / siblings (DoD 3):** not applicable, no product change.
- **Collateral damage (DoD 4):** not applicable, no product change.
- **Realtime / undo / commands (DoD 5):** not applicable, no product change and no command added.
- **Gates (DoD 6):** rows 3-10 above, all pass under the wrapper's rule; the wrapper itself could not run (row 2, permission denied on the parent directory).
- **Anything the gates cannot see:** the audio-thread rule and the write-jail. `createClip` runs on the UI thread (the handler is run through `runOnUiThread`, `src/core/ControlRegistry.cpp:296`), so no realtime path is involved.
- **Completion check (`submit_check.py`):** run twice (`submit-check-1.log`, `submit-check-2.log`). Every row passes except `gates (feature_check.py)`, which fails only because the wrapper cannot write its logs outside the write-jail (see the unresolved note below). The ASK items are answered in this section.

### Answers to the completion-check ASK items (verdict is a not-real claim, so no product change)

1. *Regression test fails with the change reverted and passes with it:* no product change exists, so there is no test to run RED/GREEN. Not applicable to this verdict; the claim rests on the code citations above.
2. *Deterministic test:* no test was added. Not applicable.
3. *Fix at cause and complete:* no fix. Siblings checked: `src/core/ControlCommandsClip.cpp:131-134` and `src/core/ControlCommandsMidi.cpp:453-454` also dereference a `createClip` result without a null check. The Midi sibling calls `InstrumentTrack::createClip`, which is the same plain `new` (`src/tracks/InstrumentTrack.cpp:1086-1090`, `new MidiClip(this)`). I did not trace the track type of the `ControlCommandsClip.cpp` sibling, so it is unverified beyond the missing guard. None was changed.
4. *No collateral damage:* no product code changed, so no saved data, project load or limit path is affected.
5. *Verdict evidence (file:line / contract):* `src/tracks/SampleTrack.cpp:318-323` (plain `new`, never null), `include/SampleTrack.h:66` (only override), `include/Track.h:135` (pure virtual base), `src/core/ControlCommandsSample.cpp:229-232` (`dynamic_cast` plus null guard before the call), `src/core/ControlRegistry.cpp:444-456` and `:514` (exception boundary). The contract is the one in `ControlRegistry.cpp:297-306`: a handler that throws is a typed failure, never a crash.

**Unresolved - gates row (`feature_check.py`):** the wrapper exits 1 with `PermissionError` before running any gate, because it creates `<worktree>/../zene-qwen-new-p21-58-feature-check` in `projects/zene-studio-program/`, which is outside this session's write-jail. The gates were run individually in row 3-10 above and all pass under the wrapper's rule. This is an environment blocker, not a gate failure in the code, and it needs the owner to run `feature_check.py` from an unjailed shell or relocate its log directory. I did not edit the shared gate script.
