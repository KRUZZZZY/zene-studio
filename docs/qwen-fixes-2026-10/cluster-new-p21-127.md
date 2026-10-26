Verdict: FALSE-POSITIVE

new-p21-127: FALSE-POSITIVE — `createTrackOfType` cannot reach `back()` on an empty container, so there is no undefined behaviour to fix.

Evidence:

- **The finding's premise cannot occur.** The finding says `addPatternTrack()` may "fail to append". `Song::addPatternTrack()` is `void` ([src/core/Song.cpp:1434](src/core/Song.cpp:1434)) and has no failure return. The only way to get a Pattern track is `Track::create(Type::Pattern, ...)`, whose Pattern arm is `new PatternTrack(tc)` ([src/core/Track.cpp:492](src/core/Track.cpp:492)). The `Track` base constructor always calls `m_trackContainer->addTrack(this)` ([src/core/Track.cpp:303](src/core/Track.cpp:303)).
- **`addTrack` appends for every Pattern track.** `TrackContainer::addTrack` skips only `Type::HiddenAutomation` ([src/core/TrackContainer.cpp:321](src/core/TrackContainer.cpp:321)) and otherwise does `m_tracks.push_back(_track)` ([src/core/TrackContainer.cpp:325](src/core/TrackContainer.cpp:325)). A Pattern track is not hidden, so the container has grown by one before `addPatternTrack()` returns. `tracks().back()` is therefore the track just created, and the container is never empty at that point.
- **The only interruption is an exception, not a silent failure.** If `new PatternTrack` fails, `std::bad_alloc` propagates out of `createTrackOfType` before `back()` is evaluated, so no `back()` on an empty vector is reached.
- **The `nullptr` branch is reachable only for types refused up front.** `Track::create` returns `nullptr` for Event and Video (`default: break`, [src/core/Track.cpp:504](src/core/Track.cpp:504); the Event/Video arms are commented out at [src/core/Track.cpp:494-495](src/core/Track.cpp:494)). The DawProject import refuses those first: `allTrackTypesCreatable` ([src/core/DawProjectSession.cpp:154-166](src/core/DawProjectSession.cpp:154), with the Video refusal at [:158](src/core/DawProjectSession.cpp:158)) runs at [:485](src/core/DawProjectSession.cpp:485), before `song->clearAllTracks()` at [:488](src/core/DawProjectSession.cpp:488). The caller's `if (track == nullptr)` at [src/core/DawProjectSession.cpp:516](src/core/DawProjectSession.cpp:516) is therefore a defensive check that the current code does not trigger.
- **Sibling copy is also safe.** `ControlCommandsArrangement.cpp` has the same helper ([src/core/ControlCommandsArrangement.cpp:75-79](src/core/ControlCommandsArrangement.cpp:75)). Its only type source is `trackTypeForName` ([src/core/ControlCommandsArrangement.cpp:61-73](src/core/ControlCommandsArrangement.cpp:61)), which yields only Pattern, Sample, Automation, Folder or Instrument, never Event, Video or HiddenAutomation. Both call sites (`:221`, `:230-231`) therefore always get a non-null track.

Not-real branch: no product file changed, so there is no RED/GREEN pair and no `fix.patch` to reverse (step 4b does not apply). No regression test was added, because there is no defect for one to catch. A test of `createTrackOfType` would need the anonymous-namespace helper exposed, which would be a seam added for a path that cannot fail.

Evidence commands (all run in the worktree; logs in the LOG FOLDER):

| Evidence | Command | Result |
| --- | --- | --- |
| Track-creation arms, Event/Video refusal, Pattern path | `rg -n "case Type::Pattern: t = new \|default: break\|case Type::Event\|case Type::Video" src/core/Track.cpp` plus the ControlCommandsArrangement and DawProjectSession greps, in one logged run | EXIT=0; `logs/001-source-audit.log` |
| `feature_check.py` as written | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-127 > .../logs/002-feature-check.log 2>&1; echo EXIT=$?` | EXIT=1, before any gate ran: `PermissionError` creating `projects/zene-studio-program/zene-qwen-new-p21-127-feature-check` (`logs/002-feature-check.log`). The gate's own output directory is outside the write-jail. |
| Six non-ratchet gates, run one by one from the worktree (the same commands `feature_check.py` runs) | `bash tests/fork-sources-gate.sh`; `bash tests/all-sources-reproduce.sh`; `bash tests/no-upstream-regression-gate.sh`; `tests/scripted/check-namespace`; `bash tests/no-tautology-gate.sh`; `python3 tests/doc-figures-gate.py` | EXIT=0 each (`logs/003` to `logs/008`) |
| Ratchet gates, run one by one (the same commands `feature_check.py` runs) | `bash tests/complexity-gate.sh --check`; `bash tests/file-length-gate.sh --check` | EXIT=1 each (`logs/009-complexity.log`, `logs/010-file-length.log`). Every listed regression is in a file this branch does not change; the only changed file is the report. |
| Completion check | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-127 new-p21-127` | EXIT=1 before commit (`logs/011-submit-check-1.log`). Failing items: no commit yet, untracked report, and `gates (feature_check.py)`, which is the same write-jail refusal as above. |

Files committed for this verdict: `docs/qwen-fixes-2026-10/cluster-new-p21-127.md`. No production or test file changed.

## Gates

`feature_check.py` could not produce its own PASS/FAIL table: it tries to create `<worktree>-feature-check` in the projects folder, which the write-jail refuses. I ran its six simple gates and its two ratchets individually, with the same commands and the same worktree. The six simple gates all exited 0. The two ratchets exited 1, listing regressions in `src/wasm/WasmSandbox.cpp`, `plugins/Vst3Effect/Vst3Host.cpp`, `src/core/TakeLane.cpp`, `src/core/DawProjectReadTracks.cpp`, `src/core/ControlCommands*.cpp`, `tests/src/gui/XyPadPairTest.cpp`, `tests/src/gui/ArrangerTimelineTest.cpp` and others. None of them is in this branch's changed set, which is one new docs file. `feature_check.py` only fails on items in changed files, so by its rule both ratchet rows should PASS. That is my reading of the script, not a table it printed. A table from the script itself needs a run outside the write-jail.

## Self-check

- **Regression test fails with the change reverted and passes with it.** Not applicable. The verdict is FALSE-POSITIVE, so there is no product change to revert and no test was added. Evidence: the code references in the verdict above, and `logs/001-source-audit.log`.
- **Test is deterministic.** Not applicable, since there is no test.
- **Fix at the cause, with callers and siblings checked.** Not applicable, since there is no fix. The callers (`src/core/DawProjectSession.cpp:515-516`, `src/core/ControlCommandsArrangement.cpp:221`, `:230-231`) and the sibling helper (`src/core/ControlCommandsArrangement.cpp:75-79`) are checked above, and none can receive `nullptr` from the Pattern arm.
- **No collateral damage.** Not applicable, since nothing product-side changed.
- **FALSE-POSITIVE contract evidence.** Given above: `src/core/Song.cpp:1434` (void), `src/core/Track.cpp:303` (base constructor appends), `src/core/TrackContainer.cpp:321-325` (push_back for every type except HiddenAutomation), `src/core/Track.cpp:492` (Pattern arm), `src/core/DawProjectSession.cpp:485-488` (refusal before clearing).
