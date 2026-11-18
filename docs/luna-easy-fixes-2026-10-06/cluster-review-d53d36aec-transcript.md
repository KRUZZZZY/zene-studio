# Cluster review d53d36aec — verification transcript

- Worktree: `bugs/bunny-control-2026-10-05`
- Starting revision: `701560e280f06f26d4cf52fd87ca9d5f5e7bddcc`
- Premise checked at `src/core/ControlScaleSupport.cpp:78-84`: the `double` was cast to `int` before any finite or accepted-range check.
- `fff` was unavailable; bounded `rg` searches were used in `src/` and `tests/`.

## Regression-first proof

Added coverage to the existing `ControlNoteScaleVerbsTest::malformedCustomScaleEntriesAreIgnored` case. `1e300` must be discarded while its valid neighboring degree remains; a separate `2147483648.0` input checks `FE_INVALID` on Linux x86-64, where the overflowing conversion sets that flag.

Unfixed implementation:

```text
cmake --build build --target zene ControlNoteScaleVerbsTest
EXIT=0

ctest -R '^ControlNoteScaleVerbsTest$' --output-on-failure
working directory: build/tests
1/1 test failed; malformedCustomScaleEntriesAreIgnored observed FE_INVALID=1 (expected 0).
QtTest totals: 14 passed, 1 failed, 0 skipped.
EXIT=8
```

During iteration, checking the flag around the `1e300` lookup remained red after the first guard because the lookup parses that JSON value too. The final test keeps the `1e300` semantic assertion and uses a separate one-past-`INT_MAX` custom entry for the flag check.

## Fixed implementation

The conversion now follows `std::isfinite` and `0 <= semitones <= 11`; the existing whole-semitone check remains. No shared header changed.

```text
cmake --build build --target zene ControlNoteScaleVerbsTest
EXIT=0

ctest -R '^ControlNoteScaleVerbsTest$' --output-on-failure
working directory: build/tests
1/1 test passed.
EXIT=0

./ControlNoteScaleVerbsTest
working directory: build/tests
QtTest totals: 15 passed, 0 failed, 0 skipped.
EXIT=0
```

Required checks:

```text
bash tests/complexity-gate.sh --check
PASS (check mode; baseline not written)
EXIT=0

bash tests/file-length-gate.sh --check
PASS (check mode; baseline not written)
EXIT=0
```

No full suite or gate sweep was run. The `FE_INVALID` assertion is platform-specific to Linux x86-64; behavior and the regression were not run on other platforms.
