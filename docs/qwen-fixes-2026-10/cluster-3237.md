# Cluster 3237 — malformed LV2 state instance attribute silently targets instance zero

Verdict: **FIXED**

## Finding

`src/core/lv2/Lv2ControlBase.cpp` — `Lv2ControlBase::loadSettings` read the
`instance` attribute of each `<lv2state>` element with
`element.attribute("instance").toInt()`. `QString::toInt()` returns 0 for a
missing or non-numeric attribute, so `<lv2state instance="bad">…</lv2state>`
passed the bounds check and restored the state into processor 0 instead of
being refused.

## Evidence (file:line)

- `src/core/lv2/Lv2ControlBase.cpp:201` (pre-fix):
  `const int instance = element.attribute(QStringLiteral("instance")).toInt();`
  followed by `if (instance >= 0 && static_cast<std::size_t>(instance) < m_procs.size())`
  — a malformed attribute yields 0 and hits instance zero.
- `include/Lv2ControlBase.h` — `loadSettings` declaration; the seam
  `lv2StateInstanceIndex()` is declared beside the save/load group.

## Fix

Extracted the instance decision into `lv2StateInstanceIndex(element, procCount)`
(free function, `namespace lmms`, declared in `include/Lv2ControlBase.h`,
defined in `src/core/lv2/Lv2ControlBase.cpp`) and made it check
`toInt(&ok)`: a missing or non-numeric attribute, a negative index, or an
index beyond the processor count all return -1, and `loadSettings` skips the
element. Valid elements keep their previous behaviour.

## Regression

New slot `malformedStateInstanceAttributeIsRefused()` in the existing
`tests/src/core/Lv2StateTest.cpp`: a `good` element (`instance="1"`, two
processors → 1), a `bad` element (`instance="bad"` → -1), and a `missing`
element (no attribute → -1). Deterministic — it drives the decision seam
`loadSettings` uses, so it needs no installed LV2 plug-in (the host has no
LV2 effect implementing the State interface; the pre-existing round-trip
slot skips that, this slot does not).

## Commands and exit codes (logs in the run's LOG FOLDER)

Baseline (before any change):
- `cmake --build build --target Lv2StateTest > LOGS/build-1.log 2>&1` → BUILD-EXIT=0
- `ctest -R "^Lv2StateTest$" --output-on-failure > LOGS/test-1.log 2>&1` → CTEST-EXIT=0
- `QT_QPA_PLATFORM=offscreen ./Lv2StateTest > LOGS/test-2.log 2>&1` → DIRECT-EXIT=0
  (round-trip slot SKIPs: "no installed LV2 effect implements the State interface")

RED — seam extracted with the pre-fix semantics, test added:
- `cmake --build build --target Lv2StateTest > LOGS/build-2.log 2>&1` → BUILD-EXIT=2
  (compile error: `const QDomElement` discards qualifiers in the test)
- `cmake --build build --target Lv2StateTest > LOGS/build-3.log 2>&1` → BUILD-EXIT=0
- `ctest -R "^Lv2StateTest$" --output-on-failure > LOGS/test-4.log 2>&1` → CTEST-EXIT=8
  (`FAIL! : Lv2StateTest::malformedStateInstanceAttributeIsRefused() Compared values are not the same`)

GREEN — after the `toInt(&ok)` fix (rebuilt every target including the header:
`zene`, `lv2effect`, `lv2instrument`, `Lv2StateTest`):
- `cmake --build build --target zene Lv2Effect Lv2Instrument Lv2StateTest > LOGS/build-4.log 2>&1` → BUILD-EXIT=2
  (`No rule to make target 'Lv2Effect'` — plugin targets are lowercase)
- `cmake --build build --target zene lv2effect lv2instrument Lv2StateTest > LOGS/build-5.log 2>&1` → BUILD-EXIT=0
- `ctest -R "^Lv2StateTest$" --output-on-failure > LOGS/test-5.log 2>&1` → CTEST-EXIT=0
- `QT_QPA_PLATFORM=offscreen ./Lv2StateTest > LOGS/test-6.log 2>&1` → DIRECT-EXIT=0
  (`PASS   : Lv2StateTest::malformedStateInstanceAttributeIsRefused()`)
- `ctest -R "Lv2" --output-on-failure > LOGS/test-7.log 2>&1` → FAMILY-EXIT=0
  (1 test, 100% passed — the Lv2 family is just Lv2StateTest)

Gates:
- `bash tests/complexity-gate.sh --check > LOGS/gate-1.log 2>&1` → CCN-EXIT=0
- `bash tests/file-length-gate.sh --check >> LOGS/gate-1.log 2>&1` → LEN-EXIT=0

## Registration

- `include/Lv2ControlBase.h` and `src/core/lv2/Lv2ControlBase.cpp` were already
  declared in `tests/upstream-modifications.txt`; the BUG 9 clause was appended to
  both reasons (same commit).
- `tests/src/core/Lv2StateTest.cpp` is a product file already listed in
  `tests/fork-sources.txt`; no new files were added.
