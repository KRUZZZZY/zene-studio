# Cluster new-p12-188

## Verdict: FIXED

**BUG 3 — "Persistence test does not reload the document it saved"** (Luna's patrol,
`tests/src/core/RackZonesTest.cpp`, reported at the historical line 325).

The finding is **real**. The defect is in the test, not in the product: the writer and
the reader agree, but nothing in the suite proved it.

### Evidence

Original code, `tests/src/core/RackZonesTest.cpp:220-297` (before the fix):

* `:220-223` built `doc` and called `Engine::mixer()->saveSettings(doc, root)` — the
  document carries the macro `"Persisted"`, its one target and the two zones this test
  added.
* `:256-258` then loaded a **different** document:
  `loadDoc.setContent(QString::fromUtf8(reloadFixtureXml())); Engine::mixer()->loadSettings(...)`.
  `reloadFixtureXml()` is the hand-written fixture in
  `tests/src/core/RackTestSupport.h:302-325` — macro `"Loaded"`, zones `kick.wav` /
  no sample.
* `:260-274` asserted against **that** fixture's data (`"Loaded"`, `0.25`, `kick.wav`),
  never against `doc`.
* `:283-296` then re-saved and printed `persistence-round-trip` with the text
  `"saved=macro1/target1/zone2 loaded=same re-saved=same"` — a round-trip claim about a
  document that was never reloaded.

