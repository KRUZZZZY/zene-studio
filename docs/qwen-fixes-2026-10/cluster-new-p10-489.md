# Cluster new-p10-489 — Rejected zero-port project settings still erase existing pin connections

**Verdict: FIXED**

## Finding
`src/core/AudioPortsModel.cpp` (`AudioPortsModel::loadSettings`): when a project
carries `<pins inputs="0" outputs="0">`, `parseAttributeInRange` accepts both
counts (0 is inside `[0, MaxChannelsPerAudioBuffer]`), so `setChannelCounts(0, 0)`
is called — and `setProcessorChannelCountsImpl` refuses it
("At least one port count must be non-zero", AudioPortsModel.cpp:175-179).
Loading then fell through to `m_in.loadSettings(...)` / `m_out.loadSettings(...)`,
and `Matrix::loadSettings` clears every pin before applying the file's
attributes (AudioPortsModel.cpp:530-534). An initialized processor therefore
silently lost its existing routing even though the counts were rejected.

## Fix
`src/core/AudioPortsModel.cpp` (`loadSettings`): only load the file's matrices
when the file's counts are actually in effect after the count application
(`in().channelCount() == inputs && out().channelCount() == outputs`). A rejected
load (0/0 against an initialized model, or an out-of-range count) no longer
touches the pin matrices. Behaviour-preserving otherwise: accepted counts still
load both matrices, and the existing out-of-range tests keep passing because
their assertions hold on the untouched default connections.

## Evidence
- `src/core/AudioPortsModel.cpp:238` — `if (countsInRange) { setChannelCounts(inputs, outputs); }`
  followed unconditionally by the two matrix loads (pre-fix).
- `src/core/AudioPortsModel.cpp:175-179` — `setProcessorChannelCountsImpl` rejects
  `inCount == 0 && outCount == 0`.
- `src/core/AudioPortsModel.cpp:530-534` — `Matrix::loadSettings` fills all pins with
  false before applying attributes.
- Regression test: `tests/src/core/AudioPortsModelTest.cpp::rejectedZeroPortCountsKeepExistingPins`
  (empty and missing matrices variants).

## Commands and exit codes (logs in ~/.local/share/zene-bunny-fix/qwen/runs/new-p10-489.r1/logs)
RED (original code):
- `cmake --build build --target AudioPortsModelTest > LOGS/build-1.log 2>&1` — background task status: succeeded
- `cd build/tests && ctest -R '^AudioPortsModelTest$' --output-on-failure > LOGS/test-1.log 2>&1` — **EXIT=8**
  - `FAIL! : AudioPortsModelTest::rejectedZeroPortCountsKeepExistingPins() 'model.in().enabled(0, 0)' returned FALSE.` (AudioPortsModelTest.cpp:509)
  - `Totals: 17 passed, 1 failed, 0 skipped, 0 blacklisted, 1219ms` (family: 18 test cases)

GREEN (with fix):
- `cmake --build build --target AudioPortsModelTest > LOGS/build-2.log 2>&1` — EXIT=0
- `cd build/tests && ctest -R '^AudioPortsModelTest$' --output-on-failure > LOGS/test-2.log 2>&1` — **EXIT=0**

Test-catches-bug proof (step 4b):
- `git diff -- src/core/AudioPortsModel.cpp > LOGS/fix.patch && git apply -R LOGS/fix.patch` — applied OK
- `cmake --build build --target AudioPortsModelTest > LOGS/build-3.log 2>&1` — EXIT=0
- `cd build/tests && ctest -R '^AudioPortsModelTest$' --output-on-failure > LOGS/test-3.log 2>&1` — **EXIT=8** (fails without the fix; `Totals: 17 passed, 1 failed`)
- `git apply LOGS/fix.patch` — applied OK
- `cmake --build build --target AudioPortsModelTest > LOGS/build-4.log 2>&1` — EXIT=0
- `cd build/tests && ctest -R '^AudioPortsModelTest$' --output-on-failure > LOGS/test-4.log 2>&1` — **EXIT=0**

Gates:
- `bash tests/complexity-gate.sh --check` — EXIT=0
- `bash tests/file-length-gate.sh --check` — EXIT=0

## Files committed
- `src/core/AudioPortsModel.cpp` (fork-NEW, already listed in tests/fork-sources.txt — no ledger entry needed)
- `tests/src/core/AudioPortsModelTest.cpp` (tests/** — allowed by the divergence gate)
- `docs/qwen-fixes-2026-10/cluster-new-p10-489.md`
