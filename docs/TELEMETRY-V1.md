# Telemetry v1 — opt-in anonymous platform statistics (task #617)

Design source: the KB article **`knowledge/bundles/general/zene-studio-opt-in-telemetry-design.md`**
(*"Zene Studio — opt-in telemetry design (anonymous platform statistics + separate marketing
consent)"*, research-note, 2026-09-11). This document is the implementation report for **design A**
of that article. Every decision below is the design's, not a new one; where the design states a
rationale it is quoted rather than paraphrased.

## 1. What the design said, and what was taken from it

| Design decision | Taken as |
| --- | --- |
| "Payload is a **client-side allowlist**" of coarse buckets and counts | `TelemetryPayload` — a closed 24-key list; the only mutator refuses anything else |
| "**Never**: IP …, stable installation IDs …, free text, file/project names, file paths, plugin names, emails, geolocation" | Enforced structurally by the allowlist, not by review |
| "Consent UX: default **off**; three granular toggles (hardware / feature usage / crash counts)" | `TelemetryConsent{enabled, hardware, featureUsage, crashCounts}`, all false by default |
| "a **'What we send' screen** rendering the exact payload locally with the raw JSON" | `TelemetryConsentDialog`, Help → *Telemetry — what we send…* |
| "a compile-time `-DZENE_TELEMETRY=OFF` **packager kill switch** (the Audacity lesson)" | `option(ZENE_TELEMETRY … ON)`; OFF compiles the client out (proved in §5) |
| "minimal consent log (timestamp, version, schema version, text shown, toggles)" | stored with the toggles in the `telemetry` config class (§2.1) |
| Purpose limitation (UK GDPR Art.5(1)(b) as amended by the DUAA 2025): product-improvement data cannot be reused for marketing profiles; PECR reg.22 needs prior evidenced consent for a project with no sale | **Two separate opt-ins, never one.** Only design A is implemented here. Design B (website newsletter, double opt-in) is a *separate document and a separate decision* and is **not** merged into this feature — see §7 |

Kept rationales (not implemented, but not contradicted): aggregate segment insight from A is
legitimate; no individual marketing profile is ever built from telemetry.

## 2. What was implemented

### 2.1 Consent state and granularity — default off

* `lmms::TelemetryConsent` (`include/Telemetry.h`): master `enabled` plus the three group toggles,
  all default `false`; `maySend()` is `enabled && (hardware || featureUsage || crashCounts)`.
* Persisted with the tree's **existing** mechanism — `ConfigManager`, class `"telemetry"`, attributes
  `enabled`, `hardware`, `feature_usage`, `crash_counts`, `consent_version`, `consent_notice_version`,
  `consent_schema_version`, `consent_timestamp`. No second store was added.
* **Sending is impossible while consent is off.** `Telemetry::submit()` asks a virtual consent gate
  (`consentAllowsSend()`) before it can reach a transport, and the default answer is `maySend()`.
  With the default consent record `submit()` returns `SubmitResult::ConsentOff` and no bytes are
  produced (§4.2). An enabled master with no group is also refused.

### 2.2 The payload and the preview — one object, not two

* `TelemetryPayload` holds a fixed allowlist (§3). `setField()` **returns false and stores nothing**
  for a key that is not on the list, so a project name, a file path, an email, a plugin name or free
  text cannot be expressed in a payload at all.
* `Telemetry::buildPayload()` is the only payload builder. `previewJson()` is
  `buildPayload().toJsonBytes()`, and `submit()` sends **the same call's bytes** — the preview and
  the wire payload cannot drift because there is only one of them. A test pins it (§4.4).
* The UI preview is `TelemetryConsentDialog`, which renders `previewJson()` (pretty-printed) live as
  the toggles change, and states the on/off consequence in words.

### 2.3 The packager kill switch

* `-DZENE_TELEMETRY=OFF` (CMake option, default ON) undefines `ZENE_TELEMETRY_ENABLED` in the
  generated `lmmsconfig.h`. With it off:
  * `src/core/TelemetryNetworkTransport.cpp` (the only telemetry networking code) is not compiled
    and Qt Network is not required for it;
  * `src/gui/TelemetryConsentDialog.cpp` is not compiled and the Help-menu entry is not added;
  * `src/core/Telemetry.cpp` compiles to **one symbol**, `Telemetry::isCompiledIn()` returning
    `false`. There is no payload builder, no consent store, no `submit()`, no transport, no send
    path in the object at all.
