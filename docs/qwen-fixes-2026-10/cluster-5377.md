# Cluster 5377 — Trailing partial chunk headers are silently ignored

**Verdict:** ALREADY-FIXED

**Finding:** `src/core/SmfInterchangeReader.cpp` `readTracks()` treated one to
three remaining bytes as a normal end of the chunk list, so a file whose last
chunk header was cut short (a stray byte, or the first three bytes of an `MTrk`
header) parsed successfully and the malformed tail was dropped silently.

**Evidence (current branch, bugs/qwen-5377):**

- `src/core/SmfInterchangeReader.cpp:446` — `readTracks()` now accepts
  end-of-file only when no bytes remain; a 1–3 byte tail is refused as a
  truncated chunk header (`src/core/SmfInterchangeReader.cpp:450-459`):
  `if (cursor.remaining() < 4) { if (cursor.remaining() == 0) { break; } *error
  = "a chunk header is truncated - %1 byte(s) follow the last chunk, too few
  for a chunk header"; return false; }`
- `tests/src/core/SmfInterchangeRoundTripTest.cpp:596` — regression test
  `aTruncatedTrailingChunkHeaderIsRefused` appends 1- and 3-byte partial
  `MTrk` headers and asserts both `smf_read` and `smf_import` refuse them
  (`!read.ok`, `errorKind == InvalidArgs`, message mentions "chunk header"),
  with the clean-tail control still read.

**Already-applied commit on this branch:** `01ef61b6c` —
"fix(midi): a truncated trailing chunk header is refused, not read as end of
file" (touched `src/core/SmfInterchangeReader.cpp` and
`tests/src/core/SmfInterchangeRoundTripTest.cpp`). Its commit message records
the historical RED run: `ctest -R '^SmfInterchangeRoundTripTest$'` EXIT=8 with
`FAIL! ... '!read.ok' returned FALSE. (a 1-byte partial chunk header was
accepted)` on the pre-fix code. That RED was observed by the earlier attempt,
not re-run here (the fix is already in the committed source; reverting it to
reproduce RED would rewrite history).

**Verification on the current code (commands run here, logs in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5377.r1/logs`):**

| Step | Command | EXIT |
|---|---|---|
| Build | `cmake --build build --target zene SmfInterchangeRoundTripTest SmfInterchangeTest` (log `build-1.log`) | 0 |
| Regression family | `ctest -R '^SmfInterchangeRoundTripTest$' --output-on-failure` from `build/tests` (log `test-1.log`) | 0 |
| Sibling family | `ctest -R '^SmfInterchangeTest$' --output-on-failure` from `build/tests` (log `test-2.log`) | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check` (log `gate-1.log`) | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check` (log `gate-2.log`) | 0 |

Affected-family test count: 1 test per family (2 executed total), 100% passed
in each log. No production or test file was changed in this session — the
committed fix and its regression test were verified green as they stand.
