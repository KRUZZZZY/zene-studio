# Cluster new-p10-160-port: GigPlayer keyboard dimension selection

**Verdict: FIXED**

Port of commit ed45f4ec3 (bugs/qwen-new-p10-160) onto the newest line. The stale `wantReleaseSample == true &&` guard in `GigInstrument::addSamples` prevented `m_currentKeyDimension` from being updated for normal note starts, so notes selected the wrong (often zone zero) keyboard dimension. The guard is removed so the dimension is computed for every note. Regression: `PluginPortsMigrationTest::gigKeyboardDimensionUsesPressedKey` with `tests/data/gigproof/keyboard-dimension.gig`.

Evidence:
- `plugins/GigPlayer/GigPlayer.cpp:822` now updates `m_currentKeyDimension` whenever the MIDI note is in `DimensionKeyRange`, regardless of `wantReleaseSample`.
- `tests/src/plugins/PluginPortsMigrationTest.cpp` declares and defines `gigKeyboardDimensionUsesPressedKey()` (RED without fix, GREEN with fix).
- `tests/data/gigproof/keyboard-dimension.gig` vendored (sha256 f3412f85f7e6763e837b77271df2c07725a3d792d239097170360c3631009239).
- `tests/upstream-modifications.txt` records the inherited-file change.

## Evidence table

| Check | Exact command and result |
| --- | --- |
| RED before fix | `cd build/tests && QT_QPA_PLATFORM=offscreen ./PluginPortsMigrationTest gigKeyboardDimensionUsesPressedKey > .../logs/test-red.log 2>&1; echo EXIT=$?` **EXIT=1**; assertion `highKeyPeak < 1e-6f` failed ("high note used zone zero instead of its keyboard dimension zone"). |
| GREEN after fix | Same command, output to `test-green.log`; **EXIT=0**, test passed. |
| Reversed-fix proof | `git apply -R .../logs/fix.patch` **EXIT=0**; rebuild **EXIT=0**; test output to `test-red.log` **EXIT=1** (assertion failed). |
| Restored-fix proof | `git apply .../logs/fix.patch` **EXIT=0**; rebuild **EXIT=0**; test output to `test-green.log` **EXIT=0**, test passed. |
| Affected family | `PluginPortsMigrationTest`: full run **EXIT=0** (1/1 CTest target) after building `decimator_1202`; `gigKeyboardDimensionUsesPressedKey` passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check` **EXIT=1** (pre-existing regressions; no new REGRESSION lines introduced by this change). |
| File-length gate | `bash tests/file-length-gate.sh --check` **EXIT=1** (pre-existing regressions; no new REGRESSION lines introduced by this change). |

## Build and test commands

```text
cmake --build build --target zene PluginPortsMigrationTest > .../logs/build-1.log 2>&1; echo EXIT=$?  # EXIT=0
cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^PluginPortsMigrationTest$' --output-on-failure > .../logs/test-2.log 2>&1; echo EXIT=$?  # EXIT=0
```

Files committed:
- `plugins/GigPlayer/GigPlayer.cpp`
- `tests/src/plugins/PluginPortsMigrationTest.cpp`
- `tests/data/gigproof/keyboard-dimension.gig`
- `tests/data/gigproof/make_gig.cpp`
- `tests/data/gigproof/README.md`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-160-port.md`