* The tests that exercise the client are compiled out with it (they cannot exist without it); what
  remains in that build is the assertion that the feature is not present.

### 2.4 No network in tests, ever

* The transport sits behind `lmms::TelemetryTransport` (`send(QByteArray)`, `describe()`).
  Every test injects a `RecordingTransport` that records and never touches a socket; the tests
  assert on what *would* have been sent.
* The production transport, `TelemetryNetworkTransport`, ships with an **empty endpoint**: a
  configured-and-consented build still refuses to open a socket until an ingest URL is set
  (`telemetry/endpoint` in the config). No test constructs it.
* No third-party dependency was added. Qt Network is used (already a conditional dependency in this
  tree for the stem-separation model downloader); no MIT/BSD licence check was needed.

## 3. The payload allowlist (24 keys, closed)

Hardware / platform group (17):
`payload_schema_version`, `client_version`, `os_family`, `os_version_bucket`, `cpu_arch`,
`desktop_session`, `cpu_vendor`, `cpu_class`, `gpu_vendor`, `gpu_class`, `vram_bucket`, `ram_bucket`,
`display_resolution_bucket`, `display_scale`, `audio_backend`, `audio_buffer_size_bucket`,
`audio_sample_rate`.

Feature-usage group (5):
`plugin_vst3_count`, `plugin_clap_count`, `plugin_lv2_count`, `plugin_vst2_count`,
`feature_groups_used`.

Crash group (2):
`crash_count`, `crash_stage`.

Not expressible: IP addresses, installation/session identifiers, hostnames, user names, project or
file names, file paths, plugin names, emails, free text, geolocation.

