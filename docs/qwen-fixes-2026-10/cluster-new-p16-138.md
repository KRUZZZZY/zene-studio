FIXED

# Cluster new-p16-138: malformed boolean attributes silently disable features

Finding (Luna patrol, BUG 4): `ModulationLayer.cpp` read the boolean attributes with `toInt() != 0`
and no success check, so text that is not a number became `false`. A project with `active="bad"`
loaded a modulator that drove nothing; `unipolar`, `pervoice` and `persample` failed the same way.

## Decision

**REAL.** Before the fix, `src/core/ModulationLayer.cpp:139` (`unipolar`), `:140` (`active`),
`:146` (`pervoice`) and `:158` (`persample`) each read `element.attribute(...).toInt() != 0`. For
`"bad"`, `toInt()` returns 0, so the flag became `false` with no error. The RED run below reproduces
it: `loadSettings()` returned true for `active="bad"`.

## Contract

- The writer has only ever emitted `0`/`1` for these attributes, and only `1` for `pervoice` and
  `persample` (written only when on). `src/core/ModulationLayer.cpp:64-65` (`unipolar`, `active`),
  `:82` (`pervoice`), `:94` (`persample`). History: the layer was introduced in `052c8bcc1`, and
  `git log -p -- src/core/ModulationLayer.cpp` shows no other spelling was ever written. So
  `0`/`1` is the complete set of valid values, and existing projects load exactly as before.
- A corrupt block is refused, not repaired. This is the rule the file already applies to a malformed
  `phase` (`aMalformedPhaseIsRefused`) and to out-of-bounds `rate`; `docs/MODULATION.md` §6 states it.

## The fix (src/core/ModulationLayer.cpp)

- `boolAttribute` (`:128`): absent keeps its documented fallback; present and `0`/`1` parses; anything
  else returns false.
- `readSourceFlags` (`:147`) reads `unipolar` (fallback false), `active` (fallback true), `pervoice`
  (fallback false). `readRouteFlags` (`:155`) reads `persample` (fallback false).
- `readSource` (`:160`) / `readRoute` (`:174`) no longer parse those flags; they keep the other fields
  unchanged.
