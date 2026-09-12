# Final documentation edits — 0.2.0-alpha

Three edits applied to the release documents on `post-alpha/integration`, at tip `3ef822eaf` plus the commits
below. Each row carries the claim it settles, the command that was run against the tree, and that command's
output. Nothing here is quoted from a lane's summary: every command below was run in this worktree by the
editing pass, on the tip named. (One exception is stated where it occurs: the OFF-configuration build has no
directory in this worktree — it was built in the fix lane's — so its numbers are read from that lane's
committed logs, and the report says so rather than pretending a rerun.)

The drafts are in `RELEASE-0.2.0-CHECKLIST.md`, section *"Two more edits for the final pass"*. One of them was
wrong and is corrected here, and one — the coverage paragraph, checklist edit (d) — is deliberately **not**
applied: it needs the final coverage capture's numbers, which are not this pass's to invent.

| # | edit | commit |
|---|---|---|
| (a) | honesty gate: state the measured 6-of-6 PASS on the release configuration | `2b76a09f0` |
| (b) | telemetry: the OFF configuration is built and measured | `b07be9f01` |
| (c) | control surface: the headless `QT_QPA_PLATFORM=offscreen` requirement | `f83e24a4e` |

---

## (a) The honesty-gate sentence — `2b76a09f0`

