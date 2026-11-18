# Cluster 5375 — Truncated extended MIDI headers can be accepted

**Verdict:** ALREADY-FIXED

The finding's bug was fixed on this branch by commit
`2034207a15d4b2dbb6ccdf03458b708449d56043` — `fix(midi): a truncated MThd
header extension is refused, not ignored` — which is an ancestor of the current
`bugs/qwen-5375` HEAD (`99c219f52`). That earlier attempt was UNREPORTED, so this
run verifies the branch state and files the missing report. No production code
was changed.

## Evidence

- `src/core/SmfInterchangeReader.cpp:410-416` — `skipHeaderExtension()` checks
  the skip: `if (headerLength <= 6 || cursor.skip(headerLength - 6)) { return true; }`
  and otherwise reports `"the MThd chunk declares %1 bytes but the file ends before
  them - the header extension it names is truncated"` and returns false.
- `src/core/SmfInterchangeReader.cpp:436` — `parseHeader()` calls
  `if (!skipHeaderExtension(cursor, headerLength, error)) { return false; }`, so a
  failed skip refuses the file.
- `src/core/SmfInterchangeReader.cpp:114-119` — `Cursor::skip()` returns false when
  `remaining() < count`, i.e. exactly the truncated-extension case.
- `tests/src/core/SmfInterchangeRoundTripTest.cpp:482` — the regression
  `aTruncatedHeaderExtensionIsRefused()` writes a file whose MThd length field
  declares 256 bytes but carries only the six header fields before the `MTrk`,
  asserts `!read.ok` with `ControlErrorKind::InvalidArgs` and an `"MThd"` message,
  that `interchange.smf_import` refuses too, and that an honest six-byte header
  still reads.

## Commands run (logs in ~/.local/share/zene-bunny-fix/qwen/runs/5375.r1/logs)

Build the round-trip test target:

    cmake --build build --target SmfInterchangeRoundTripTest > .../build-1.log 2>&1; echo EXIT=$?
    EXIT=0

First ctest run (the SmfInterchangeTest binary was not built yet — 1 of 2 "Not Run"):

    cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R SmfInterchange --output-on-failure > .../test-1.log 2>&1; echo EXIT=$?
    EXIT=8

Build the second target:

    cmake --build build --target SmfInterchangeTest > .../build-2.log 2>&1; echo EXIT=$?
    EXIT=0

Rerun the family (2 tests run, 100% passed):

    cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R SmfInterchange --output-on-failure > .../test-2.log 2>&1; echo EXIT=$?
    EXIT=0

Ratchet gates:

    bash tests/complexity-gate.sh --check > .../gate-complexity-1.log 2>&1; echo COMPLEXITY_EXIT=$?
    COMPLEXITY_EXIT=0
    bash tests/file-length-gate.sh --check > .../gate-length-1.log 2>&1; echo LENGTH_EXIT=$?
    LENGTH_EXIT=0

## Notes

- No RED was produced on this branch: the fix predates this run, so the existing
  regression test passes as-is; the pre-fix behaviour is documented by commit
  `2034207a1` itself ("refused, not ignored").
- Affected-family tests: `ctest -R SmfInterchange` executed 2 tests
  (`SmfInterchangeTest`, `SmfInterchangeRoundTripTest`), both passed.
- Files committed: `docs/qwen-fixes-2026-10/cluster-5375.md` (report only).
