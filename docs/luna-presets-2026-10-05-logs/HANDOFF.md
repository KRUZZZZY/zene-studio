# Preset safety lane — WIP handoff to Luna, 2026-10-05

Stopped implementation on the owner's instruction. No product fixes have been applied.
Baseline: `7796ff6b8`; worktree `zene-fix-presets`; branch `bugs/luna-presets-2026-10-05`.

Modified test files:

- `tests/src/core/ControlChainPresetTest.cpp`: device-count mismatch/missing/invalid cases;
  apply with one valid device followed by state lacking its `<effect>` body; unreadable
  chain/export preset overwrites. Failure checks compare device JSON and/or journal top-step
  serial and bytes. Existing target; no registration changes.
- `tests/src/gui/RenderPresetMenuTest.cpp`: corrupt export preset removal and failed Unix
  unlink history checks. These tests were moved here to keep the chain test below the 500
  code-line gate. Existing target; no registration changes. This target has NOT been built.
  It directly calls `controlWriteFileBytes`, so add its declaring `ControlDeviceSupport.h`
  include before building; the test edit remains a labelled WIP.

Permission fixtures first probe whether the permission denial is observable, restore mode
before asserting, and loudly skip on privileged/unsupported runners. Current runner is
uid 1000. No red or green test run has happened yet: source review is not runtime proof.

Source-confirmed leads awaiting red/green verification:

- ID 93: `controlApplyChainPreset` removes all old effects before new-device creation/state
  restoration. `ControlCommandsChainEdit.cpp::applyPreset` also records its action before
  calling the helper. Parent approved this file's ownership. Stage/restore the replacement
  before dropping originals and record the action only after success.
- ID 94: writer's `devices` attribute is ignored by `controlReadChainPreset`.
- IDs 104, 159: chain/export overwrite ignores `controlReadFileBytes` failure.
- IDs 160/1074: export removal deletes before parsing. They are duplicate sightings.
- ID 1073: export removal records its action before an unlink that can fail.

Proposed atomic implementation, not written: use a scratch `EffectChain`, fully create and
restore devices there, then transfer ownership via `effect->setParent(chain)` and
`chain->appendEffect`. Scratch `removeEffect` before transfer prevents its destructor
deleting moved devices. Suppress scratch journalling with an RAII restoration of
`ProjectJournal::isJournalling()/setJournalling()`; verify failed apply leaves its serial
unchanged. `controlInstantiateDevice` appends automatically. Check `isOkay()` as appropriate.
Do not change intentional effect-ID or recorded-checkpoint contracts.

Configuration succeeded, exit 0, logged in `configure.log`:

```sh
PKG_CONFIG_PATH=/home/kruzzzzy/.local/zene-deps/root/usr/lib/pkgconfig:/home/kruzzzzy/.local/zene-deps/root/usr/lib/x86_64-linux-gnu/pkgconfig cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DWANT_QT6=ON -DUSE_WERROR=ON -DTARGET_UARCH=official -DUSE_COMPILE_CACHE=ON -DWANT_DEBUG_CPACK=ON -DWANT_STEM_SPLIT=ON -DWANT_CLAP=ON -DWANT_VST3=ON -DWANT_VST3_TEST_INSTRUMENT=ON -DCMAKE_EXE_LINKER_FLAGS=-Wl,--strip-debug
```

Sources only were copied from baseline `build/clap` and `build/vst3sdk` into this build,
excluding `.git`; baseline had no `_deps` directory. AAF and FFmpeg configured successfully.

Active build, left running:

```sh
cmake --build build --target zene ControlChainPresetTest -j8 > docs/luna-presets-2026-10-05-logs/build-red.log 2>&1; echo EXIT=$?
```

At handoff: tool session `68502`, shell PID `1363153`, cmake PID `1363164`;
last observed progress 21%. Slot B remains occupied until this command ends. Its ongoing
`build-red.log` is intentionally uncommitted while being written. No other build may claim
slot B yet. Build `RenderPresetMenuTest` and required real effect/instrument modules after
this build completes. Do not run tests with an empty plugin catalogue and claim pass.

Next validation from `build/tests`:

```sh
env -u LMMS_PLUGIN_DIR QT_QPA_PLATFORM=offscreen ctest -R '^(ControlChainPresetTest|RenderPresetMenuTest)$' --output-on-failure > ../../docs/luna-presets-2026-10-05-logs/ctest-red.log 2>&1; echo EXIT=$?
```

Capture the failing assertions before product edits, then rebuild affected targets and
capture green. Full suite/gates are parent integration work. Unverified: every regression's
runtime behavior, all fixes, non-Linux permission behavior. No pushes, tags, or rewrites.

## Evidence packaging correction (2026-10-05)

The original `.log` outputs referenced above are preserved as `.log.md` Markdown transcripts,
with their original SHA-256 digests. Historical commands retain their original output filenames.