**Files.** `docs/KNOWN-LIMITATIONS.md` (the "Two features are compiled out" bullet) and
`docs/RELEASE-NOTES-v0.2.0-alpha.md` (freeze item 4, and item 5's trailing sentence, which deferred to it).

**What changed.** Both said the guard's result on the release configuration was pending — limitations: *"its
result on the shipping build is not asserted here … Until that run is observed, read this as the claim the
guard exists to test, not as a green"*; notes: *"not green on either build on this box and is not claimed to
be … until that run has been observed, item 4 is not Done"*. The release-configuration run has happened, so
both now state the measured result and keep the mechanism description. The two non-release directories on this
box (`build-coverage/zene` 3 of 6, `build/zene` 1 of 6) stay in the text, labelled as what they are —
directories configured against the manifest, which the guard is right to fail — rather than as the release's
own result. Nothing was softened: the old sentence was deleted, not hedged.

**Command run (mine, on this tip).**

```bash
$ bash tests/release-honesty-gate.sh --header build/lmmsversion.h --artifacts build; echo "EXIT=$?"
=== release honesty: what the release documents vs what the build contains ===
manifest : tests/advertised-features.tsv
build    : build/lmmsversion.h  (generated header; the exact string the binary prints)
artifacts: build

  [PASS] vst3-hosting     ON matches ON; module build/plugins/libvst3effect.so
  [PASS] vst3-instrument-hosting ON matches ON; module build/plugins/libvst3instrument.so
  [PASS] clap-hosting     ON matches ON; module build/plugins/libclapeffect.so
  [PASS] session-view     OFF matches OFF
  [PASS] wasm-sandbox     OFF,OFF matches OFF
  [PASS] stem-separation  OFF matches OFF

RESULT: PASS — all 6 documented feature(s) are what this build contains
EXIT=0
```

**Committed evidence** (tracked, and the path the documents now cite):
`tests/integration-logs-release-verify/honesty-guard.log` — same six `[PASS]` rows, same `RESULT:` line;
`tests/integration-logs-release-verify/SUMMARY.md` records `PASS | honesty-guard` in the release-verify run.

**The gate also still bites**, so this is a result and not a rubber stamp: the committed log's sibling records
showed `[FAIL] session-view WANT_SESSION_VIEW='ON' …` → `EXIT=1` on a directory where the option was left on,
and `docs/RELEASE-DOCS-FINAL-PASS.md` §5 records `FAIL — 3 of 6` where the hosting modules were never built.

---

## (b) The telemetry kill-switch sentence — `b07be9f01`

**Files.** `docs/RELEASE-NOTES-v0.2.0-alpha.md` (the telemetry bullet, "What else is new") and
`docs/KNOWN-LIMITATIONS.md` (the "Telemetry and privacy" bullet).

**What changed.** The notes cited `docs/TELEMETRY-V1.md` §5 as the proof that `-DZENE_TELEMETRY=OFF` compiles
the client out. **§5 is not retracted** — it records a real link failure (`undefined reference to
TelemetryConsentDialog::save()`; a `Q_OBJECT` header still moc'd after its `.cpp` was compiled out) and is true
of the moment it records. Both documents now say that explicitly and add this release's own measurement,
including the second breakage (`docs/TELEMETRY-V1.md` §5.1) and its repair. The limitations page also stops
asserting a bare "the binary carries no telemetry string": the true statement is about the **debug-stripped**
binary, and the raw count is not zero, because an unstripped binary embeds this checkout's absolute path in its
DWARF strings. That is stated in the text rather than left for a reader to trip over.

**Commands run (mine, on this tip; the OFF build lives in the fix lane's worktree, so these read its committed
logs — no rerun is claimed).**

```bash
$ cat tests/integration-logs-telemetry-off/on-differential-final.txt \
      tests/integration-logs-telemetry-off/off-differential-final.txt
ON  nm: 99
ON  stripped strings: 156
ON  raw strings: 690
OFF nm: 0
OFF stripped strings: 0
OFF raw strings: 385 (all on this checkout's path)

$ diff tests/integration-logs-telemetry-off/on-command-ids.tsv \
       tests/integration-logs-telemetry-off/off-command-ids.tsv
60,61d59
< telemetry.consent
< telemetry.status

$ wc -l tests/integration-logs-telemetry-off/on-command-ids.tsv \
        tests/integration-logs-telemetry-off/off-command-ids.tsv
  74 tests/integration-logs-telemetry-off/on-command-ids.tsv
  72 tests/integration-logs-telemetry-off/off-command-ids.tsv

$ tail -12 tests/integration-logs-telemetry-off/off-kill-switch-gate.log
--- [3/3] differential on build-off/zene ---
  nm -C build-off/zene | grep -ci telemetry        = 0
  strings (debug-stripped) | grep -ci telemetry = 0
  raw strings (DWARF build paths included)  = 385  (of which on this checkout's path: 385)
--- registry assertion (build-off/tests/ControlRegistryTest) ---
ControlRegistryTest EXIT=0   (log: build-off/telemetry-off-registry-test.log)

PASS: -DZENE_TELEMETRY=OFF configures, builds, and its binary carries
      0 telemetry symbol(s) and 0 telemetry string line(s)
      once the debug info (which embeds this checkout's own path) is stripped.

$ tail -3 tests/integration-logs-telemetry-off/off-ctest.log
100% tests passed, 0 tests failed out of 86

Total Test time (real) =  62.87 sec
```

**So the numbers in the text are:** `nm` **99 → 0**, debug-stripped `strings` **156 → 0**, live registry
**74 → 72** with the diff exactly the two `telemetry.*` ids; OFF configures and builds under
`-DUSE_WERROR=ON` and its suite is 86/86. Sources: `docs/TELEMETRY-KILL-SWITCH.md` §3–§7 and the evidence
directory `tests/integration-logs-telemetry-off/`, both cited in the edited text.

**One number from the checklist draft was not used.** Draft wording said the default build "carries 99 and
305". `99` is right; `305` is not reproducible on this tree — the committed ON measurement is `156`
(debug-stripped) and `690` (raw). `docs/TELEMETRY-KILL-SWITCH.md` §4 records the same discrepancy and does not
smooth it over, so the applied text carries the measured pair.

---

## (c) The headless line, and the `--unattended` flag that does not exist — `f83e24a4e`

**File.** `docs/RELEASE-NOTES-v0.2.0-alpha.md`, control-surface section (new paragraph after the "Technically,
it is three things" paragraph, before "What you can do with it").

**What changed.** The section never said that this GUI-linked build needs a Qt platform plugin on a headless
host. It now does, and keys the requirement to `--control-socket`.

**The draft's error.** The checklist wording said *"set `QT_QPA_PLATFORM=offscreen` before `--unattended`"*.
There is **no `--unattended` flag**, and the applied text says so rather than dropping the clause: agent-instance
status comes from `--control-socket` alone (`isAgentInstance()`, `include/UnattendedRun.h`), and an unknown
option is a usage error plus `EXIT_FAILURE`. No document in the tree implied such a flag — the drafted phrase
was the only occurrence of the idea, and it was not applied.

**Commands run (mine, on this tip).**

```bash
$ grep -rn '"--unattended"' src/ include/ tests/ | wc -l
0

$ grep -n "Invalid option" src/core/main.cpp
820:				return usageError( QString( "Invalid option %1" ).arg( argv[i] ) );

$ sed -n '269,274p' src/core/main.cpp          # what usageError does
int usageError(const QString& message)
{
	qCritical().noquote() << QString( "\n%1.\n\nTry \"%2 --help\" for more information.\n\n" )
			   .arg( message ).arg( qApp->arguments()[0] );
	return EXIT_FAILURE;
}

$ grep -n "needs a Qt platform plugin even though the test is guiless" tests/CMakeLists.txt
196:# (D9b/D9a), which needs a Qt platform plugin even though the test is guiless.

$ grep -n "offscreen QPA also hangs here\|xvfb-run" docs/AUTOSAVE-RECOVERY.md
67:and `xvfb-run` (a real X server; the `offscreen` QPA platform made no difference). The run ends
309:export QT_QPA_PLATFORM=xcb        # offscreen QPA also hangs here
317:xvfb-run -a -s "-screen 0 1280x1024x24" \
```

The stderr string the paragraph quotes
(`qt.qpa.plugin: Could not load the Qt platform plugin "xcb"`) is attributed in the text to the release
session's smoke test on a headless box — it is that session's observation, not a measurement this pass could
repeat on this box, which has a display. Everything else in the paragraph is checked above against the tree.

---

## (d) The coverage paragraph — deliberately not applied

The checklist's coverage paragraph needs `build-coverage`'s frozen-tip numbers filled in (`commit <FILL: the
frozen tip>` plus the final capture's file/line counts). Those numbers belong to the capture that is still
running, so no paragraph was written and no number was invented. When they arrive the paragraph is a
self-contained edit to the "Our own test coverage" bullet of `docs/KNOWN-LIMITATIONS.md`.

---

## The tree-level gates, after the edits

Documentation-only commits, so the fork-scoped gates were re-run to confirm nothing moved (all from the
worktree root, exit codes measured unpiped):

| gate | exit | note |
|---|---|---|
| `bash tests/file-length-gate.sh` | **0** | `PASS (ratchet clean; baseline refreshed)` |
| `bash tests/fork-sources-gate.sh` | **0** | `PASS: every tracked source in scope is registered (242 fork-NEW, 1036 inherited, 35 tooling)` |
| `bash tests/no-upstream-regression-gate.sh` | **0** | `PASS: every change to upstream-inherited code since 01148947ea… is declared` (`.md` under `docs/` is an allowed category) |

`git status --porcelain` is empty after every commit.

---

## Stale text left as written, on purpose

- **`docs/INDEPENDENT-NOTES-READ.md`** — the independent reader's audit record. Its item A11 is the finding
  this pass answers; the finding was true of the binaries it read and is a record, not a claim about the
  release. Left untouched per the brief.
- **`docs/AUDIT-FIX-PASS.md`** — the per-item record of the audit-application pass, which quotes the
  superseded wording (*"not green on either build on this box and is not claimed to be"*) and states at §"5"
  that *"both documents say mechanism, not green"*. That sentence was true of that pass and is false of the
  tree now. It was left as a dated record rather than rewritten, because it is the record of a pass and not a
  live claim — but a reader who follows `docs/RELEASE-NOTES-v0.2.0-alpha.md`'s freeze item 5 to it will land
  on the old wording, so it is flagged here.
- **`docs/RELEASE-DOCS-FINAL-PASS.md`** — the same class: a lane's dated report whose §5 records the gate as
  not-green-in-this-worktree. True when written; the release configuration now exists and passes.
