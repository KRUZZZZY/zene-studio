# RESUME NOTES — PLATFORM-DELTA lane (MACOS), branch `030/wplat-mac`

**Base:** `eba78f8ff` (run 35126160372 head). **Commits on this branch (newest last):**

| # | commit | state |
|---|---|---|
| 1 | `dea96ef65` | ControlMasteringCommands (mac arm64) — **fix committed**, CI-proof required |
| 2 | `e021de5dd` | MmpzGitDepthTest (both mac arches) + the mac backtrace step — **fix committed**, CI-proof required |
| 3 | `c1a2f9dcb` | Vst3ChunkProbeTest (both mac arches + msvc-x64) — **fix committed**, CI-proof required |
| 4 | `85103e377` | SafeStartTest (both mac arches) — **fix committed, NOT compiled** (no Qt5/macOS here), CI-proof required |

Nothing is pushed. `git status` is clean apart from RESUME-NOTES.md itself (this commit).

## Logs pulled (all in `/tmp/wplatmac-logs/` on this box — re-pull with the recipe below if gone)

```
macos-arm64  mac-run-tests.log   job 104895806458   (6 failed / 206)
macos-x86_64 mac-x86.log         job 104895806868   (3 failed / 206)
linux-x86_64 linux-run-tests.log job 104895780 (104895806780)  (5 failed / 209)
win_*.log                        msvc 104895806855, mingw 104895806930, win-arm64 104895806707
recipe: zene-gh-token gh api "repos/KRUZZZZY/zene-studio/actions/jobs/<ID>/logs" --allow-escape-sequences
```

Failed set, macos-arm64: 39 ControlShutdownHookTest, 48 SafeStartTest, 152 Vst3ChunkProbeTest,
186 ControlMasteringCommands, 189 ControlGoldenAudio, 205 MmpzGitDepthTest.
macos-x86_64: only 48, 152, 205. macos jobs have NO ctest-log artifact (only msvc uploads one).

---

## 1 · ControlMasteringCommands (186) — COMMITTED `dea96ef65`

* **Root cause (proven from the log).** The ctest line is `/usr/local/bin/python3` (mac log 7708)
  while the provisioning step's own `python3` was `/opt/homebrew/opt/python@3.14/bin/python3.14`
  (6832): `externally-managed-environment` (6786) → `--break-system-packages` retry → "Successfully
  installed numpy-2.5.3 scipy-1.18.1" (6830) into **Homebrew's** python, so the test died for the
  missing module anyway (7253). macos-x86_64 passed because Homebrew IS `/usr/local` there.
  `tools/auto-mastering-demo.py` is loaded **by path in the test's own interpreter**
  (`tests/mastering_probe_lib.py:shipped_fixture()`), so only that interpreter matters.
* **Fix.** The mac step reads `PYTHON3_EXECUTABLE` out of `build/CMakeCache.txt` (the exact value
  `find_program()` at `tests/CMakeLists.txt:1896` feeds every python ctest), provisions it, keeps the
  PEP 668 retry, fails loudly if the cache names no usable interpreter.
* **Proven here:** YAML OK + yamllint EXIT=0; cache extraction against a fixture cache; guard fires
  on an empty value. **Next action:** confirm the mac job's new line prints
  `/usr/local/bin/python3 2.5.3 1.18.1` and 186 is green.

## 2 · MmpzGitDepthTest (205) + the mac backtrace step — COMMITTED `e021de5dd`

* **Root cause (one, two faces).** `timeout` is GNU coreutils, absent on macOS:
  `FileNotFoundError: [Errno 2] No such file or directory: 'timeout'` from
  `test_mmpz_git.py:482 _loads()` (2 errors, mac log 8010-8035), and the same absence makes the
  "Signal-death backtrace" step a no-op on mac (mac log 8067: `line 19: timeout: command not found`)
  — which is why no frame exists for any mac red in this run.
* **Fix.** `_loads()` bounds the DAW call with `subprocess.run(..., timeout=180)` +
  `TimeoutExpired` → an explicit hang failure; the step resolves `timeout`/`gtimeout`/neither once and
  uses it (all three job copies stay byte-identical). File kept at its ratchet value: 966 lines.
* **Proven here:** `BinarySafety` → OK 2 tests EXIT=0 against a stand-in `build/zene`; hang path with
  the bound temporarily 1 s → `AssertionError: ... the DAW hung loading ... (180 s)`, EXIT=1 in
  1.086 s; the three timeout-resolution branches run rc=0; YAML/yamllint/bash -n OK.
* **Next action:** both mac arches 205 green; the mac backtrace step must print lldb output.

## 3 · Vst3ChunkProbeTest (152) — COMMITTED `c1a2f9dcb`

* **Root cause (proven).** The fixture hardcoded the Linux bundle layout and entry point on every
  platform: mac log 7181 "The bundle "vst3-chunk-probe.vst3" couldn't be loaded because its executable
  couldn't be located." (CFBundle) with `linuxmain.cpp.o` in the build log (2618), and msvc
  `LoadLibraryW failed for ...\Contents\x86_64-win\vst3-chunk-probe.vst3` (win_104895806855.log:5320).
  `vst3-test-effect` (which passes on mac) already carries the correct per-platform contract.
* **Fix.** Per-platform subdir/file/entry-main (+ `Info.plist.in` for APPLE, + two configure-time
  contract assertions), mirroring `tests/data/vst3-test-effect/CMakeLists.txt`.
* **Proven here:** three configures against a stubbed SDK (linux / `-DAPPLE=TRUE` / `-DWIN32=TRUE`),
  all EXIT=0; the *generated* `build.make` rules read back: `Contents/MacOS/vst3-chunk-probe` +
  `Contents/Info.plist` + `macmain.cpp`, `Contents/x86_64-win/vst3-chunk-probe.vst3` + `dllmain.cpp`,
  `Contents/x86_64-linux/…so` + `linuxmain.cpp`; generated plist `CFBundleExecutable =
  vst3-chunk-probe`.