Example of the exact bytes (captured from the built code; the fixture's values, all four groups on):

```json
{"audio_backend":"alsa","audio_buffer_size_bucket":"unknown","audio_sample_rate":"unknown","client_version":"0.1.0-alpha.2+33fde52","cpu_arch":"x86_64","cpu_class":"8-core","cpu_vendor":"amd","crash_count":"5","crash_stage":"startup","desktop_session":"wayland","display_resolution_bucket":"unknown","display_scale":"unknown","feature_groups_used":"4","gpu_class":"unknown","gpu_vendor":"unknown","os_family":"linux","os_version_bucket":"kernel-7","payload_schema_version":"1","plugin_clap_count":"1","plugin_lv2_count":"0","plugin_vst2_count":"2","plugin_vst3_count":"3","ram_bucket":"16_32gb","vram_bucket":"unknown"}
```

## 4. Proofs

All commands were run in the worktree with exit codes measured unpiped
(`cmd > log 2>&1; echo EXIT=$?`).

### 4.1 Build and tests

```
$ JOBS=4 bash tools/local-ci.sh --build-dir build --jobs 4
configure EXIT=0   (log: build/configure.log)
build EXIT=0   (log: build/build.log)
ctest EXIT=0   (log: build/ctest.log)
ctest totals: 100% tests passed, 0 tests failed out of 26
local-ci: overall exit=0 (0 = every executed step passed)
```

The tree's previous total was 25/25; `TelemetryTest` is the 26th. The configuration is the CI
linux-x86_64 flag set with the printed deviation `-DWANT_QT6=ON` (this box has Qt6 only, no
`qtbase5-dev`), exactly as `tools/local-ci.sh` reports.

### 4.2 Consent off cannot send — with the inverted control

`TelemetryTest::consentOffCannotSend` (see `tests/src/core/TelemetryTest.cpp`):

* default consent + recording fake → `submit()` is `ConsentOff`, `recording.sendCount() == 0`;
* **inverted control**: the same call on a subclass that answers the consent gate "yes"
  (`GateRemovedTelemetry`) → `Sent`, `sendCount() == 1`. So the assertion above is precisely the one
  that fails if the gate is removed from `submit()`.
* `enabledWithoutAnyGroupStillCannotSend` pins the same property for a master-on, no-group record.

### 4.3 The allowlist — with the inverted control

`TelemetryTest::nonAllowlistedFieldCannotEnterThePayload`:

* `setField()` is refused for `project_name`, `file_path`, `plugin_name`, `email`, `ip_address`,
  `installation_id`; the field stays absent from `presentFields()`;
* an allowlisted key (`os_family`) **is** accepted, so the refusal is the allowlist deciding rather
  than a method that refuses everything;
* **inverted control**: on a subclass whose allowlist check is widened
  (`UnrestrictedPayload`), the identical `setField("project_name", "secret")` call succeeds — the
  assertion is sensitive to the allowlist being removed;
* the payload built at full consent contains only allowlisted keys (`everyKeyAllowlisted`).

### 4.4 The preview is the wire payload

`TelemetryTest::previewIsExactlyWhatIsSent`: with full consent, `submit()` is `Sent`,
`sendCount() == 1`, and the recorded bytes are **`QCOMPARE`-equal to `previewJson()`**.

### 4.5 The kill switch — both configure paths

Same build dir, both paths configured and built, exit codes unpiped:

```
ON  configure  -> ON_CONFIGURE_EXIT=0,  ZENE_TELEMETRY:BOOL=ON
OFF configure  -> OFF_CONFIGURE_EXIT=0, ZENE_TELEMETRY:BOOL=OFF
OFF build (lmms + TelemetryTest) -> OFF_BUILD_EXIT=0
```

Compile graph with the switch off: `build_make_refs=0` (no reference to
`TelemetryNetworkTransport` in `build/src/CMakeFiles/lmmsobjs.dir/build.make`) and
`transport_object=ABSENT`.

`nm` on the freshly built binaries, ON vs OFF:

| Symbol class | ON build | OFF build |
| --- | --- | --- |
| `nm` lines | 16838 | 16697 |
| `-i telemetry` | 82 | **2** (`lmms::Telemetry::isCompiledIn()` + the TU's static-init thunk) |
| `NetworkTransport` | 15 | **0** |
| `Telemetry::submit` | 1 | **0** |
| `QNetworkAccessManager` | 3 | **0** |

With the switch off there is no transport type, no `submit()`, and no Qt network symbol reachable
from the telemetry client: the only surviving telemetry symbol is the one that answers "is this
feature present?" with `false`.

`ctest -R TelemetryTest` in the OFF build: `OFF_CTEST_EXIT=0`, 1/1 — and the **full** suite also
passes in the OFF configuration (26 passed, 0 failed; that build is the one the pre-restore cache
held, recorded here because it is useful evidence that the switch does not break the tree).

### 4.6 Behaviour preservation — consent off

**No audio-path file was touched.** `git diff --name-only` since the branch base lists only build
config, the two registries, `src/lmmsconfig.h.in`, `src/gui/MainWindow.cpp` (one Help-menu entry)
and the new telemetry files; nothing under `src/core/audio/`, the mixer, the engine or any DSP
source.

The byte-identical render control is **not applicable and was not invented**: I ran it anyway and
found that this box cannot produce one. Renders of a project that uses instruments are **not
deterministic even with the same binary and the same config**:

```
$ md5sum impl.wav impl2.wav impl3.wav          # three consecutive runs, one binary
e88515fcccac0b03866e39dd6ae2b3c4  impl.wav
50e885f82383c2063ba4af991aa24428  impl2.wav
81efe7230ed5e476502316e724db9bbb  impl3.wav
```

The empty project (`tests/emptyproject.mmp`) renders identically across runs
(`463529ea609ee341f5bf4bace07acf66` twice), so the renderer itself is fine — instrument-bearing
projects are the non-deterministic ones. A pre-change vs post-change render comparison therefore
cannot be interpreted on this machine: the same binary disagrees with itself. The behaviour claim
rests on the untouched-diff argument above plus the default-off property, not on a render hash.

### 4.7 Gates and registrations

New sources are registered in `tests/fork-sources.txt` (7 files: 3 headers, 3 implementation files,
and the payload/consent code) and the new test in `tests/all-sources.txt` (the sibling-lane
convention for `tests/**`). `src/gui/MainWindow.cpp` and `src/lmmsconfig.h.in` are declared in
`tests/upstream-modifications.txt` with a reason read off the diff.

| Gate | Exit | Notes |
| --- | --- | --- |
| `bash tests/fork-sources-gate.sh` | **0** | script absent on this branch; run from a temporary copy of the six-lane version, then deleted: 1098 scanned, 106 fork-NEW, 993 inherited, 0 unregistered |
| `bash tests/no-upstream-regression-gate.sh` | **0** | every change to inherited code is declared (32 ledger files) |
| `bash tests/no-tautology-gate.sh` | **0** | the new test has slots and real assertions |
| `bash tests/run-all-gates.sh` | **0** | gates 1,3,4,5,6,7,8 PASS; gate 2 (coverage) SKIP — see the note below |

`run-all-gates.sh` on this branch exits **0** (every executed gate passed, coverage skipped). The expected `3` comes from the "a skipped
gate is not a pass" change (`e77fa7b4b`, *run-all-gates.sh exits 3*), which is **not an ancestor of
this branch** (it lives on `post-alpha/midi-race`). This branch's script records skips as `SKIP`
without failing, so it exits 0 when every executed gate passes. That is a branch-state difference,
not a result of this work; the same run on `post-alpha/integration` would report 3 because coverage
is skipped.

## 5. A real defect the kill switch caught

The first `-DZENE_TELEMETRY=OFF` link **failed**:

```
undefined reference to `lmms::gui::TelemetryConsentDialog::save()'
undefined reference to `lmms::gui::TelemetryConsentDialog::refreshPreview()'
```

AUTOMOC scans `#include` lines without evaluating the preprocessor, so the dialog's `Q_OBJECT`
header was still moc'd while the kill switch had removed the dialog's `.cpp`. Fixed by removing
`Q_OBJECT` from the dialog (every connection in it is already a pointer-to-member connect, so no moc
is needed) and by making the OFF build define exactly one telemetry symbol. This is recorded because
it is the kind of defect the switch exists to surface, and it was found by *building* the OFF path,
not by reading it.

## 6. Files

New: `include/Telemetry.h`, `include/TelemetryNetworkTransport.h`,
`include/TelemetryConsentDialog.h`, `src/core/Telemetry.cpp`,
`src/core/TelemetryNetworkTransport.cpp`, `src/gui/TelemetryConsentDialog.cpp`,
`tests/src/core/TelemetryTest.cpp`.

Touched: `CMakeLists.txt` (option + Qt Network), `src/lmmsconfig.h.in` (switch define),
`src/core/CMakeLists.txt`, `src/gui/CMakeLists.txt`, `tests/CMakeLists.txt`,
`src/gui/MainWindow.cpp` (Help-menu entry), `tests/fork-sources.txt`, `tests/all-sources.txt`,
`tests/upstream-modifications.txt`.

## 7. What is NOT done (and is not pretended to be)

* **Design B (marketing) does not exist here.** The newsletter/double opt-in is a separate document
  and a separate decision, per the design's Art.5(1)(b) / PECR reg.22 argument. Nothing in this
  feature feeds marketing; there is no field from design B in the payload and no join key.
* **No server.** There is no ingest endpoint, no Cloudflare Pages Function, no D1/KV, no
  aggregation, no k-threshold suppression, no retention schedule and no published hardware survey
  page. The client ships with an empty endpoint and therefore sends nothing even when enabled.
* **No local queue.** The design's local-first queue and "one-click off deletes queued local data"
  have nothing to delete yet: v1 holds no local payload store. The consent record is the only thing
  written.
* **Crash counts are not wired to the crash reporter.** The fields exist and are allowlisted, but
  `TelemetryHardware::collect()` reports `crash_count = 0`, `crash_stage = "none"`; nothing counts
  crashes yet.
* **Only a few hardware buckets are collected.** `collect()` fills OS family, a kernel-major bucket,
  CPU arch, session type and a RAM bucket; GPU vendor/class/VRAM, display bucket/scale, audio
  backend/buffer/sample rate and the feature counters default to `"unknown"`/0 until the engine
  surface supplies them. The allowlist, not the collector, is the privacy boundary.
* **The "test-send" button is not implemented.** The screen shows the payload and the consequence;
  it does not offer to send, because there is nowhere to send it.
* **Consent notice text is versioned, not archived.** The log records a notice version and the
  toggle set; the verbatim notice text shown at consent time is not stored.
* **No publication/changelog process** for aggregates (the Steam 2017-18 correction lesson) — it
  belongs with the server that does not exist yet.