- `readRoutes` (`:188`) keeps the existing route-without-parameter drop, then checks flags on each
  named route before anything is added, so one corrupt named route refuses the whole `<modulator>`
  and nothing is left half-loaded. It was extracted from
  `loadModulator` to keep that function at CCN 8 (the gate's limit is 10; the first version measured 11).
- `loadModulator` (`:215`): `readSourceFlags` joins the phase check (`:220`); the route loop now calls
  `readRoutes` (`:228`), and routes are added only after `addModulator` succeeds, as before.

Callers and siblings checked: `loadModulator` is called only from `loadSettings` (`:567`). `readSource`
and `readRoute` have no other callers (`rg`). No other reader of these XML attributes exists: the other
hits for `unipolar` are JSON in `ControlModulationSupport.cpp` and `ControlCommandsModulator.cpp`, which
the XML path does not touch.

Other behaviour is unchanged: `rate`, `phase`, `shape`, `steps`, `attack`, `trigger`, `depth`, `track`,
`channel`, `chain`, `effect`, `parameter`, `formula`, and the route-without-parameter drop.

Realtime, undo and commands: load path only. Nothing on the audio thread, no new control command, no
undo change.

## Regression test (tests/src/core/ModulationLayerTest.cpp:435, `aMalformedBooleanIsRefused`)

Deterministic: a DOM built in memory, no timing, no seam needed. It checks:
1. `active`, `unipolar`, `pervoice` each set to `"bad"` on a `<modulator>` make `loadSettings` return
   false with `modulatorCount() == 0`.
2. `persample="bad"` on a `<route>` refuses the whole modulator.
3. `persample="bad"` on a route without a parameter leaves that route ignored; it does not discard
   a modulator with a named route.
4. `"0"`/`"1"` read as written, and absent flags keep defaults (checked on a second, valid layer).

## Evidence (exact commands, EXIT codes; logs in `…/logs/`)

| step | command | EXIT | result |
|---|---|---|---|
| build 1 | `cmake --build build --target ModulationLayerTest -j4` | 2 | ccache could not write `/run/user/1000/ccache-tmp` (sandbox); not a code error (`build-1-red.log`) |
| build 2 (RED) | `CCACHE_TEMPDIR=/tmp/ccache-tmp-new-p16-138 cmake --build build --target ModulationLayerTest -j4` | 0 | built (`build-2-red.log`) |
| RED | `cd build/tests && ctest -R '^ModulationLayerTest$' --output-on-failure` | 8 | FAIL `aMalformedBooleanIsRefused`, `Actual (layer.loadSettings(root)): 1`, `Expected (false): 0`, `ModulationLayerTest.cpp(451)` (`test-1-red.log`); 13 passed, 1 failed |
| build 3 (GREEN) | `cmake --build build --target zene ModulationLayerTest ModulationLayerProjectRoundTripTest ModulationRateTest -j4` | 0 | built (`build-3-green.log`) |
| GREEN | `cd build/tests && ctest -R '^ModulationLayerTest$' --output-on-failure` | 0 | 1/1 passed (`test-2-green.log`) |
| family (first fix) | `ctest -R '^(ModulationLayerTest\|ModulationLayerProjectRoundTripTest\|ModulationRateTest)$'` | 0 | 3/3 passed (`test-4-family.log`) |
| 4b proof (first fix) | `git diff -- src/core/ModulationLayer.cpp > fix.patch && git apply -R fix.patch`, rebuild, ctest | 0, 0, 8 | RED at line 451 (`test-5-unfixed.log`); `git apply fix.patch`, rebuild, ctest: 0, 0, 0 (`test-6-refixed.log`) |
| refactor | CCN check: `loadModulator` was CCN 11 (`gate-complexity.log`) | 1 | route loop moved into `readRoutes`; `loadModulator` no longer flagged (`gate-complexity-2.log`) |
| final build | `cmake --build build --target zene ModulationLayerTest ModulationLayerProjectRoundTripTest ModulationRateTest -j4` | 0 | (`build-6-final.log`) |
| final family | `ctest -R '^(ModulationLayerTest\|ModulationLayerProjectRoundTripTest\|ModulationRateTest)$' --output-on-failure` | 0 | 3/3 passed (`test-7-final-family.log`) |
| 4b proof (final) | `git diff -- src/core/ModulationLayer.cpp > fix2.patch`; `git apply -R fix2.patch`; rebuild `ModulationLayerTest`; ctest | 0, 0, 0, 8 | RED at line 451 (`test-8-red.log`) |
| 4b proof (final) | `git apply fix2.patch`; rebuild `ModulationLayerTest`; ctest | 0, 0, 0 | GREEN, 1/1 (`test-9-green.log`) |
| related | `cmake --build build --target ModulatorToolsTest ProjectOpenIntegrityTest InstrumentRoutingTest -j4`; ctest on the three | 0, 0 | 3/3 passed (`test-10-related.log`) |

Affected-family test count: `ModulationLayerTest` (14 test functions, 1 new), `ModulationLayerProjectRoundTripTest`,
`ModulationRateTest` = **3 ctest tests, 3 passed**, 0 failed. Related tests that load modulator XML
(`ModulatorToolsTest`, `ProjectOpenIntegrityTest`, `InstrumentRoutingTest`): 3 passed. The fixture
`tests/data/modulation-layer-fixture.mmp` uses only `unipolar="0|1"` and `active="1"`.

### Gates

`feature_check.py` cannot run in this sandbox: it creates its log folder at
`<worktree>/../zene-qwen-new-p16-138-feature-check` (outside the write-jail; `PermissionError`, see
`gates-1-feature-check.log`), and its base-listing cache lives in the shared qwen folder. So I ran each
of its gate commands directly from the worktree, logging to the LOG FOLDER:

| gate | command | EXIT |
|---|---|---|
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 |
| check-namespace | `tests/scripted/check-namespace` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 |
| complexity | `bash tests/complexity-gate.sh --check` | 1 |
| file-length | `bash tests/file-length-gate.sh --check` | 1 |

The two ratchet gates exit 1 on the train by design. Their items are in files this commit does not
touch: complexity lists six (`XyPadPairTest`, `DawProjectReadTracks`, `ControlCommandsMixerTools`,
`ControlCommandsMixerStrip`, `ProjectRevisions`, `ControlCommandsShortcuts`), and file-length lists the
other over-500 files. Neither lists `ModulationLayer.cpp` or `ModulationLayerTest.cpp`, and the
`loadModulator` item I introduced is gone (`gate-complexity-2.log`). The `feature_check` rule (fail only on
items in changed files) therefore passes for both ratchets, judged by reading the logs; the base-listing
comparison itself could not be computed here.

## Files committed

- `src/core/ModulationLayer.cpp`
- `tests/src/core/ModulationLayerTest.cpp`
- `docs/MODULATION.md` (§6: the boolean rule and the new test name)
- `docs/qwen-fixes-2026-10/cluster-new-p16-138.md` (this report)

No new files, so `tests/fork-sources.txt` and `tests/upstream-modifications.txt` are unchanged.

## Self-check

- **The regression FAILS with the fix reverted and PASSES with it.** RED: `test-8-red.log` (EXIT 8,
  `ModulationLayerTest.cpp(451)`); GREEN: `test-9-green.log` (EXIT 0). Both proven on the final diff
  with `fix2.patch`.
- **Deterministic.** The test builds its DOM in memory and runs no timer, sleep or thread. No seam
  needed; the input is the XML attribute text.
- **Fix at the cause and complete.** The four boolean reads (`unipolar`, `active`, `pervoice`,
  `persample`) are all covered by `readSourceFlags`/`readRouteFlags`; the latter applies to named
  routes before the modulator is added. Unnamed routes remain ignored. Save-side and other XML readers
  were checked as described above.
- **No collateral damage.** The writer has only ever written `0`/`1` (`git log -p`), so every saved
  project still loads the same. The changed refusal applies only to values that never came from this
  writer. The family and related tests pass (6 tests, 0 failed).
- **Verdict FIXED**, so the ALREADY-FIXED / FALSE-POSITIVE evidence line does not apply.
- **Completion check (`submit_check.py`, `submit-check-2.log`, EXIT 1 on the committed tree):** PASS for
  commits on the branch, clean working tree, report present, tests changed. FAIL only for
  `gates (feature_check.py)`: the runner creates `<worktree>/../zene-qwen-new-p16-138-feature-check`
  outside the write-jail (`PermissionError`, `gates-1-feature-check.log`), so it cannot produce its table
  from this sandbox. BLOCKED on that one row; every gate command it runs was run directly (table above),
  and the two ratchets exit 1 only on items in unchanged files. The runner needs a re-run from outside
  the jail to confirm its table.

## Independent QA reviewer follow-up (2026-10-20)

The reviewer found that the first version validated `persample` before dropping a route with no
`parameter`. That changed the old behavior for an inert route, so `readRoutes` now drops unnamed routes
before checking their boolean flags. `aMalformedBooleanIsRefused` covers both a bad `persample` on a
named route (refused) and one on an unnamed route (ignored while its named route still loads).

- Final build: `CCACHE_TEMPDIR=/tmp/ccache-tmp-new-p16-138 cmake --build build --target zene ModulationLayerTest ModulationLayerProjectRoundTripTest ModulationRateTest -j4` — EXIT 0 (`qa-build-4-final.log`).
- Focused ctests, each from `build/tests`:

  ```sh
  ctest -R '^ModulationLayerTest$' --output-on-failure
  ctest -R '^ModulationLayerProjectRoundTripTest$' --output-on-failure
  ctest -R '^ModulationRateTest$' --output-on-failure
  ```

  Each exited 0 with 1/1 passed (`qa-test-4-ModulationLayerTest.log`,
  `qa-test-5-ModulationLayerProjectRoundTripTest.log`, `qa-test-6-ModulationRateTest.log`).
- QA correction reversal: `git diff -- src/core/ModulationLayer.cpp > /tmp/new-p16-138-qa-product.patch`
  and `git apply -R /tmp/new-p16-138-qa-product.patch` each exited 0; rebuilding `zene ModulationLayerTest`
  exited 0 (`qa-build-2-reversed.log`). Then `ctest -R '^ModulationLayerTest$' --output-on-failure`
  from `build/tests` exited 8: `aMalformedBooleanIsRefused` found one loaded modulator, expected two
  (`qa-test-2-reversed.log`). Applying the patch again exited 0; the final build and focused tests above
  passed.
- Gates: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-138`
  exited 0; all gates passed and both ratchets found no new changed-file items (`qa-gate-2-feature-check.log`).

The original completion-check limitation was specific to the fixer's sandbox. The independent review
ran `feature_check.py` successfully, so that limitation is resolved for this review.
