# Cluster new-p16-80: unparsable saved levels become zero

**Verdict: FIXED**

`QString::toFloat()` returns `0` for text it cannot parse, and `0` is finite, so the
`std::isfinite` guard did not catch it. A saved `mix="abc"` on a hardware insert loaded as fully dry
(`mix` 0), and a saved `level="abc"` on a cue master loaded as silent (`level` 0). Both loaders now read
the conversion success flag and fall back to the documented default (`mix` 1, cue `level` 1) when the
text is unparsable, as well as when it is non-finite.

## Evidence (file:line, current branch)

- `src/core/HardwareInsert.cpp:227-229`: the `mix` attribute is parsed with `toFloat(&mixOk)`; the value is
  used only when `mixOk && std::isfinite(mix)`, otherwise `Settings{}.mix` (1.f, `include/HardwareInsert.h:76`).
  The sibling `latency` and `measured` reads in the same function already used this flag (`:230-234`).
- `src/core/HardwareIo.cpp:293-295`: the cue `level` attribute is parsed with `toFloat(&levelOk)`; used only
  when `levelOk && std::isfinite(cueLevel)`, otherwise `1.f` (the `CueMaster` default, `include/HardwareIo.h:66`).
- Pre-existing idiom: `src/core/MixerSurround.cpp:229` already reads `toFloat(&ok)` for the surround loader.
- Caller check: `HardwareInsert::load` is called from `ChannelHardwareIo::load` (`src/core/HardwareIo.cpp:182`);
  `hardwareio::loadMixer` is called from project load (`src/core/Mixer.cpp:2899`). Neither signature changed.
- Sibling check: the cue send `level` and `pan` (`src/core/HardwareIo.cpp:96-99`) also parse `"abc"` to `0`,
  but `0` is their declared attribute default ("no send", centred). That is intended and left unchanged.
  `on` (`src/core/HardwareInsert.cpp:224`) reads `toInt() != 0`, so a missing or malformed value means off, which is
  also its default. Neither is part of this finding.

## Regression test

`tests/src/core/HardwareIoTest.cpp:397-425`, `unparsableSavedLevelsUseTheirDefaults`: loads
`<hardwareinsert mix="abc">` and `<cuemix index="0" level="abc">` and asserts the defaults
(`:413` insert mix 1.f; `:423` cue level 1.f). The test first asserts that `"abc"` really is unparsable
(`toFloat(&parsed)` gives `parsed == false`), so it cannot pass for the wrong reason.

## Evidence table

