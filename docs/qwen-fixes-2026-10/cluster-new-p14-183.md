Verdict: ALREADY-FIXED

new-p14-183: Non-finite strip settings from project files can poison audio (`src/core/MixerStrip.cpp:359`, historical line; now `:367` for trim)

The finding says a project file with `trim="nan"` reaches `pow()` in the audio path. On this branch the parsed value is rejected before it reaches any model:

- The reader `floatAttribute()` at [MixerStrip.cpp:74-78](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-183/src/core/MixerStrip.cpp:74) parses the attribute with `toFloat()` and returns the default (`fallback`) when the result is not finite (`std::isfinite(value) ? value : fallback`).
- The strip load reads trim through that helper at [MixerStrip.cpp:367](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-183/src/core/MixerStrip.cpp:367). A `nan` trim therefore loads as `0.f`, the same value a missing attribute gets.
- The only audio-side reader of trim is [MixerStrip.cpp:53-55](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-183/src/core/MixerStrip.cpp:53) (`db` → `std::pow(10.f, db / 20.f)`). It only ever sees the finite, clamped value.
- The fix is on the branch: commit `3a8724211` ("reject non-finite mixer XML floats") is an ancestor of HEAD `639e1d23e` (`git merge-base --is-ancestor 3a8724211 HEAD` → yes). It added the `isfinite` guard to `floatAttribute` and the regression test `MixerStripTest::nonFiniteStripSettingsAreReplaced` (`tests/src/core/MixerStripTest.cpp:430-451`, assertion `trim` at line 444).

## Caller and sibling sweep

- Strip readers and writers: the only strip load and save callers are `src/core/Mixer.cpp:2939` (`mixerstrip::load`) and `src/core/Mixer.cpp:2655` (`mixerstrip::save`). Both go through the helper above, so every strip float (trim, eqlow, eqmid, eqmidf, eqhigh, panright, width) gets the same guard. The test already covers each of them (`MixerStripTest.cpp:435-441`).
- Live command path: `mixer.set_strip`-style settings are checked by `finiteStripSettings()` ([ControlCommandsMixerStrip.cpp:229-231](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-183/src/core/ControlCommandsMixerStrip.cpp:229), called at [ControlCommandsMixerStrip.cpp:259](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-183/src/core/ControlCommandsMixerStrip.cpp:259) before the values are applied).
- Model range: `m_trimModel` is `FloatModel{0.f, -24.f, 24.f, 0.1f}` ([Mixer.h:128](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-183/include/Mixer.h:128)), so a finite value is also clamped to ±24 dB.

No change to production code is needed, so none is made. The commit holds this report only.

## Evidence: RED and GREEN, both run on this tree

The regression test is the pre-existing `nonFiniteStripSettingsAreReplaced`. To show it catches the bug, I removed the guard in a scratch edit, rebuilt, and ran it. Then I restored the guard and ran it again.

The worktree was built with `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp`. The ccache config at `~/.cache/ccache-bunny/ccache.conf` points `temporary_dir` at `dsh/home/ccache-tmp-r6`, which this sandbox refuses to write to. The override changes only where ccache stages temp files, not the compiled code.

All logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-183.r1/logs/`.

| Step | Command (run from the worktree unless noted) | EXIT | Result |
| --- | --- | --- | --- |
| Build attempt without the override (`build-1.log`) | `cmake --build build --target zene MixerStripTest -j4 > .../build-1.log 2>&1; echo EXIT=$?` | 2 | Failed in ccache (`Permission denied` on `ccache-tmp-r6`), not in the code |
| Build with the override (`build-2.log`) | `env CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp cmake --build build --target zene MixerStripTest -j4 > .../build-2.log 2>&1; echo EXIT=$?` | 0 | Built |
| BASELINE, unmodified tree (`test-1-baseline.log`) | `cd build/tests && ctest -R '^MixerStripTest$' --output-on-failure > .../test-1-baseline.log 2>&1; echo EXIT=$?` | 0 | 1/1 passed |
| Mutation: `floatAttribute` returns `value` with no finiteness check (`MixerStrip.cpp:77`). Patch saved to `fix.patch` | `git diff -- src/core/MixerStrip.cpp > .../fix.patch` | 0 | Patch is the one-line guard removal |
| RED build (`build-3-red.log`) | `env CCACHE_TEMPDIR=... cmake --build build --target zene MixerStripTest -j4 > .../build-3-red.log 2>&1; echo BUILD_EXIT=$?` | 0 | Built |
| RED test (`test-2-red.log`) | `cd build/tests && ctest -R '^MixerStripTest$' --output-on-failure > .../test-2-red.log 2>&1; echo RED_TEST_EXIT=$?` | **8** | **FAIL**: `MixerStripTest::nonFiniteStripSettingsAreReplaced()` "Compared floats are not the same (fuzzy compare)", `Loc: [MixerStripTest.cpp(444)]` (the `trim` assertion). Other tests in that binary pass (22 passed, 1 failed) |
| Restore the guard | `git apply -R .../fix.patch` | 0 | `git status` clean; `MixerStrip.cpp:77` is `return std::isfinite(value) ? value : fallback;` again |
| GREEN build (`build-4-green.log`) | `env CCACHE_TEMPDIR=... cmake --build build --target zene MixerStripTest -j4 > .../build-4-green.log 2>&1; echo BUILD_EXIT=$?` | 0 | Built |
| GREEN test (`test-3-green.log`) | `cd build/tests && ctest -R '^MixerStripTest$' --output-on-failure > .../test-3-green.log 2>&1; echo GREEN_TEST_EXIT=$?` | 0 | **PASS**: 1/1 (100% tests passed) |
| Family build (`build-5-family.log`) | `env CCACHE_TEMPDIR=... cmake --build build --target RealtimeSnapshotTest MixerToolsTest -j4 > .../build-5-family.log 2>&1; echo BUILD_EXIT=$?` | 0 | Built |
| Family test (`test-4-family.log`) | `cd build/tests && ctest -R '^(MixerStripTest\|RealtimeSnapshotTest\|MixerToolsTest)$' --output-on-failure > .../test-4-family.log 2>&1; echo FAMILY_EXIT=$?` | 0 | 3/3 passed (`MixerStripTest`, `MixerToolsTest`, `RealtimeSnapshotTest`); tests executed: 3 |

RED and GREEN ran on the same tree, with the same test binary rebuilt for each state. The RED failure is at the trim assertion, not at some unrelated check.

## Gates

| Step | Command | EXIT | Result |
| --- | --- | --- | --- |
| feature_check, first try (not logged to a file; output shown in the session) | `python3 .../qwen/feature_check.py <worktree>` | 1 | `PermissionError`: the gate creates `<worktree>-feature-check` as a sibling of the worktree, outside the write-jail, so it never reached its checks |
| feature_check via the wrapper (`gate-1.log`) | `python3 -I .../qwen/runs/new-p14-183.r1/gate_wrap.py <worktree> <LOG FOLDER>/gate-1 > .../gate-1.log 2>&1; echo EXIT=$?` | 0 | `RESULT: PASS`; 8 rows PASS (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length) |

`gate_wrap.py` runs the unchanged `feature_check.py` source with one substitution: the `logs = wt.parent / ...` line becomes a `logs` path inside the LOG FOLDER. Every check is the gate's own. No gate limit or baseline was touched.

## Self-check

Gate row `gates (feature_check.py)` in `submit_check.py` is **FAIL for an environment reason**, not a gate result. `submit_check.py` runs `feature_check.py <worktree>` directly, and that script creates `<worktree>-feature-check` as a sibling of the worktree (`feature_check.py:70-71`). That path is outside the write-jail, so it raises `PermissionError` before any check runs (first run, EXIT 1, traceback shown in the gates table above; that run was not written to a log file). The same eight checks run through `gate_wrap.py` with only the log folder moved (`logs/gate-1.log`, EXIT 0, `RESULT: PASS`). Submit-check output: `logs/submit-1.log`, EXIT 1. The owner or loop should re-run `feature_check.py` from outside the jail to close this row. No gate limit or baseline was changed.

1. **The regression test FAILS with the product change reverted and PASSES with it.** Product change reverted by applying the mutation saved as `logs/fix.patch` (removes the `isfinite` guard at `MixerStrip.cpp:77`): `logs/test-2-red.log`, `ctest -R '^MixerStripTest$'` from `build/tests`, EXIT 8, failure `nonFiniteStripSettingsAreReplaced` at `MixerStripTest.cpp:444`. Restored with `git apply -R logs/fix.patch` (EXIT 0), guard back at `MixerStrip.cpp:77`, rebuilt, `logs/test-3-green.log`, EXIT 0, 1/1 passed. The RED and GREEN runs are on the same test source and the same tree.

2. **Deterministic.** `nonFiniteStripSettingsAreReplaced` (`MixerStripTest.cpp:430-451`) builds an in-memory `QDomElement` with `"nan"` attribute strings, calls `mixerstrip::load`, and compares exact defaults (0, 1000, 1). It uses no clock, sleep, thread or race, and no seam is needed.

3. **Fix at the cause; every caller and sibling checked.** No product change is made in this commit (the finding is already fixed by `3a8724211`, an ancestor of HEAD). The sweep: strip load/save callers `Mixer.cpp:2939` and `Mixer.cpp:2655` both go through `floatAttribute` (`MixerStrip.cpp:74-78`); the command path is guarded by `finiteStripSettings` at `ControlCommandsMixerStrip.cpp:229-231` and its check at `:259`; trim consumers downstream of the model are `MixerStrip.cpp:53-55` (audio gain), `MixerSurroundMix.cpp:248`, and the inspector knob `InspectorChannelRouting.cpp:212`. All of them receive only the finite, clamped value.

4. **No collateral damage.** Nothing in this commit changes behaviour. The guard applies only to a non-finite parsed value, which the fallback replaces with the same default a missing attribute already gets, so old and finite projects load as before (`MixerStripTest` 1/1, `MixerToolsTest` and `RealtimeSnapshotTest` also pass: `logs/test-4-family.log`, EXIT 0, 3/3). Save output for finite values is unchanged (`MixerStrip.cpp:343-363`).

5. **ALREADY-FIXED: the code shows it.** `MixerStrip.cpp:74-78` (guard), `MixerStrip.cpp:367` (trim load through the guard), `MixerStrip.cpp:53-55` (the only audio-side read of trim), and commit `3a8724211` (ancestor of HEAD, checked with `git merge-base --is-ancestor`). The first verdict section above has the full chain.
