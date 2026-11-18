# Bunny fixes — core — batch 21

Six Luna findings triaged on `bugs/bunny-core-2026-10-05`. Three were real and fixed,
three did not survive reading the current code.

### Cluster 5186 — Modulation can write through a destroyed parameter

Verdict: SKIPPED-SCOPE

Evidence: `targetAlive()` (include/ModulationLayer.h:368-372) compares an atomic token,
then `writeModulatedEntry(entry.model, ...)` dereferences the raw `AutomatableModel*`
(src/core/ModulationLayerPublisher.cpp:189, 222); `destroyed()` only CASes the token to 0
(src/core/ModulationLayerPublisher.cpp:111-160), so it cannot close the check-then-use
window. Making it safe needs deferred reclamation or a lifetime-safe audio handle on the
audio thread — a design change, and not deterministically reproducible headlessly.

Commit: none

Test: none

Red/green: none

### Cluster 1527 — Step sequences advance once per LFO cycle

Verdict: FALSE-POSITIVE

Evidence: `extraShape()` indexes `steps[n % source.stepCount]` with `n = floor(cycles)`
(src/core/ModulationLayer.cpp:282-293) — one value per cycle, which is the documented
behaviour: include/ModulationLayer.h:105 says the steps are "the source's step values, one
a cycle step". `ModulationLayerValueTest::randomStepsAndEnvelope`
(tests/src/core/ModulationLayerValueTest.cpp:136-153) locks it with stepCount=3:
`outputAt(0.5)==1.0`, `outputAt(1.5)==-0.5`, `outputAt(3.5)==1.0` (wrap). The suggested
`floor(phase * stepCount)` would break that existing assertion.

Commit: none

Test: none (existing coverage already asserts the documented behaviour)

Red/green: none

### Cluster 2370 — Formula variable `x` includes the starting phase

Verdict: FALSE-POSITIVE

Evidence: include/ModulationFormula.h:43 defines `x` as cycles since the modulator started,
and `outputAt()` passes exactly that: `source.phase + max(0.0, seconds) * rate`
(src/core/ModulationLayer.cpp:345). `phase` is documented as "0..1, where in its cycle the
modulator starts" (include/ModulationLayer.h:151), so a modulator starting half-way through
its cycle really is 0.5 cycles along. `extraShape()` derives `phase = cycles - floor(cycles)`
(src/core/ModulationLayer.cpp:280-283) from the same value, and the shape vocabulary uses
phase-shifted cycle counts throughout (comment at src/core/ModulationLayer.cpp:279).
`ModulatorToolsTest::formulaSetDrivesTheModulator` asserts `saw(t)` at 1 Hz is 0.5 at 0.75 s.

Commit: none

Test: none

Red/green: none

### Cluster 5176 — Tick remainder can cross sixteenth boundaries

Verdict: FIXED

Evidence: `barsBeatsFrom()` bounded the `.tick` remainder with `m.perBeat - 1`, while the
emitter it has to round-trip against, `split()` / `barsBeatsText()`, always emits a
remainder below `m.perSixteenth` (src/core/MusicalTime.cpp:84-103). In 4/4
(perBeat=48, perSixteenth=12) `positionFromText("1.1.1.47")` parsed and formatted back as
"1.1.4.11". The bound is now `m.perSixteenth - 1`, with a comment explaining it.

Commit: 0abda9a52

Test: RecordingPassTest::aTickRemainderStaysInsideItsSixteenth

Red/green:
- `cmake --build build --target RecordingPassTest zene > build/bunny-b.log 2>&1` — EXIT=0
- `cd build/tests && ctest -R '^RecordingPassTest$' --output-on-failure` — EXIT=8
  FAIL at RecordingPassTest.cpp:206: `'!positionFromText("1.1.1.12")' returned FALSE`
- `cmake --build build --target RecordingPassTest zene > build/bunny-b.log 2>&1` — EXIT=0
- `cd build/tests && ctest -R '^RecordingPassTest$' --output-on-failure` — EXIT=0 (10 cases)
- `bash tests/complexity-gate.sh --check` — EXIT=0; `bash tests/file-length-gate.sh --check` — EXIT=0

### Cluster 5187 — Vendor namespace validation accepts malformed domains

Verdict: FIXED

Evidence: `vendorNamespace()` rejected only `' '` and `'\t'`, so `vendorNamespace("a\nb", 1)`
returned `vendor.a\nb:1`, and `isVendorNamespace()` split on `lastIndexOf(':')`, so
`vendor.example:extra:1` was accepted as a vendor namespace. Both now share a
`malformedDomain()` helper (src/core/NamespaceRegistry.cpp:48-62) that rejects empty
domains and any whitespace or separator character, and `isVendorNamespace()` requires
exactly one separator colon. The contract is unchanged (include/NamespaceRegistry.h:115-118).

Commit: 05f9c645c

Test: NamespaceRegistryTest::vendorNamespaceRefusesMalformedInput

Red/green:
- `cmake --build build --target NamespaceRegistryTest zene > build/bunny-b.log 2>&1` — EXIT=0
- `cd build/tests && ctest -R '^NamespaceRegistryTest$' --output-on-failure` — EXIT=8
  FAIL `vendorNamespaceRefusesMalformedInput` at NamespaceRegistryTest.cpp:315
- `cmake --build build --target NamespaceRegistryTest zene > build/bunny-b.log 2>&1` — EXIT=0
- `cd build/tests && ctest -R '^NamespaceRegistryTest$' --output-on-failure` — EXIT=0 (15 cases)
- `bash tests/complexity-gate.sh --check` — EXIT=0; `bash tests/file-length-gate.sh --check` — EXIT=0

### Cluster 5208 — Generated stable keys can collide with literal parameter keys

Verdict: FIXED

Evidence: `listKeys()` numbered a repeated key with "#2", "#3", ... but only counted
repeats of the same raw key (src/core/ParameterAliases.cpp:72-73, pre-fix), so the suffix
was never checked against another parameter's own key: a device with ports named `gain`,
`gain` and `gain#2` produced `id:gain`, `id:gain#2`, `id:gain#2` — two parameters sharing
one alias key. A new `uniqueKey()` helper skips a suffix another parameter already holds.

Commit: 873f611a0

Test: DeviceToolsTest::generatedKeysNeverCollideWithALiteralParameterKey

Red/green:
- `cmake --build build --target DeviceToolsTest zene > build/bunny-b.log 2>&1` — EXIT=0
- `cd build/tests && ctest -R '^DeviceToolsTest$' --output-on-failure` — EXIT=8
  FAIL: `'keys.at(2) != keys.at(1)' returned FALSE. (id:gain#2)`
- `cmake --build build --target DeviceToolsTest zene > build/bunny-b.log 2>&1` — EXIT=0
- `cd build/tests && ctest -R '^DeviceToolsTest$' --output-on-failure` — EXIT=0 (1/1 passed)
- `bash tests/complexity-gate.sh --check` — EXIT=0; `bash tests/file-length-gate.sh --check` — EXIT=0

Leftovers: Cluster 5186 is left open by design — the audio-thread lifetime redesign is out
of this lane's scope and needs a caller that unloads a device mid-render to prove it.