The stale comment at the old `:252-255` ("The fixture's chains carry no effects on
purpose … instantiating one from XML starts the plugin loader") was also false:
`initRackFixture()` (`tests/src/core/RackTestSupport.h:222-229`) appends a `RackParamEffect`
to the channel's chain **and** to the rack chain, and that effect is local to the test
binary (no plugin module), so a load instantiates it as a `DummyEffect` rather than
starting the plugin loader.

### The fix

`tests/src/core/RackZonesTest.cpp` only. No product file changed — the round trip was
already correct, so changing product behaviour would have been wrong.

1. `:252-271` **THE ROUND TRIP** — `Engine::mixer()->loadSettings(doc.documentElement())`
   now reloads the document this test saved, and the assertions name the saved data:
   `macrosBefore + 1` macros with `name == "Persisted"`, `value == 0.5f`, one target
   (`chain == kDrivenChain`, `effect == 0`, `parameter == "Gain"`, `low == 0.25f`,
   `high == 0.75f`), `zonesBefore + 2` zones with their keys, chains, sample and the
   full `0..200` velocity range, and the first-match-wins ladder back
   (`resolve(10,150)`, `resolve(100,150)`, `resolve(128,150) == -1`,
   `resolve(100,201) == -1`).
2. `:317-331` the re-save now closes the trip on **that** configuration
   (`persistence-re-saved`).
3. `:333-361` the hand-written fixture is **kept** and still asserted
   (`persistence-reloaded`), relabelled as the reader-facing case: the reader takes a
   document this build did not write. No existing assertion was dropped.

`docs/RACK-MACROS.md:240-242` — the evidence table §5.2 now describes the two labels
accurately.

### Evidence table

| what | command | EXIT |
| --- | --- | --- |
| baseline, original test, unmodified product | `cmake --build build --target RackZonesTest -j$(nproc)` (`build-1.log`) | `0` |
| baseline, original test | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^RackZonesTest$' --output-on-failure` (`test-1.log`) | `0` |
| **old test + a writer that drops `<zone high_key>`** (`src/core/RackZones.cpp`) | `cmake --build build --target RackZonesTest -j$(nproc)` (`build-4.log`) | `0` |
| **old test + broken writer — PASSES, the blind spot the finding names** | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^RackZonesTest$' --output-on-failure` (`test-4.log`) | `0` |
| new test + the same broken writer | `cmake --build build --target RackZonesTest -j$(nproc)` (`build-5.log`) | `0` |
| **new test + broken writer — RED** | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^RackZonesTest$' --output-on-failure` (`test-5.log`) | `8` |
| new test, writer restored | `cmake --build build --target RackZonesTest -j$(nproc)` (`build-6.log`) | `0` |
| **new test, unmodified product — GREEN** | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^RackZonesTest$' --output-on-failure` (`test-6.log`) | `0` |
| final, after the evidence-string edit | `cmake --build build --target RackZonesTest -j$(nproc)` (`build-9.log`) | `0` |
| final, after the evidence-string edit | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^RackZonesTest$' --output-on-failure` (`test-9.log`) | `0` |
| affected family, 7 tests | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R 'Rack\|InstrumentRouting\|InstrumentLayers' --output-on-failure` (`test-10.log`) | `0` — 7/7 passed |
| the binary itself | `QT_QPA_PLATFORM=offscreen ./build/tests/RackZonesTest` (`evidence-2.log`) | `0` — 5 passed, 0 failed |

RED assertion, verbatim from `test-5.log`:

```
FAIL!  : RackZonesTest::theConfigurationPersistsUnderTheRackElement() Compared values are not the same
   Actual   (lowZone->highKey): 127
   Expected (47)              : 47
   Loc: [../../tests/src/core/RackZonesTest.cpp(291)]
```

Final evidence lines (`evidence-2.log`):

```
RACK_MACRO_EVIDENCE persistence-round-trip reloaded the document this test saved: macro=1 name=Persisted value=0.5 target=1 zone=5 resolve(10,150)=3 resolve(100,150)=4 resolve(128,150)=-1
RACK_MACRO_EVIDENCE persistence-re-saved re-saved from the reloaded document: same macro/target/zone/chain counts
RACK_MACRO_EVIDENCE persistence-reloaded macro=1 value=0.25 target=1 zone=2 resolve(40,150)=0 resolve(55,20)=-1 apply_writes=0 (the fixture's chain has no devices)
```

### Why the RED proof is inverted

Step 4b of the procedure reverses **product** changes, and this fix has none — the
defect *is* the missing assertion. The proof that the new test catches the bug and the
old one could not is therefore the inversion: one temporary product defect
(`RackZones::saveSettings` no longer writing `high_key`, reverted with
`git checkout -- src/core/RackZones.cpp` and **not** part of the commit), run against
both versions of the test.

* original test + broken writer → **EXIT 0** (blind spot reproduced: the writer emits
  data the reader cannot restore and the test is green)
* new test + the same broken writer → **EXIT 8** at `RackZonesTest.cpp:291`

The patch used for that swap was `git diff -- tests/src/core/RackZonesTest.cpp >
logs/fix.patch` (`fix.patch`).

### Gates (unpiped, `--check`)

| gate | log | EXIT |
| --- | --- | --- |
| `bash tests/complexity-gate.sh --check` | `gate-complexity-2.log` | `0` |
| `bash tests/file-length-gate.sh --check` | `gate-filelength-2.log` | `0` |
| `bash tests/duplication-gate.sh --check` | `gate-duplication.log` | `0` |
| `bash tests/no-tautology-gate.sh --check` | `gate-no-tautology.log` | `0` |
| `bash tests/fork-sources-gate.sh` | `gate-fork-sources.log` | `0` |
| `bash tests/no-upstream-regression-gate.sh` | `gate-no-upstream-regression.log` | `0` |
| `bash tests/unregistered-tests-gate.sh` | `gate-unregistered-tests.log` | `0` |
| `bash tests/evidence-gate.sh` | `gate-evidence.log` | `0` |

No baseline was re-anchored and no limit was changed.

### Registries

No registry change was needed: `tests/src/core/RackZonesTest.cpp` and
`tests/src/core/RackTestSupport.h` are fork-NEW and already listed in
`tests/fork-sources.txt:2003` and `:2005`. `tests/src/core/RackZonesTest.cpp` is 367
lines, under the 500-line file-length limit.

### Files committed

* `tests/src/core/RackZonesTest.cpp`
* `docs/RACK-MACROS.md`
* `docs/qwen-fixes-2026-10/cluster-new-p12-188.md`

Logs (not committed): `logs/build-{1,4,5,6,9}.log`, `logs/test-{1,4,5,6,9,10}.log`,
`logs/evidence-{1,2}.log`, `logs/fix.patch`, `logs/gate-*.log`.