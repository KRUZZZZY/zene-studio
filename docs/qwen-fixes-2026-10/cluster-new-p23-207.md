Verdict: FALSE-POSITIVE

new-p23-207: FALSE-POSITIVE

Evidence: the finding's "divides by zero" is a float division that yields NaN under IEEE-754, with no trap and no integer-division fault. `totalTracks` is `Engine::patternStore()->tracks().size()` at [PatternClipView.cpp:151](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-207/src/gui/clips/PatternClipView.cpp:151), and `TrackList` is `std::vector<Track*>` ([TrackContainer.h:61](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-207/include/TrackContainer.h:61)), so it is `size_t`. The only division by it is at [PatternClipView.cpp:155](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-207/src/gui/clips/PatternClipView.cpp:155): `float * size_t / size_t * float`, which is float arithmetic. With an empty store this computes `0.0f / 0` = NaN. The NaN is not observable:

- It is stored only in `totalHeightForEmptyTracks` and `totalHeightForTracks` ([PatternClipView.cpp:155-156](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-207/src/gui/clips/PatternClipView.cpp:155)).
- Both are read only inside ternaries gated on the counts: `trackHeight` uses `numberInstrumentTracksUsed > 0` and `emptyTrackHeight` uses `numberEmptyTracks > 0` ([PatternClipView.cpp:158-163](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-207/src/gui/clips/PatternClipView.cpp:158)). With an empty store both counts are 0, so both are `0.f`.
- `verticalNoteSpacing` is `int(trackHeight * m_noteVerticalSpacing)` = 0 ([PatternClipView.cpp:165](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-207/src/gui/clips/PatternClipView.cpp:165)), so no NaN reaches an int conversion.
- The per-track loop that uses `emptyTrackHeight` and `trackHeight` ([PatternClipView.cpp:169-198](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-207/src/gui/clips/PatternClipView.cpp:169)) iterates over the same empty `tracks()` list, so it runs zero times.

Nothing is drawn from the NaN and nothing crashes. The core target is not built with fast-math (the `-ffast-math` flags in the tree are in plugin CMakeLists only), so the IEEE behaviour is the one the code gets. The probe below mirrors lines 151-165 with an empty vector and confirms: `is_iec559=1`, both NaN locals, `trackHeight=0`, `emptyTrackHeight=0`, `verticalNoteSpacing=0`, and 0 loop iterations, at both -O0 and -O2, with exit 0.

The finding's line number is stale: the `~128` anchor is the `gradient()` branch ([PatternClipView.cpp:128](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-207/src/gui/clips/PatternClipView.cpp:128)); the division is at line 155.

A `totalTracks == 0` guard would leave every output unchanged, so no test could fail without it. Under procedure steps 2 and 6 this is the not-real branch: there is no product change, no RED/GREEN pair, and no step 4b patch to reverse.

Adjacent code checked on the same path: `lengthOfPattern()` returns `maxLength.nextFullBar()`, which is at least one bar ([PatternStore.cpp:85-101](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-207/src/core/PatternStore.cpp:85)), so the integer modulos at [PatternClipView.cpp:121](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-207/src/gui/clips/PatternClipView.cpp:121) and [:122](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-207/src/gui/clips/PatternClipView.cpp:122) do not depend on the store being non-empty. Not verified and out of scope: line 122 divides by `pixelsPerPattern`, which is `int(bars * m_ppb)` and could truncate to 0 at a very small zoom. `setPixelsPerBar` does not clamp `m_ppb` ([TrackContainerView.cpp:507-509](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-207/src/gui/editors/TrackContainerView.cpp:507)); I did not check whether the zoom caller bounds it.

| Evidence | Command | Result |
| --- | --- | --- |
| Float-division probe, -O0 build | `g++ -std=c++20 -Wall -Wextra -O0 probe-divzero.cpp -o probe-O0 > .../probe-build-1.log 2>&1; echo BUILD_O0_EXIT=$?` then `./probe-O0 > .../probe-run-O0-1.log 2>&1` | BUILD_O0_EXIT=0, RUN_O0_EXIT=0; output in `logs/probe-run-O0-1.log` |
| Float-division probe, -O2 build | `g++ -std=c++20 -O2 probe-divzero.cpp -o probe-O2 > .../probe-build-2.log 2>&1` then `./probe-O2 > .../probe-run-O2-1.log 2>&1` | BUILD_O2_EXIT=0, RUN_O2_EXIT=0; output in `logs/probe-run-O2-1.log` |
| Every use of `totalTracks` and the NaN locals | `rg -n "totalHeightForEmptyTracks|totalHeightForTracks|emptyTrackHeight|trackHeight\b|totalTracks|numberEmptyTracks" src/gui/clips/PatternClipView.cpp` | EXIT=0; uses at lines 151-198 only, as cited above |
| Integer-division sites in the file | `rg -n " / " src/gui/clips/PatternClipView.cpp` | EXIT=0; line 155 is the only division by `totalTracks` |
| Fast-math in core build | `rg -n -i "fast.math|Ofast|float-divide" --glob 'CMakeLists.txt' --glob '*.cmake'` (excluding `plugins/`) | No core-target match; fast-math appears only in plugin CMakeLists |
| Feature-check wrapper | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > .../gate-feature-check-1.log 2>&1` | EXIT=1 before any gate ran: `PermissionError` creating `zene-qwen-new-p23-207-feature-check` next to the worktree (outside the write jail). The wrapper was not modified. |
| fork-sources gate (run directly, same command as the wrapper) | `bash tests/fork-sources-gate.sh > .../gate-fork-sources-1.log 2>&1` | EXIT=0 |
| all-sources-reproduce gate | `bash tests/all-sources-reproduce.sh > .../gate-all-sources-reproduce-1.log 2>&1` | EXIT=0 |
| no-upstream-regression gate | `bash tests/no-upstream-regression-gate.sh > .../gate-no-upstream-regression-1.log 2>&1` | EXIT=0 |
| check-namespace | `tests/scripted/check-namespace > .../gate-check-namespace-1.log 2>&1` | EXIT=0 |
| no-tautology gate | `bash tests/no-tautology-gate.sh > .../gate-no-tautology-1.log 2>&1` | EXIT=0 |
| doc-figures gate | `python3 tests/doc-figures-gate.py > .../gate-doc-figures-1.log 2>&1` | EXIT=0 |
| complexity ratchet (run directly) | `bash tests/complexity-gate.sh --check > .../gate-complexity-1.log 2>&1` | EXIT=1 (red on the train on purpose); `REGRESSION:` lines are on the base, not in this branch's changed files |
| file-length ratchet (run directly) | `bash tests/file-length-gate.sh --check > .../gate-file-length-1.log 2>&1` | EXIT=1 (red on the train on purpose); same as above |

Both ratchet scripts were run directly because the wrapper could not write its log directory. This branch's only changed file is this report, which is not a product file, so the wrapper's rule (fail only on items in changed files) cannot fail on a `REGRESSION:` line here. The base-listing comparison that the wrapper performs was not executed.

Files committed for this verdict: `docs/qwen-fixes-2026-10/cluster-new-p23-207.md`. No product file changed.
