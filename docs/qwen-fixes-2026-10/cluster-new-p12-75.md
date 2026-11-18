# cluster new-p12-75

## Verdict: FIXED

**Finding (Luna patrol, BUG 11):** the classic ZIP writer permitted exactly 65,535
container entries and then wrote `0xFFFF` into the end-of-central-directory count
fields. `0xFFFF` there is the ZIP64 sentinel: a reader that follows the ZIP format
reads it as "the real counts are in the ZIP64 end-of-central-directory record", and
this writer emits no ZIP64 records.

## Evidence (file:line, current tree)

* `src/core/DawProjectZip.cpp:150-151` — `MaxU16 = 0xFFFF`, `MaxU32 = 0xFFFFFFFFLL`:
  the preflight's field limits.
* `src/core/DawProjectZip.cpp:185-190` (was 174-182 before the fix) —
  `containerRefusal()` compared `entries.size() > MaxU16`, so a count of exactly
  65,535 was permitted.
* `src/core/DawProjectZip.cpp:372-373` (was 363-364) — `dawProjectZipWrite()` writes
  `static_cast<quint16>(entries.size())` into both EOCD count fields, so 65,535
  entries produced the sentinel in a container with no ZIP64 record.
* Fix: a new `MaxEntryCount = 0xFFFE` constant (`src/core/DawProjectZip.cpp:153-161`)
  and `containerRefusal()` refusing anything above it. The refusal keeps the
  `"classic ZIP"` wording the existing container test asserts on.

No header changed, so no dependent target needed a rebuild beyond the test target
and `zene` itself.

## Regression test

`tests/src/core/DawProjectInterchangeRoundTripTest.cpp`,
`theZip64EntryCountSentinelIsRefused()`:

* 65,535 entries must be refused, with a `"classic ZIP"` error and no file left
  behind;
* 65,534 entries (one below the sentinel) must still write, and the two EOCD count
  fields in the file's last 22 bytes must read 65,534 — not `0xFFFF`.

## Commands run and their exit codes

Build/test logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-75.r1/logs/`.

RED (regression test added, product code untouched):

```
cmake --build build --target DawProjectInterchangeRoundTripTest -j"$(nproc)" > build-1.log 2>&1
# EXIT=0
cd build/tests && ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > test-1.log 2>&1
# EXIT=8
# FAIL! : DawProjectInterchangeRoundTripTest::theZip64EntryCountSentinelIsRefused()
#   '!dawProjectZipWrite(sentinelCount, many, &error)' returned FALSE.
#   (65535 entries were written with 0xFFFF - the ZIP64 sentinel - in the count
#   fields of an archive that carries no ZIP64 record)
# Totals: 34 passed, 1 failed, 0 skipped, 0 blacklisted
```

GREEN (after the fix):

```
cmake --build build --target DawProjectInterchangeRoundTripTest -j"$(nproc)" > build-3.log 2>&1
# EXIT=0
cd build/tests && ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > test-3.log 2>&1
# EXIT=0  -- 100% tests passed, 0 tests failed out of 1
```

Proof the test catches the bug (`git apply -R` of the product-only patch):

```
git diff -- src/core/DawProjectZip.cpp > fix.patch && git apply -R fix.patch
# EXIT=0
cmake --build build --target DawProjectInterchangeRoundTripTest -j"$(nproc)" > build-4.log 2>&1
# EXIT=0
cd build/tests && ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > test-4.log 2>&1
# EXIT=8  -- the same FAIL! on the refusal assertion; Totals: 34 passed, 1 failed
git apply fix.patch
# EXIT=0
cmake --build build --target DawProjectInterchangeRoundTripTest -j"$(nproc)" > build-5.log 2>&1
# EXIT=0
cd build/tests && ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > test-5.log 2>&1
# EXIT=0  -- 100% tests passed, 0 tests failed out of 1
```

Product target and the rest of the affected family:

```
cmake --build build --target zene -j"$(nproc)" > build-6.log 2>&1
# EXIT=0
cmake --build build --target ProjectContainerEntriesTest ProjectContainerTest ControlProjectArchiveTest -j"$(nproc)" > build-7.log 2>&1
# EXIT=0
cd build/tests && ctest -R "DawProject|Archive|Container|Zip" --output-on-failure > test-7.log 2>&1
# EXIT=0  -- 100% tests passed, 0 tests failed out of 4
```

Test counts: the `DawProjectInterchangeRoundTripTest` ctest entry runs 35 Qt test
functions (34 passed + 1 failing before the fix, 35 passed after); the wider
`DawProject|Archive|Container|Zip` family is 4 ctest entries, all passing.

Ratchet gates (unpiped exit codes):

```
bash tests/complexity-gate.sh --check > gate-complexity-1.log 2>&1   # EXIT=0
bash tests/file-length-gate.sh --check > gate-filelength-1.log 2>&1  # EXIT=0
```

## Files changed (committed together)

* `src/core/DawProjectZip.cpp` — `MaxEntryCount = 0xFFFE` constant and the
  `containerRefusal()` bound that uses it.
* `tests/src/core/DawProjectInterchangeRoundTripTest.cpp` — the regression test.
  No new files were added, so `tests/fork-sources.txt` /
  `tests/upstream-modifications.txt` needed no entry (`src/core/DawProjectZip.cpp`
  is already registered in `tests/fork-sources.txt:1427`).
* `docs/qwen-fixes-2026-10/cluster-new-p12-75.md` — this report.