* **Next action:** 152 green on both mac arches (and msvc-x64, same defect, same fix).

## 4 · SafeStartTest (48) — COMMITTED `85103e377` (NOT compiled here: no macOS, no Qt5 headers)

* **Mac evidence (no diagnosis in the log).** Both slots failed on BOTH mac arches, silently, with no
  QFATAL/child output/crash report, and the SIGKILL slot — whose child kills itself with an
  uncatchable signal — failing means the child never reached that line as intended. In isolation the
  same binary passes (mac `ctest --rerun-failed -V`: "Totals: 6 passed" — that was
  ControlShutdownHookTest; for SafeStart the -V rerun fails both slots identically).
* **Measured mechanism on linux-x86_64 (provable):** that child ran qtestlib's inherited
  `FatalSignalHandler` (`=== Received signal at function time: 3ms ...` + `QFATAL : ... Received
  signal 11`, linux log 10208/10226) and died by the SIGABRT `qFatal()` raises — not SIGSEGV.
* **Fix (assertions unchanged):** the SIGSEGV child resets the fatal signals to `SIG_DFL` before its
  fault; `waitBounded()` continues a merely-STOPPED child instead of reading it as a death; both
  failure messages decode the wait status in words. File is exactly 500 lines (whole-tree limit) —
  the long reasoning is in the commit message and in the not-yet-written report.
* **Next action / the honest gap:** if macOS's mechanism is a third one (e.g. the child dying inside
  `install()/beginSession()`), the next mac run's message will name it — then fix that, do not widen
  the assertion. Also worth noting: `CrashReporterTest` (47) passes on mac because its child *arms the
  product handler*, which re-raises with SIG_DFL — the same shape this fix gives the SafeStart child.

## 5 · ControlGoldenAudio (189) — ANALYSED, NO FIX YET (mac arm64 only)

* Not a numpy failure (that message belongs to 186) and not a fixture defect:
  `peak envelope delta 1748.015 LSB  limit 1  FAIL` while `frames`, `window count`, `envelope delta
  0.000049 dB` and `level delta +0.000011 dB` are all ok (mac log 7745-7759). Passes on
  macos-x86_64 (44.73 s) and on both linux jobs.
* Reading of the term (`tests/golden_audio_record.py:compare_fingerprints`): the per-window PEAK term
  is compared in LSB and is **not** skipped for windows that are digital silence in the record (only
  the -inf dBFS envelope term is), so 1748 LSB = 0.0533 in normalised amplitude is consistent with a
  small (~-25 dBFS) blip in one window the record has at `0.000000` — one window of the 103.
* **Next action (drafted, not implemented):** in `golden_audio_record.py:compare_fingerprints`
  (359 lines, fork scope, CCN 17 grandfathered — add no branch) print the worst peak window's index,
  the record's value and the measured value, so the next mac run names the window and the direction.
  Then re-run mac arm64 and decide: real arm64 render delta (a product defect) vs a re-record.
  **Do not touch `tests/control-golden-audio.py`** — it is exactly 500 lines.

## 6 · ControlShutdownHookTest (39) — MAC EVIDENCE ONLY, DELIBERATELY NOT TOUCHED

* Mac evidence: `FAIL! : ...aHookSurvivesTheRegistryInstanceItWasRegisteredOn() 'recreated != registry'
  returned FALSE. (destroy() did not build a new instance for instance())` at
  `tests/src/core/ControlShutdownHookTest.cpp:86` in the -j3 run; the isolated `--rerun-failed -V`
  passes (6/6). Identical failure on linux-x86_64 (both the -j3 run and its rerun).
* Root cause: `ControlRegistry::destroy()` deletes and nulls the singleton
  (`src/core/ControlRegistry.cpp:141-145`) and the re-created object *may* land on the same address, so
  comparing raw pointers is unsound on any allocator. Shared with the linux instance (a sibling lane is
  on those), so per the dispatch this file was left untouched to avoid a duplicate edit on the same
  line — **the parent resolves it.** A sound re-derivation: assert the re-created instance's own
  observable state (the surviving hook runs: `ran == 1`, count drains to 0 — already asserted) and drop
  the pointer comparison, or compare a monotonic instance serial if one is ever added (note:
  `include/ControlRegistry.h` is AT the 500-line cap, so a member cannot be added there).

## Gates (run after each commit, unpiped)

```
bash tests/complexity-gate.sh --check                 -> PASS (no regressions)
bash tests/file-length-gate.sh --check                -> PASS (no regressions)
bash tests/file-length-gate.sh --check --scope all    -> FAIL, but NOT on this lane's files:
     pre-existing from other lanes (ScriptBindingsTest 741->752, ScriptEngineTest 627->651,
     ClapHostTest 705 new). SafeStartTest.cpp is 500 lines and is no longer flagged.
```

## Still to do (in order)

1. Write `docs/reports/WPLAT-MAC-REPORT.md` (per item: root cause, fix, evidence, residuals) — the
   whole reasoning above is the draft; commit it.
2. Item 5's window-level diagnostic (small, additive, no new branch), then ask for one mac arm64 run.
3. Re-run the namespace + manifest gates (`fork-sources-gate.sh`, `no-upstream-regression-gate.sh`,
   `all-sources-reproduce.sh`, `unregistered-tests-gate.sh`) on the finished tree.
4. `hotspot:` `tests/src/core/SafeStartTest.cpp` and `tests/src/core/ControlShutdownHookTest.cpp` —
   both are shared with the linux-instance lane; expect merge work in exactly those two files.
