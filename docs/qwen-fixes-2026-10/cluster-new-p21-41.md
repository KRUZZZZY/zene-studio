FALSE-POSITIVE

new-p21-41: FALSE-POSITIVE

Finding: `note.move_to_track` dereferences `to->createClip(...)` without a null check (reported at `src/core/ControlCommandsNoteMoves.cpp:73`).

Evidence: the destination pointer cannot be null at the call site, and `createClip` cannot return null on the only path that can run.

- `src/core/ControlCommandsNoteMoves.cpp:67-68` - `to` is `dynamic_cast<InstrumentTrack*>` of the resolved track, and a null `to` returns an `InvalidArgs` failure before any use. `to` is therefore a valid `InstrumentTrack` at line 73.
- `src/core/ControlCommandsNoteMoves.cpp:73` - `dynamic_cast<MidiClip*>(to->createClip(clip->startPosition()))`. Because the static type is `InstrumentTrack*`, the call dispatches to `InstrumentTrack::createClip`.
- `include/InstrumentTrack.h:248` - `Clip* createClip(const TimePos & pos) override;`.
- `src/tracks/InstrumentTrack.cpp:1091-1096` - the whole body is `auto p = new MidiClip(this); p->movePosition(pos); return p;`. It has no early return, no failure branch and no capacity check, so it never yields a null pointer. A failed allocation throws `std::bad_alloc` (the default `operator new`); the repo does not use `nothrow` in these files (log 005).
- `dynamic_cast<MidiClip*>` of a `MidiClip*` returns the same non-null pointer, so `target` is non-null. The constructor is `MidiClip(InstrumentTrack*)` at `include/MidiClip.h:57` and `src/tracks/MidiClip.cpp:44`.
- No class derives from `InstrumentTrack` in `include/`, `src/` or `plugins/` (log 006), so no override can return null. `InstrumentTrack::createClip` is the only dynamic target.
- The finding's failure scenario ("createClip fails/returns null on an invalid or full track") has no code path. An invalid track is rejected at lines 67-68, and no track type has a full-track limit that `createClip` checks.

Sibling sites: `src/core/ControlCommandsAudioSlice.cpp:227` and `src/core/ControlCommandsAudioAnalysis.cpp:100` use the same `dynamic_cast<MidiClip*>(track->createClip(...))` pattern. Each track there is created as an `InstrumentTrack` (`Track::create(Track::Type::Instrument, ...)`, AudioAnalysis at line 97), so they are covered by the same guarantee. Nothing in those files was changed.

Scope: no product code was changed, and no regression test was added. Procedure step 2 sends a not-real verdict straight to the report, so the RED/GREEN and step 4b exit codes do not apply here. The verdict is claim-only: its evidence is the code cited above, not a test run.

Commands run (all from the worktree, logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-41.r1/logs`):

| Evidence | Command | Result |
| --- | --- | --- |
| createClip caller at the finding | `rg -n "createClip|nothrow" src/core/ControlCommandsNoteMoves.cpp > logs/001-noteMoves-createClip.log 2>&1; echo EXIT=$? >> logs/001-noteMoves-createClip.log` | EXIT=0; line 73 only |
| InstrumentTrack::createClip body | `rg -n -A6 "^Clip\* InstrumentTrack::createClip" src/tracks/InstrumentTrack.cpp > logs/002-instrumenttrack-createclip.log 2>&1; echo EXIT=$? >> ...` | EXIT=0; lines 1091-1096 |
| Override declaration | `rg -n "createClip" include/InstrumentTrack.h > logs/003-instrumenttrack-h.log 2>&1; echo EXIT=$? >> ...` | EXIT=0; line 248 |
| No nothrow allocation in the create path | `rg -n "nothrow" src/tracks/MidiClip.cpp src/tracks/InstrumentTrack.cpp > logs/005-nothrow.log 2>&1; echo EXIT=$? >> ...` | EXIT=1; no matches |
| No InstrumentTrack subclass | `rg -n ":\s*(public|protected|private)\s+InstrumentTrack\b" include src plugins > logs/006-instrumenttrack-subclasses.log 2>&1; echo EXIT=$? >> ...` | EXIT=1; no matches |
| MidiClip constructor | `rg -n "MidiClip\( *InstrumentTrack|^MidiClip::MidiClip" include/MidiClip.h src/tracks/MidiClip.cpp > logs/008-midiclip-ctor.log 2>&1; echo EXIT=$? >> ...` | EXIT=0; `include/MidiClip.h:57`, `src/tracks/MidiClip.cpp:44` |

An earlier subclass search (`logs/004-instrumenttrack-subclasses.log`) had a malformed pipeline and found nothing usable; it was superseded by log 006 and is not relied on.

Files committed for this verdict: `docs/qwen-fixes-2026-10/cluster-new-p21-41.md` only. No product file changed.

## Self-check

Answers to the completion-check asks for this claim-only closure:

- Regression test fails with the product change reverted and passes with it: not applicable. No product change, and a not-real verdict needs no regression test. The procedure asks for no RED/GREEN run here.
- Test deterministic (no timing, sleeps or races): not applicable; no test added.
- Fix at the cause and complete (callers and siblings checked): not applicable; no fix. The sibling sites are checked above.
- No collateral damage: not applicable; no product code changed, so nothing that loads, saves or renders could change.
- FALSE-POSITIVE evidence (code file:line or contract): given above, `ControlCommandsNoteMoves.cpp:67-68, 73`, `InstrumentTrack.h:248`, `InstrumentTrack.cpp:1091-1096`, `MidiClip.h:57`, `MidiClip.cpp:44`, with the subclass check in log 006.
- Audio-thread, undo and reversibility items: not applicable; no product code changed, and this closure adds no command.
- Gates: `feature_check.py` could not run in this lane. It creates its log folder at `<worktree>/../zene-qwen-new-p21-41-feature-check`, which is outside the write-jail, and it exited 1 with `PermissionError` at `logs.mkdir` (`logs/009-feature-check.log`). Its eight gate commands were run directly from the worktree instead, with exit codes: fork-sources 0, all-sources-reproduce 0, no-upstream-regression 0, check-namespace 0, no-tautology 0, doc-figures 0 (`logs/010-gate-*.log`). Complexity and file-length exited 1 with the usual REGRESSION lines (`logs/010-gate-complexity.log`, `logs/010-gate-file-length.log`). Their items are in `src/` and `plugins/` files, none of which this branch changes: `git diff --name-only 639e1d23e..HEAD` lists only this report, and the merge base with FIX-BASE (`050/m0-train-17`) is `639e1d23e`. So the wrapper's ratchet rows should pass, but that is inferred from the wrapper's rule, not seen as its output.
- Completion check: `logs/011-submit-check.log`, run on the report commit before this section was added. Commits, clean tree, report and claim-only test rows PASS; the gates row FAILs only because of the `feature_check.py` log-folder refusal above. A re-run after this section is committed is in `logs/013-submit-check-final.log`.