| Step | Exact command | Result |
| --- | --- | --- |
| Build attempt 1 (RED) | `cmake --build build --target HardwareIoTest -j4 > logs/build-1-red.log 2>&1; echo EXIT=$?` | **EXIT=2**. ccache could not write its temp file under `/run/user/1000/ccache-tmp` (permission denied). An environment failure, not a code result. |
| Build 2 (RED) | `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp cmake --build build --target HardwareIoTest -j4 > logs/build-2-red.log 2>&1; echo EXIT=$?` | **EXIT=0** (unfixed code, test added) |
| RED | `cd build/tests && ctest -R '^HardwareIoTest$' --output-on-failure > logs/test-1-red.log 2>&1; echo EXIT=$?` | **EXIT=8**. `24 passed, 1 failed`. `FAIL! HardwareIoTest::unparsableSavedLevelsUseTheirDefaults()`; `Actual (insert.settings().mix): 0`, `Expected (1.f): 1`, at `HardwareIoTest.cpp(413)` |
| Build 3 (GREEN) | `CCACHE_TEMPDIR=... cmake --build build --target zene HardwareIoTest -j4 > logs/build-3-green.log 2>&1; echo EXIT=$?` | **EXIT=0** |
| GREEN | `cd build/tests && ctest -R '^HardwareIoTest$' --output-on-failure > logs/test-2-green.log 2>&1; echo EXIT=$?` | **EXIT=0**, `100% tests passed out of 1` (the log holds this summary line only; ctest suppresses test output on success) |
| 4b revert | `git diff -- src/core/HardwareInsert.cpp src/core/HardwareIo.cpp > logs/fix.patch && git apply -R logs/fix.patch; echo EXIT=$?` | **EXIT=0** (`git diff --stat` then shows only the test file) |
| 4b build (reverted) | `cmake --build build --target zene HardwareIoTest ControlHardwareIo -j4 > logs/build-4-redproof.log 2>&1; echo EXIT=$?` | **EXIT=2**: `ControlHardwareIo` is a ctest name, not a make target. make had already rebuilt `HardwareInsert.cpp.o`, `HardwareIo.cpp.o`, relinked `zene` and `HardwareIoTest` (verified in the log), so it was a harmless target-name error. |
| 4b build (reverted, clean) | `cmake --build build --target zene HardwareIoTest -j4 > logs/build-5-redproof-clean.log 2>&1; echo EXIT=$?` | **EXIT=0** |
| 4b RED proof | `cd build/tests && ctest -R '^HardwareIoTest$' --output-on-failure > logs/test-3-redproof.log 2>&1; echo EXIT=$?` | **EXIT=8**, same assertion: `Actual 0`, `Expected 1` at `HardwareIoTest.cpp(413)` |
| 4b restore | `git apply logs/fix.patch; echo EXIT=$?` | **EXIT=0** |
| 4b build (restored) | `cmake --build build --target zene HardwareIoTest -j4 > logs/build-6-green-proof.log 2>&1; echo EXIT=$?` | **EXIT=0** |
| 4b GREEN proof | `cd build/tests && ctest -R '^HardwareIoTest$' --output-on-failure > logs/test-4-greenproof.log 2>&1; echo EXIT=$?` | **EXIT=0** |
| Affected family | `ctest -N -R Hardware` lists 2 tests: `HardwareIoTest`, `ControlHardwareIo`. `cd build/tests && ctest -R '^ControlHardwareIo$' --output-on-failure > logs/test-5-family-controlhwio.log 2>&1; echo EXIT=$?` | **EXIT=0**. Both family members executed and passed (2/2). |
| Gate (as provided) | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/gate-1-feature-check.log 2>&1; echo EXIT=$?` | **EXIT=1**. Not a gate result: the script creates a log folder next to the worktree (`zene-qwen-new-p16-80-feature-check`) in the projects directory, which is outside the write-jail (`PermissionError`). |
| Gate (copy, logs redirected) | `python3 -I /tmp/fc-new-p16-80/feature_check.py <worktree> > logs/gate-3-feature-check.log 2>&1; echo EXIT=$?` | **EXIT=0**, `RESULT: PASS`: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity (no new item in your files), file-length (no new item in your files). |

The `/tmp` copy of `feature_check.py` differs from the original only in two lines, which I changed to
redirect the log folder and resolve `ROOT` to the original tooling directory (so the existing baseline cache is
used and nothing in the gate logic or limits changes):

```text
< ROOT = Path(__file__).resolve().parent
> ROOT = Path("/home/kruzzzzy/.local/share/zene-bunny-fix/qwen")
< logs = wt.parent / f"{wt.name}-feature-check"
> logs = Path(".../runs/new-p16-80.r1/logs/feature-check")
```

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-80.r1/logs/`.

## Self-check

- **RED / GREEN with the product change reverted and restored**: yes. RED `test-1-red.log` EXIT=8 and
  `test-3-redproof.log` EXIT=8, both `Actual 0 / Expected 1` at `HardwareIoTest.cpp(413)`. GREEN
  `test-2-green.log` EXIT=0 and `test-4-greenproof.log` EXIT=0. The reverted and restored states used
  `logs/fix.patch`, which contains only `src/core/HardwareInsert.cpp` and `src/core/HardwareIo.cpp`.
- **Deterministic**: yes. The test builds its inputs in memory with `QDomDocument`, has no timing, sleeps
  or threads, and resets state at the end with `hardwareio::resetMixer()` (`:424`). No seam is needed:
  `HardwareInsert::load` and `hardwareio::loadMixer` take the DOM element directly.
- **Fix at the cause and complete**: yes. The root cause is that the success flag was ignored in both
  loaders. Both are fixed, and each uses the same flag pattern as its own siblings (latency and measured in
  `HardwareInsert.cpp`; `MixerSurround.cpp:229`). Callers (`HardwareIo.cpp:182`, `Mixer.cpp:2899`) and
  signatures are unchanged. The sibling send level and pan (`HardwareIo.cpp:96-99`) are intentionally left
  alone because `0` is their declared default.
- **No collateral damage**: valid values take the same code path as before (`mixOk`/`levelOk` are true and
  the clamp is unchanged), so every saved value that parses behaves identically. Only unparsable or empty
  text changes, from 0 to the documented default. The app never writes non-numeric text for these
  attributes (they are saved as doubles: `HardwareInsert.cpp:212` for `mix`, `HardwareIo.cpp:269` for cue `level`), so no
  legitimately saved project changes. No limit or check was added.
- **Verdict ALREADY-FIXED / FALSE-POSITIVE**: not applicable (the verdict is FIXED).

## Commit

Files committed: `src/core/HardwareInsert.cpp`, `src/core/HardwareIo.cpp`, `tests/src/core/HardwareIoTest.cpp`,
`docs/qwen-fixes-2026-10/cluster-new-p16-80.md`. No file registration needed: no new file was created.
