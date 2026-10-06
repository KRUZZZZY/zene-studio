# Space Bunny fixer lane `core` — batch 22

Six findings, Zene Studio core model. Verdicts: 2 FIXED, 1 REAL-UNTESTED,
2 SKIPPED-SCOPE, 1 FALSE-POSITIVE.

---

### Cluster 5207 — Duplicate parameter names overwrite each other in the alias report

Verdict: FIXED

Evidence: `src/core/ParameterAliases.cpp:163-181`. `toJson()` inserted every alias
under the parameter's display name. Two parameters sharing a display name meant the
later `QJsonObject::insert` replaced the earlier one, so the report could only ever
show one of the two aliases. Confirmed by test: with both display names set to
`Gain` and aliases `Level`/`Drive`, the object had exactly one entry.

Commit: `f7056046f`

Test: `DeviceToolsTest::twoParametersWithTheSameDisplayNameBothKeepTheirAlias`

Red/green:
```
cmake --build build --target zene DeviceToolsTest > build/bunny-b.log 2>&1   EXIT=0
cd build/tests && ctest -R '^DeviceToolsTest$' --output-on-failure           EXIT=8
  FAIL! ... values.size() Actual 1, Expected 2
cmake --build build --target zene DeviceToolsTest > build/bunny-b.log 2>&1   EXIT=0
cd build/tests && ctest -R '^DeviceToolsTest$' --output-on-failure           EXIT=0
```

The fix keeps the existing display-name-first wire format (no schema change): the
first parameter to claim a display name keeps it, and any later parameter with that
same name is reported under its own stable key, so no alias is dropped.

---

### Cluster 5216 — Invalid patch ports silently become port 0

Verdict: FIXED

Evidence: `src/core/PatchWiring.cpp:163-213` (`fromJson`). A port field was only
applied `if (value.isDouble())`; any other present value left the default 0 in
place and the edge was accepted. `from_port: "1"` therefore wired port 0, and
`to_port: 0.5` was silently truncated to 0 by `toInt()`. Because port 0 usually
exists, the edit succeeded and connected a different port than the caller asked
for with nothing downstream objecting.

Commit: `fd9d74972`

Test: `PatcherCommandsTest::aPresentButInvalidPortIsRefusedRatherThanBecomingPortZero`

Red/green:
```
cmake --build build --target zene PatcherCommandsTest > build/bunny-b.log 2>&1   EXIT=0
cd build/tests && ctest -R '^PatcherCommandsTest$' --output-on-failure           EXIT=8
  FAIL! ... '!stringResult.ok' returned FALSE
cmake --build build --target zene PatcherCommandsTest > build/bunny-b.log 2>&1   EXIT=0
cd build/tests && ctest -R '^PatcherCommandsTest$' --output-on-failure           EXIT=0
```

A port field that is absent (or null) still means port 0; a field that is present
but is not an integer fitting an `int` (string, fraction, out-of-range) is now
rejected as malformed input. The validation is a file-local `readPortField()`
helper so `fromJson` stays under the complexity ratchet (CCN 10).

---

### Cluster 5218 — Long timer periods overflow Qt's signed interval

Verdict: REAL-UNTESTED

Evidence: `src/core/PluginEditorHost.cpp:138`. The interval is
`static_cast<int>(std::max<std::uint32_t>(periodMs, 1))`, so a `periodMs` above
`INT_MAX` (e.g. 2147483648) casts to a negative `int`. The requested period is
genuinely mis-signed, as the finding says. However, Qt does not fire a
negative-interval timer inside a short test window, so the claimed symptom
("run it with unintended timing" / fire at once) does not reproduce
headlessly: a regression test asserting "a ~24-day timer does not fire within
200 ms" passed both before and after any fix, so no deterministic RED could be
produced. Per the batch procedure (no deterministic failing test => do not
change product code), the code was left alone.

Commit: none

Test: none (attempted `aTimerPeriodAboveIntMaxDoesNotFireAtOnce`; passed
unfixed, so it guards nothing and was not kept)

Red/green: none

The clamp the finding recommends is a small, correct change, but it is
untestable here and should be taken together with a future rescan/browser work
item rather than as an unverifiable standalone patch.

---

### Cluster 2404 — Rescanning leaves plug-in browser rows pointing at freed cached descriptors

Verdict: SKIPPED-SCOPE

Evidence: `src/core/PluginFactory.cpp:430` replaces the whole cached-descriptor
store (`m_cachedDescriptors = std::make_unique<CachedDescriptorStore>()`) exactly
as `discoverPlugins()` replaces `m_pluginInfos` at `:468`. Browser rows store a
`Plugin::Descriptor*` by value in `PluginKey::Key` (`include/Plugin.h:135-157`,
used by `PluginDescWidget` at `src/gui/PluginBrowser.cpp:324-333`), so rows built
from an earlier scan would dangle after a rescan. The hazard is real. It is also
already documented in `docs/PLUGIN-SCAN-CACHE.md:271-276`: nothing in the product
re-runs the scan (the only trigger is the first `getPluginFactory()`) and the
tests build fresh factories, so this bites a future "Rescan plugins" button.
Fixing it means changing descriptor ownership/keep-alive across the factory and
the browser, a cross-cutting design change well beyond this batch's scope.

Commit: none

Test: none

Red/green: none

---

### Cluster 2403 — Rescanning can unload libraries used by active plug-in instances

Verdict: SKIPPED-SCOPE

Evidence: `src/core/PluginFactory.cpp:468` (`m_pluginInfos = pluginInfos`)
releases the previous `PluginInfo` list, dropping its `shared_ptr<QLibrary>`
references (`include/PluginFactory.h`, `PluginInfo { ... shared_ptr<QLibrary>
library; ... }`). Active plug-in instances do not retain those references, so a
rescan could unload a library whose code live instances still call. Real, but the
fix requires keeping each library loaded for at least the lifetime of its
instances — a lifetime/ownership design change across `PluginFactory`/`Plugin`,
far larger than a minimal patch and not verifiable by this batch's tests.

Commit: none

Test: none

Red/green: none

---

### Cluster 5220 — Failed plugin loads stay skipped after their cause is fixed

Verdict: FALSE-POSITIVE

Evidence: `src/core/PluginFactory.cpp:207` (`planForCandidate`) returns
`ScanPlan::Skip` for any cached record whose status is not `HasDescriptor`,
including `LoadFailed` (recorded at `:559` with the file's fingerprint). This is
exactly the documented caching behaviour: `docs/PLUGIN-SCAN-CACHE.md:136,159`
specifies the `plugin-scan: ... B known-bad skipped ...` stat and that known-bad
files are skipped while their fingerprint matches. The report's scenario (a
missing dependency is later installed but the plugin file is unchanged) is a real
limitation of a fingerprint that does not track external loader state — but that
is the intended, documented trade-off, not a regression, and "retry cached load
failures" would defeat the scan-cache this codebase deliberately built.

Commit: none

Test: none (existing `PluginScanCacheTest` covers the documented skip behaviour)

Red/green: none

---

Leftovers: cluster 5218 is a real mis-signed cast with no headless way to observe
it — needs the clamp taken alongside a future rescan/browser work item rather than
as an unverifiable patch. Clusters 2404 and 2403 are real lifetime/ownership
design changes already noted in docs/PLUGIN-SCAN-CACHE.md; both should be handled
if/when a "Rescan plugins" button is added. Cluster 5220 is by-design caching.