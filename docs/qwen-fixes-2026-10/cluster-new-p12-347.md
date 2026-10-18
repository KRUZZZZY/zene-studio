# Cluster new-p12-347: a malformed CLAP `clap_entry` callback table crashes the host

**Verdict: FIXED**

## What was wrong

`plugins/ClapEffect/ClapLoader.cpp`'s load ladder resolved the module's
`clap_entry` symbol and then, after only a version check, called
`entry->init(...)` (line 266) and `entry->get_factory(...)` (line 274). It never
checked that those function pointers were non-null, and `Library::reset()`
calls `entry->deinit()`. A loadable module can export a `clap_entry_t` whose
callbacks are null (a malformed/hostile table); calling one is an indirect jump
to address zero and crashes the host instead of returning a typed load error.

The same check already exists in this tree's vendored Carla host
(`plugins/CarlaBase/carla/source/backend/plugin/CarlaPluginCLAP.cpp:3065`,
`plugins/CarlaBase/carla/source/discovery/carla-discovery.cpp:2394`), so the
refusal is the established contract, not an invented behaviour.

## The fix

- `plugins/ClapEffect/ClapLoader.cpp` gained a small
  `entryCallbacksPresent(const clap_plugin_entry_t&)` helper (anonymous
  namespace) and a guard at the top of the ladder, right after `clap_entry` is
  resolved and before the version check:

  ```cpp
  if (!entryCallbacksPresent(*entry))
  {
      status.code = Code::EntryInvalid;
      status.detail = QStringLiteral("'%1' exports an invalid clap_entry (a required callback is null)")
          .arg(modulePath);
      return false;
  }
  ```

  The helper is `entry.init != nullptr && entry.deinit != nullptr &&
  entry.get_factory != nullptr` — all three callbacks the ladder and
  `Library::reset()` invoke.
- `plugins/ClapEffect/ClapLoader.h`: new `Code::EntryInvalid` (appended before
  `Count`, so existing code values are unchanged), its ladder comment updated.
- `plugins/ClapEffect/ClapHost.cpp`: the code list in the load comment updated.
- New one-fault fixture module `clap-test-broken-null-callback` built from
  `tests/data/clap-test-plugin/clap-test-broken.c`
  (`CLAP_TEST_FAULT_NULL_CALLBACK`: current `clap_version`, all callbacks null),
  registered in that directory's `CMakeLists.txt` and in `tests/CMakeLists.txt`.
- `tests/src/plugins/ClapLoaderErrorTest.cpp`: new
  `testNullCallbacksAreEntryInvalid()` (instance path and scan path agree on
  `entry-invalid`), the fixture added to the presence check, the distinct-code
  and scan/instance tables, and the message-shape table.

## Evidence table

| Check | Exact command and result |
| --- | --- |
| Build (fix) | `cmake --build build --target ClapLoaderErrorTest > .../logs/build-2.log 2>&1; echo EXIT=$?` from the worktree root; **EXIT=0**. |
| GREEN before revert | `ctest -R '^ClapLoaderErrorTest$' --output-on-failure > .../logs/test-1-green.log 2>&1; echo EXIT=$?` from `build/tests`; **EXIT=0**, 1/1 CTest target passed. The test binary itself reported `Totals: 16 passed, 0 failed, 0 skipped`, including `testNullCallbacksAreEntryInvalid`. |
| Reverse the fix | `git diff -- plugins/ClapEffect/ClapLoader.cpp plugins/ClapEffect/ClapLoader.h plugins/ClapEffect/ClapHost.cpp > .../logs/fix.patch && git apply -R .../logs/fix.patch; echo EXIT=$?`; **EXIT=0** to write the 87-line patch, **EXIT=0** to reverse-apply. |
| Build (reverted) | `cmake --build build --target ClapLoaderErrorTest > .../logs/build-3-red.log 2>&1; echo EXIT=$?`; **EXIT=0** (test still compiles: it asserts on the published token, not the new enum value). |
| RED | `ctest -R '^ClapLoaderErrorTest$' --output-on-failure > .../logs/test-2-red.log 2>&1; echo EXIT=$?`; **EXIT=8**. The log shows `***Exception: SegFault` at `testNullCallbacksAreEntryInvalid` (frame `#8 ... testNullCallbacksAreEntryInvalid()`), `0% tests passed, 1 tests failed out of 1`. |
| Restore the fix | `git apply .../logs/fix.patch; echo EXIT=$?`; **EXIT=0**. |
| Build (restored) | `cmake --build build --target ClapLoaderErrorTest > .../logs/build-4-green.log 2>&1; echo EXIT=$?`; **EXIT=0**. |
| GREEN after restore | `ctest -R '^ClapLoaderErrorTest$' --output-on-failure > .../logs/test-3-green.log 2>&1; echo EXIT=$?`; **EXIT=0**, 1/1 passed. |
| Affected family | `cmake --build build --target ClapHostTest ClapEffectIntegrationTest ClapBusMapTest > .../logs/build-5-family.log 2>&1` (**EXIT=0**), then `ctest -R 'Clap' --output-on-failure > .../logs/test-4-family.log 2>&1; echo EXIT=$?`; **EXIT=0**, 4/4 passed (`ClapBusMapTest`, `ClapHostTest`, `ClapLoaderErrorTest`, `ClapEffectIntegrationTest`). |
| Product rebuild | `cmake --build build --target zene clapeffect > .../logs/build-6-product.log 2>&1; echo EXIT=$?`; **EXIT=0** (the header's dependents rebuilt). |
| Complexity gate | `bash tests/complexity-gate.sh --check > .../logs/gate-complexity-final.log 2>&1; echo EXIT=$?`; **EXIT=1**. |
| File-length gate | `bash tests/file-length-gate.sh --check > .../logs/gate-filelength-final.log 2>&1; echo EXIT=$?`; **EXIT=1**. |

### Gate failures are pre-existing and not caused by this change

Both ratchets already failed on the **unmodified** tree, before any edit
(`.../logs/gate-complexity-base.log`, `.../logs/gate-filelength-base.log`), both
**EXIT=1**: their committed baselines are stale against HEAD and name over-limit
files/functions unrelated to this cluster (e.g. `src/core/TakeLane.cpp`,
`tests/src/core/DeviceDspTest.cpp`, `dotCentreAlong@tests/src/gui/XyPadPairTest.cpp`).

Proof this change adds no gate regression: the `REGRESSION:` line sets are
byte-identical before and after the edit for both gates, and no `ClapLoader` /
`clap-test` path appears in either final report:

```text
diff <(grep REGRESSION gate-complexity-base.log)  <(grep REGRESSION gate-complexity-final.log);  echo exit=$?  # exit=0
diff <(grep REGRESSION gate-filelength-base.log)  <(grep REGRESSION gate-filelength-final.log);  echo exit=$?  # exit=0
```

The new helper (`entryCallbacksPresent`) has CCN 3 and the anchor `load()`
stays well under the CCN 10 target; the gates are not re-anchored.

Files committed:

- `plugins/ClapEffect/ClapLoader.h`
- `plugins/ClapEffect/ClapLoader.cpp`
- `plugins/ClapEffect/ClapHost.cpp`
- `tests/data/clap-test-plugin/clap-test-broken.c`
- `tests/data/clap-test-plugin/CMakeLists.txt`
- `tests/CMakeLists.txt`
- `tests/src/plugins/ClapLoaderErrorTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p12-347.md`
