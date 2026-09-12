# Integration merges into `post-alpha/integration` — merge train 3D (the last train before the freeze)

Worktree: `projects/lmms-fl-research/zene-pa-integration` (branch `post-alpha/integration`).
Entry tip: **`91d490557`** — train 3C's exit tip (its report commit), which is what the worktree
actually held when the tree settled (`git status --porcelain` empty, no fresh writes).
Exit: the three merge commits (`221686487`, `d7de12677`, `ea0426199`) — no fix-ups — then the
evidence commit `3f9c7d214`, this report, and a one-line correction commit on top of it.
`git log --oneline --merges 91d490557..HEAD` and `git log --oneline -6` are the authoritative
record of which commits exist; a report cannot name its own tip without going stale, so it does not.

Nothing was pushed; no remote, PR, issue or **tag** was touched; `origin` (LMMS/lmms) and `messmerd`
were never contacted; no branch was rebased, amended, reset or rewritten; nothing was staged with
`git add -A` (every `git diff --cached --name-only` was read before every commit). Every exit code
below was measured unpiped (`cmd > log 2>&1; echo EXIT=$?`) and every log is committed under
`tests/integration-logs-3d/` — never `/tmp`. One build directory (`build/`), `df -h`: 60 GB free at
entry, 59 GB at exit.

Gate 9 = `tests/fork-sources-gate.sh` · Gate 6 = `tests/no-upstream-regression-gate.sh` ·
`run-all-gates.sh` exits **3** = `PASS-WITH-SKIPS` (gate 2 coverage needs `--with-coverage`), which is
the expected result and is **not** a pass. The runner is at **ten** gates (3B's Gate 10 included).

## The three merges, in the order the parent specified

| # | Lane | Merge commit | Conflicts | Ledger entries | fork / all / tools | Gate 9 | Gate 6 | `run-all-gates.sh` | ctest |
|---|------|--------------|-----------|----------------|--------------------|--------|--------|--------------------|-------|
| 1 | `post-alpha/instrument-hosting-impl` | `221686487` | 2 (both manifests) | 437 | 175 / 1,208 / 18 | 0 | 0 | 3 (10 gates) | 70/70 |
| 2 | `post-alpha/instrument-view-safety` | `d7de12677` | 1 (the ledger) | 438 | 175 / **1,209** / 18 | 0 | 0 | 3 (10) | 70/70 |
| 3 | `post-alpha/release-prep` | `ea0426199` | 1 (the ledger) | **439** | 175 / 1,209 / 18 | 0 | 0 | 3 (10) | 70/70 |

The order mattered exactly as briefed: after merge 1 landed, `instrument-view-safety`'s merge base
became `5c69b514f` (its own branch point), so merge 2 carried only its **five own commits**, not the
seven it carries against `f32dc7cd1`. `release-prep` landed last and rewrote the version, the
documents and the capability contract.

`git log --oneline --merges 91d490557..HEAD` returns exactly those three commits and nothing else;
all three named branches are ancestors of HEAD (`git merge-base --is-ancestor`, YES ×3). **No branch
outside the list was merged** and none was touched. Suites: **70 (entry) → 70 → 70 → 70** — see
finding 1 for why the number does not move and what was done about it.

## Method

Every conflict in this train was in an append-only registry (`tests/upstream-modifications.txt`) or an
append-only manifest (`tests/fork-sources.txt`, `tests/all-sources.txt`). **No conflict was resolved
by unioning lines across markers.**

3C's tools were carried forward under `tests/integration-logs-3d/tools/` (`resolve_pair.py`,
`regen.py`, `precommit_check.py`, `verify_union.py`, `merge_ledger.py`; `regen.py`'s transcribed fork
command was amended once, see merge 1), plus this train's own
`tests/integration-logs-3d/run-merge-checks.sh` — the six-step per-merge procedure (build+ctest,
Gate 9, Gate 6, the ten-gate runner, the manifest re-derivations, and a final `git status` to catch a
mutant the mutation gate failed to restore). It was run after **all three** merges and once more on
the finished tree (`final/`).

Two rules did real work again:

* **Re-derive every manifest from its own documented command after every merge** (before the commit
  against the index, and again against HEAD afterwards). Verdict `ALL-REPRODUCE` ×3 at **all three**
  merges. The reproduction check is what found both registration gaps this train had (below).
* **A pre-commit gate script reads HEAD, so it is not a pre-commit check.**
  `tests/no-upstream-regression-gate.sh` computes `git diff --name-only "$BASE"..HEAD`, so before a
  merge commit it reports the pre-merge HEAD. Gate 6's rule was therefore replayed **against the
  index** before every commit (`precommit_check.py INDEX`) and the real script was run again
  afterwards. `PRECOMMIT_EXIT=0`, `GATE6-REPLICA violations: NONE`, `cross-manifest entries: NONE`
  at every merge.

## Manifest reproducibility

| after merge | `fork-sources.txt` | `all-sources.txt` | `tools-sources.txt` | verdict |
|---|---|---|---|---|
| entry (`91d490557`) | 167 | 1,199 | 18 | inherited from 3C |
| 1 instrument-hosting-impl | **175** | **1,208** | 18 | `REPRODUCES` ×3 |
| 2 instrument-view-safety | 175 | **1,209** | 18 | `REPRODUCES` ×3 |
| 3 release-prep | 175 | 1,209 | 18 | `REPRODUCES` ×3 |
| final tip (`ea0426199`) | 175 | 1,209 | 18 | `ALL-REPRODUCE` |

Two merges needed work beyond an entry union, and the reproduction check found each:

* **Merge 1 — `plugins/Vst3Instrument/logo.png` was in `fork-sources.txt` where its own command
  cannot produce it.** The file's regeneration command filters `\.(cpp|c|h|hpp|cc|cxx)$`, and the
  lane knew it (its own comment says "the regeneration command above filters for source
  extensions"). It cannot simply be dropped: Gate 6 examines every path changed since its base, and
  for a `plugins/` path that is neither a test, a build file, CI config, documentation nor a
  registered fork-NEW source, the only remaining home is the divergence ledger — which would be a
  **false statement** there, because `plugins/Vst3Instrument` does not exist upstream at all (the
  ledger's own SCOPE paragraph refuses fork-authored paths for exactly that reason, and 3C deleted
  seven `tools/` entries for making that false statement). Resolved the way the file already
  resolves this class: the asset is admitted **by name** in the second pathspec line
  (`-- tools/local-ci.sh tools/ncpu-shim.c plugins/Vst3Instrument/logo.png`), with its reason
  recorded in the header beside `tools/ncpu-shim.c`'s. Measured before choosing it: with the entry
  present, Gate 7 (file-length) `EXIT=0`, Gate 4 (complexity) `EXIT=0`, Gate 8 (duplication)
  `EXIT=0` — the fork-scoped ratchets widen by one file that all three measure without a violation.
* **Merge 2 — `tests/evidence/instrument-view-safety/qt-probe/probe-setwindowicon.cpp` was in no
  manifest.** A C++ source under `tests/` is in Gate 9's scope (`is_source()`), and the merge
  would have been Gate 9-red. `tests/all-sources.txt` is `git ls-files` over the C/C++ extension
  set, so its own command produces the entry: the file was **re-derived** (`regen.py --write
  all-sources.txt`, 1,199 → 1,209), never hand-added. (The lane's `.py`, `.sh`, `.png` and `.log`
  evidence under `tests/evidence/` is outside Gate 9's scope and correctly needs no home — the same
  classification 3B recorded for the brand-placeholders scripts.)

Headers were kept from integration in every case (the lanes' copies are older: `fork-sources.txt`
theirs 108 vs ours 167, `all-sources.txt` theirs 1,108 vs ours 1,199, the ledger theirs 32/127 vs
ours 437/438).

## The product-code hunks an automatic merge resolved, and how each was checked

This is the class the `EffectChain` double-lock and 3C's two-render-path trap came from, so every
merge's diff was read for product code, not just for conflicts.

### Merge 1 — `instrument-hosting-impl`

* **`plugins/Vst3Effect/Vst3Host.cpp` (+86 net) — the MIDI-in path and the audio-thread hand-off,
  the brief's named weak spot. Read line by line.** `load()` resolves the input event bus only for
  an instrument (`d.eventInputBusIndex = d.instrument ? resolveEventInputBus(...) : -1`);
  `prepare()` sets `d.processData.inputEvents = d.midiEnabled ? &d.inputEvents : nullptr`, so **an
  effect keeps `nullptr`** exactly as before and its render path cannot change; `process()` drains
  the queue into the event list **only when `inputEvents != nullptr`**. Allocation-freedom on that
  path, checked against the SDK and the new headers, not asserted:
  - `EventList::clear()` is `{ fillCount = 0; }` (SDK `eventlist.h:39`) — no allocation;
    `addEvent()` appends into the array its constructor allocated once, at `HostedPlugin`'s
    construction (GUI thread, load time).
  - `drainMidiIntoEventList()` (`Vst3MidiEvent.cpp:128-156`) writes through a **pre-sized**
    `std::vector<MidiEventIn>` (`assign(256)` in `prepare()`) using indexed assignment and insertion
    sort — no `push_back`, no allocation, no lock; it stops at `scratch.size()`, so the work per
    block is bounded and it never overruns the 256-slot event list.
  - `MidiQueue` (`Vst3MidiQueue.h`) is a **fixed 1,024-slot ring** with a per-slot sequence number
    (the correct shape for the multi-producer/single-consumer mix the header documents): one
    compare-exchange, one plain store and one release store per operation, and a **full queue
    refuses the event** (`return false`) instead of growing or spinning unboundedly; the caller
    counts it (`droppedMidi`). The `HostedPlugin::Impl` member is by value, so nothing allocates on
    push.
  - `release()` and `prepare()` both reset the queue and `midiEnabled`, so a note-on queued before a
    sample-rate change cannot sound on the first block after it.
  - `pushMidiEvent()` refuses when `midiEnabled` is false — nothing could ever drain the queue.
* **`plugins/Vst3Effect/Vst3Host.h`** — the four new declarations, the `receiveMidi()`/`process()`
  threading contract updated in the class comment, `Vst3MidiQueue.h` included.
* **`cmake/modules/Vst3Sdk.cmake`** — a `IF(NOT TARGET lmms_vst3_sdk)` guard around the static host
  library, so the second includer (the instrument module) is a no-op instead of redefining the
  target. Balanced with its `ENDIF()` at EOF; configure `EXIT=0` proves it.
* **`cmake/modules/PluginList.cmake`** — one line, `Vst3Instrument` added to `LMMS_PLUGIN_LIST`.
* **`plugins/Vst3Effect/CMakeLists.txt`** — `Vst3MidiEvent.cpp` added to the effect host's sources.
* **`tests/CMakeLists.txt`** — the auto-merge inserted the lane's block at exactly the branch's own
  position: **byte-for-byte the same nesting as the branch tip** (compared hunk context line by line
  after the merge; `if(`/`endif()` balance 26/26; the new registrations sit inside
  `if(WANT_VST3_TEST_INSTRUMENT)`, see finding 1).
* **`tests/complexity-baseline.tsv` / `tests/file-length-baseline.tsv`** — taken through the gates'
  own default ratchet mode (which is what re-derives them), not hand-edited; the only difference
  between the merge's result and the refreshed file was the tie order of two equal-CCN entries
  (`14` and `11`). No `--reanchor` and no `--reanchor-file` was used anywhere in this train.
* New files were read too (not merge resolutions, but the train's product surface):
  `plugins/Vst3Instrument/Vst3Instrument.cpp` (`handleMidiEvent()` is lock-free and alloc-free, and
  clamps channel/key/velocity before pushing; `processImpl()` re-prepares on the GUI thread via an
  atomic flag rather than reconfiguring on the audio thread), `Vst3InstrumentView.cpp`,
  `Vst3Instrument.h`, the module's `CMakeLists.txt` (`BUILD_PLUGIN(vst3instrument ...)` gated by
  `WANT_VST3`, the same option the effect host uses — the contract row's claim).

### Merge 2 — `instrument-view-safety`

* **`src/gui/instrument/InstrumentView.cpp` — the guard is untouched by the merge.**
  `git show post-alpha/instrument-view-safety:src/gui/instrument/InstrumentView.cpp | diff - <merged>`
  is **empty**: the merged file is byte-identical to the branch tip, and the merge's whole
  contribution to it is the one hunk the brief forbade moving (`if( auto * window =
  instrumentTrackWindow() )` around the `setWindowIcon` call, plus the comment explaining the null).
  The branch's own file is otherwise only that hunk against integration's version.
* **`tests/src/plugins/Vst3InstrumentIntegrationTest.cpp` — byte-identical to the branch tip**, and
  the merge's diffstat (63/11) equals the branch's own diffstat for the file. The regression test
  `testTheViewsEntryPointIsSafeOutsideAnInstrumentTrackWindow` is present, builds `BareInstrumentView`
  over a plain `QWidget` parent, asserts `view.instrumentTrackWindow() == nullptr` (so the guarded
  branch is the branch under test) and prints the three pointers.

### Merge 3 — `release-prep`

* **`CMakeLists.txt`** — `SET(VERSION_MINOR "2")` and `PROJECT_EMAIL` → the project's own issue URL
  (theirs) with integration's `ZENE_TELEMETRY` option and its `IF(ZENE_TELEMETRY)` block intact
  (`grep`ed: lines 62, 66, 140-142, 376 all present).
* **`README.md`** — the Download section at `0.2.0-alpha` and the "effects only" corrections
  (theirs) with the rename paragraph (`~/.zenestudio.xml`, `docs/RENAME-COMPLETE.md`) preserved.
* **`tests/run-all-gates.sh`** — theirs' `declare -a RESULTS=()` / `declare -a SKIPPED=()` **and**
  ours' Gate 10 block and `build/gate1-build.log`; ten gates still reported.
* **`tests/mutation-gate.sh`** — `declare -a ROWS=()`.
* **`.github/workflows/build.yml`** — byte-identical to the branch; **six** jobs
  (`linux-x86_64`, `linux-arm64`, `macos`, `mingw`, `msvc`, `msys2`), each with the
  release-version guard step, and the file parses as YAML (`yaml.safe_load`, 6 jobs).
* **`tests/advertised-features.tsv`** — the capability contract, verified below.
* **`tests/upstream-modifications.txt`** — the conflict; entry union, below.
* New and read: `tests/release-version-gate.sh`, `tests/test-release-version-gate.sh`, the
  `docs/*` release documents, `tests/evidence/release-prep-0.2.0/**`.

## The capability contract: exactly six rows, the expected ids

`bash`-level verification of `tests/advertised-features.tsv` at the exit tip — every row is five
TAB-separated columns, the ids are the six the brief names, in order, and **no row is duplicated**:

```
$ grep -vE '^[[:space:]]*(#|$)' tests/advertised-features.tsv | wc -l
6
$ grep -vE '^[[:space:]]*(#|$)' tests/advertised-features.tsv | cut -f1
vst3-hosting
vst3-instrument-hosting
clap-hosting
session-view
wasm-sandbox
stem-separation
$ grep -vE '^[[:space:]]*(#|$)' tests/advertised-features.tsv | cut -f1 | sort | uniq -d
(no output — no duplicated id)
$ grep -vE '^[[:space:]]*(#|$)' tests/advertised-features.tsv | awk -F'\t' '{print NF}'
5 5 5 5 5 5
```

The rows themselves (`cat -A` shows `^I` = TAB):

```
vst3-hosting	WANT_VST3	ON	vst3effect	VST3 hosting (effects) is in the release
vst3-instrument-hosting	WANT_VST3	ON	vst3instrument	VST3 instrument hosting (one instrument per track, MIDI in to audio out) is in the release
clap-hosting	WANT_CLAP	ON	clapeffect	CLAP hosting (effects) is in the release
session-view	WANT_SESSION_VIEW	OFF	-	The Session View (clip launcher and launch scheduler) is documented as absent from these builds
wasm-sandbox	WANT_WASM	OFF	-	The WASM DSP sandbox is documented as absent from these builds
stem-separation	WANT_STEM_SPLIT	OFF	-	Offline stem separation is documented as absent from these builds
```

**No line-union was performed on this file**: only the branch changed it, so the merge applied its
version wholesale, and the count above is the proof. Note the branch deliberately **removed** the
`#feature<TAB>option<TAB>…` legend line, so there is no non-comment line that is not a row.

## THE RENDER: the sha256 matches the expected value

```
expected from train 3B                 943e323864cb5c07bb8da264736978be9294513b93c1b6b81421cb6d8a890526
this train, run 1 (ea0426199 tree)     943e323864cb5c07bb8da264736978be9294513b93c1b6b81421cb6d8a890526
this train, run 2                      943e323864cb5c07bb8da264736978be9294513b93c1b6b81421cb6d8a890526
```

`bash tools/mmpz-git/render-recipe.sh data/projects/shorties/sv-DnB-Startup.mmpz -o …` →
`RENDER_1_EXIT=0`, `RENDER_2_EXIT=0`; 16-bit, 2 channels, 44,100 Hz, **544,256 frames**. Run-to-run
identical, so the same-build floor is 0. The chunk comparison was taken anyway, because it is the
cheapest way to say "the audio did not move through three merges":

* **`data` chunk sha256 = `b37cefc5a97e2d46…`**, 2,177,024 bytes — the value 3A, 3B, 3C and a
  pre-train render all recorded.
* The `LIST`/`INFO` chunk still holds `ISFT = "Zene Studio (libsndfile-1.2.2)"` (3B's documented
  `AudioFileWave.cpp` rename); file total 2,177,120 bytes, the same 8-byte tag delta as 3C.
* The repo's own comparator, `tools/render-determinism-compare.py`, against **3A's committed
  artifact**: `0 differing frames (0.000000 %)`, `max |delta| 0 LSB (−inf dBFS)`, `0 of 2,126
  periods dirty`, `header match=True` — and the same figures against **3C's** artifact.

**Verdict: `ALL-REPRODUCE` on the render too. `943e3238…` is this line's value, confirmed a third
time, and the audio is bit-identical to 3A's and 3C's.**

## The release identity, and the untagged-tree behaviour

`git log --oneline -- CMakeLists.txt | head` shows the version change (the brief's check (c)):

```
ea0426199 Merge post-alpha/release-prep into post-alpha/integration (merge train 3D, #3 of 3)
1066a056f Merge post-alpha/telemetry into post-alpha/integration (merge train 3C, #5 of 6)
4c7ebe473 build(release): 0.2.0-alpha, and the product's own contact address
```

The tree declares `0.2.0-alpha`; the binary reports the `git describe` string, because **the tag is
the version and this tree is untagged**:

```
$ git describe --tags
v0.1.0-alpha-235-gea0426199
$ git tag --points-at HEAD
(nothing)
$ ./build/zene --version | head -1
Zene Studio 0.1.0-alpha.235+ea04261
```

That is the expected untagged-tree behaviour the brief predicts, **not** a defect. Nothing was
tagged, nothing was "fixed", and no `-DFORCE_VERSION` was passed into anything committed.
`bash tests/release-version-gate.sh` → **`RELEASE_VERSION_GATE_EXIT=0`** — the tree, the release
notes' H1 and the README's Download link all agree on `0.2.0-alpha`, with the tag row
`[skip] tag: no v0.2.0-alpha tag exists yet (the owner creates it at freeze…)`. Its red/green harness
`bash tests/test-release-version-gate.sh` → **`TEST_RELEASE_VERSION_GATE_EXIT=0`**, 8 controls
passed, and **all six injected defects observed to fail with exit 1** (R1 a release ref that is not
the declared version, R2 the tree moving without its documents, R3 the README's Download section
lagging, R4 notes missing, R5 HEAD tagged with another version, R6 a tag HEAD does not descend
from) — a guard that has been seen red.

## The honesty gate: both invocations, exit 0

```
$ bash tests/release-honesty-gate.sh --header build/lmmsversion.h
HONESTY_HEADER_EXIT=0
  [PASS] vst3-hosting     ON matches ON
  [PASS] vst3-instrument-hosting ON matches ON
  [PASS] clap-hosting     ON matches ON
  [PASS] session-view     OFF matches OFF
  [PASS] wasm-sandbox     OFF,OFF matches OFF
  [PASS] stem-separation  OFF matches OFF
RESULT: PASS — all 6 documented feature(s) are what this build contains

$ bash tests/release-honesty-gate.sh --header build/lmmsversion.h --artifacts build
HONESTY_ARTIFACTS_EXIT=0
  [PASS] vst3-hosting     ON matches ON; module build/plugins/libvst3effect.so
  [PASS] vst3-instrument-hosting ON matches ON; module build/plugins/libvst3instrument.so
  [PASS] clap-hosting     ON matches ON; module build/plugins/libclapeffect.so
  [PASS] session-view     OFF matches OFF
  [PASS] wasm-sandbox     OFF,OFF matches OFF
  [PASS] stem-separation  OFF matches OFF
RESULT: PASS — all 6 documented feature(s) are what this build contains
```

**My reading of the local-only failure the brief anticipated: there is none on this tree, and the
brief's premise is wrong.** The brief says the three `ON` rows "may fail … that is a property of
this box lacking the VST3 SDK". This box **has** the pinned SDK
(`build/vst3sdk`, `VST3 SDK v3.8.1_build_84 (3cdf9ca5d, MIT)`), `WANT_VST3=ON` is configured, and
after merge 1 the module exists. The failure the *lane* recorded
(`tests/evidence/release-prep-0.2.0/05-honesty-gate-with-artifacts.txt`, `[FAIL]
vst3-instrument-hosting … the module was never built`) is a property of **the lane's own tree**,
which was branched before `plugins/Vst3Instrument` existed; the merge is what supplies the module,
and the row passes. **Nothing was "fixed" by editing the manifest** — the six rows are the branch's,
byte for byte. The only stale-header risk left is that the gate must be pointed at a header produced
by a configure of *this* tree, which `tools/local-ci.sh` does on every run.

## Merge 2's trap, closed out of band: the test is present, and it really runs

`tests/src/plugins/Vst3InstrumentIntegrationTest.cpp` — the file that carries
`testTheViewsEntryPointIsSafeOutsideAnInstrumentTrackWindow` — is registered **inside
`if(WANT_VST3_TEST_INSTRUMENT)`**, which defaults **OFF** (it needs the MIT fixture bundle as its
test subject). The release/CI configuration therefore registers those suites but never builds them,
which is why `ctest` is 70/70 at all three merges. An "absent entirely" test is worse than a
failing one, so the trap was closed explicitly, out of band, in the same build directory:

```
$ cmake -S . -B build <the local-ci flag set> -DWANT_VST3_TEST_INSTRUMENT=ON   -> CONFIGURE_ON_EXIT=0
$ cmake --build build --target Vst3InstrumentIntegrationTest -j4               -> BUILD_TARGET_EXIT=0

# the named test on the merged tree, guard present
$ QT_QPA_PLATFORM=offscreen ./build/tests/Vst3InstrumentIntegrationTest \
      testTheViewsEntryPointIsSafeOutsideAnInstrumentTrackWindow
PASS   : …::testTheViewsEntryPointIsSafeOutsideAnInstrumentTrackWindow()
QINFO  : … instrument view built without an instrument window: model=0x… parent=0x… window=0x0
Totals: 3 passed, 0 failed     REGRESSION_NAMED_EXIT=0
$ … (whole suite)              REGRESSION_SUITE_EXIT=0   Totals: 9 passed, 0 failed

# NEGATIVE CONTROL: the guard removed, nothing else changed
$ …                            REGRESSION_NO_GUARD_EXIT=139
Received signal 11 (SIGSEGV), code 1, for address 0x0000000000000008
         testTheViewsEntryPointIsSafeOutsideAnInstrumentTrackWindow function time: 6ms

# restored (git checkout; file verified identical to HEAD and to the saved copy)
$ …                            REGRESSION_RESTORED_EXIT=0   Totals: 3 passed, 0 failed
```

So the test **passes with the guard and dies with SIGSEGV at address 0x8 without it** — the same
address the lane's report names — and the window genuinely does not exist in the test
(`window=0x0`), i.e. it exercises the guarded branch rather than something adjacent. The build
directory was then reconfigured `-DWANT_VST3_TEST_INSTRUMENT=OFF` and the full pass re-run
(`final/`), so the tree's recorded state is the release configuration.

## The auto-merged files I refused to accept as-is

* **The ledger's duplicated comment block.** `resolve_pair.py`'s ledger mode appends every comment
  line of `theirs` as a "banner" when theirs' file carries no `# ---` banner, so both times it
  produced ours + **the branch's whole (older) header text** + the branch's entry. A duplicated
  header is not an entry, but it is a false statement sitting in the file (it predates the SCOPE
  paragraph the ledger now relies on). Both results were rebuilt by hand as **ours verbatim + a
  labelled banner + the branch's entries, verbatim**, with the union asserted in code:
  merge 2 `438 = 437 ∪ {src/gui/instrument/InstrumentView.cpp}`,
  merge 3 `439 = 438 ∪ {CMakeLists.txt}`, `0 lost, 0 dup, 0 blank reason, 0 markers` each.
  For every path both sides carry with different text, the branch's text is still the merge base's,
  so ours is kept — asserted, not assumed (`0 paths where theirs differs from both ours and the
  base` at merge 3).
* **A line union of `tests/all-sources.txt`** would have been wrong in principle: the file is
  `git ls-files` over an extension set, and its own command is the authority. It was re-derived at
  every merge and reproduces at each.

## Findings

1. **The two new VST3 instrument suites do not run in the release configuration.** ctest is 70/70 at
   every merge of this train; `Vst3InstrumentTest` and `Vst3InstrumentIntegrationTest` are inside
   `if(WANT_VST3_TEST_INSTRUMENT)` (default OFF, because it builds the MIT fixture they test
   against). What lands is **source and tests, not a CI-run capability** — and the *only* host-level
   witness for the merged MIDI path (plus merge 2's crash regression) never executes in CI. This is
   3C's `session-scheduler` finding class again. Closed out of band (previous section); see
   "Decisions needed" #1.
2. **The brief's premise that "this box lack[s] the VST3 SDK" is wrong for this tree.** The pinned
   SDK is present, `WANT_VST3=ON` is configured, `libvst3effect.so` and `libvst3instrument.so` both
   exist, and the honesty gate passes with `--artifacts`. The failure the brief predicted is a
   property of the release-prep lane's own tree, not of the box.
3. **`tests/advertised-features.tsv`'s header contains a claim that is false on a re-configured build
   directory.** It says `WANT_VST3_TEST_INSTRUMENT` "is declared inside tests/CMakeLists.txt under a
   condition, so it never reaches the generated header". `build/lmmsversion.h` in this tree reports
   `WANT_VST3_TEST_INSTRUMENT='OFF'`. Mechanism: the dump is generated in `src/CMakeLists.txt`
   (`get_cmake_property(VARIABLES)`, before `ADD_SUBDIRECTORY(tests)`), so on a **fresh** configure
   the option is absent; but `option()` is a cache variable, and on every later configure of the same
   build directory it is already visible at that point and lands in the dump. Consequence: the same
   source tree can export two different `LMMS_BUILD_OPTIONS` strings, depending on build-directory
   history. Harmless to the gate (it iterates the manifest's rows, so an unreported option only
   matters if a row names it), but the stated reason is build-history-dependent.
4. **I collided with my own background gate run, and its evidence is not used.** My entry-baseline
   `tests/run-all-gates.sh` was still inside the mutation gate when merge 1 was committed; it left
   the mutant in `src/core/RoutingGraph.cpp` (`std::min` → `std::max` at the line the log names). I
   killed it, restored the file with `git checkout` (verified identical to HEAD, then `touch`ed so
   the next build recompiles it), and re-ran the gates serially thereafter — which is why
   `run-merge-checks.sh` exists. **The entry baseline therefore rests on 3C's recorded tip evidence
   plus my own entry runs of local-ci (ctest 70/70), Gate 9 (0) and Gate 6 (0)**, not on that run.
   The lesson is worth recording: the mutation gate mutates a tracked file and restores it, so a
   killed run leaves the tree dirty and a concurrent build reads a mutant.
5. **Commit message typo, not corrected.** `d7de12677`'s message opens a paragraph with
   "lue src/gui/instrument/InstrumentView.cpp…" where "The" was intended. I did not amend or rewrite
   the commit to fix it (hard rule: never rewrite history). The verification it describes is in
   `tests/integration-logs-3d/merge2/`.
6. **`resolve_pair.py`'s ledger banner logic is buggy** (finding under "refused", above): it copies a
   branch's whole comment block into the result whenever the branch has no `# ---` banner. Both ledger
   merges in this train needed a hand rebuild because of it. A future train should fix the resolver
   rather than repeat the repair.
7. **Ledger growth is exactly the two declarations the two branches owed, and nothing was
   resurrected.** `diff <(git show 91d490557:tests/upstream-modifications.txt) tests/upstream-modifications.txt`
   is `+` two banners and `+` two entries and nothing else; the six `tools/mmpz-git/*` entries 3C
   refused to resurrect remain absent (the single `tools/mmpz-git/run-demo.sh` entry is pre-existing
   at the entry tip and was untouched), and no merge added a `tools/` path to the ledger.
8. **No cherry-picked duplicates.** Every non-merge commit of all three branches was patch-id'd
   (`git show <c> | git patch-id --stable`) and compared against the 7,718 distinct patch-ids of the
   entire pre-train history reachable from `91d490557`: hosting-impl 2 commits / 0 duplicates,
   view-safety 7 / 0, release-prep 6 / 0. (view-safety's 7 include hosting-impl's 2 as *ancestors*.)
   Nothing was deduped because nothing was duplicated.

## Test expectations changed during this train

**None. No test assertion, tolerance, expectation, exemption or gate threshold was changed by this
train.** That is a measurement, not an omission: the file-level changes I made to the test tree were
*registrations* only (`tests/fork-sources.txt`'s header and its second pathspec line,
`tests/all-sources.txt`, `tests/upstream-modifications.txt`), plus the two baseline files that the
gates' own ratchet mode re-derives. `tests/file-length-exempt.txt` and
`tests/file-length-baseline-all.tsv` were not touched. No `--reanchor` and no `--reanchor-file` was
used; no code was trimmed to satisfy a metric.

## Refusals

* **No tag was created**, nothing was tagged, and `-DFORCE_VERSION` was not passed into anything
  committed. `git tag | wc -l` is 60 before and after; `git tag --points-at HEAD` is empty.
* **No `--reanchor` of any kind, and no hand-edited baseline.** The two baselines that moved were
  re-derived by the gates' own default ratchet mode.
* **No gate script was edited or weakened.** Where a branch's older gate-runner copy conflicted, the
  fix was applied to ours (`declare -a …=()`), never ours' coverage removed.
* **No entry was resurrected, and no line union was used on any registry or manifest.**
* **The instrument-view guard was not moved** — the merged file is byte-identical to the branch tip —
  and no test expectation was adjusted to make anything green.
* **Nothing off-list was merged**, nothing pushed, no branch rebased/amended/reset, no remote
  contacted (a tag was never created, which is the one thing this train could have done "for" the
  release).
* **I did not audit the lanes' designs.** I read every product-code hunk the merges resolved and the
  new files' product surface, and asserted the invariants above; the lanes' own design choices are
  theirs, covered by their tests and by the render comparison (three merges, 0 LSB, identical
  `data` chunk).
* **The whole-tree scope was not made green**, Gate 2 (coverage) was not run, and no CI was run.

## What is NOT proven

* **Gate 2 (coverage) was not run** at any point. Every `run-all-gates.sh` invocation was the
  default; coverage needs `--with-coverage` plus an instrumented build. The `3` is
  `PASS-WITH-SKIPS` and is not a pass.
* **CI was not run** and no CI configuration was changed beyond the branch's own six job steps. Every
  exit code here is local; the README's claim that only platforms whose job is green get a package
  is therefore untested by this train.
* **The new instrument suites were exercised in one build-directory configuration on this box**, with
  a hand-built fixture bundle — not in CI, and not on another platform. I did not run
  `Vst3InstrumentTest`'s whole assertion set against a third-party instrument (there is none).
* **The `ISFT`/version interaction was not probed further**: the render's bytes do not carry the
  version string, so this train cannot tell whether the *released* (tagged) build would produce the
  same sha256. The tagged-tree render has never been measured.
* **3C's `ZENE_TELEMETRY` default-ON question** is untouched by this train.

## Concurrent activity — noted, not touched

Another session is live in this clone. The entry check passed before merge 1 (`git status
--porcelain` empty, no file written in the previous 5 minutes) and `git status` was re-checked after
every gate run (`TREE-CLEAN` at all three merges and at the final tip). No concurrent branch was
read-write touched, none was merged, and no concurrent merge was observed arriving at integration
during this train.

## Decisions needed from the owner

1. **Should the VST3 instrument suites run in CI?** They are the only host-level witness for the
   merged MIDI path and for merge 2's crash regression, and today they execute nowhere automated
   (`WANT_VST3_TEST_INSTRUMENT` default OFF). Options: add a CI job (or one gate) that configures
   `-DWANT_VST3_TEST_INSTRUMENT=ON` and runs those three targets, or accept developer-run-only and
   say so in `docs/QA-GATES.md` / the release notes. This is the one finding of this train that
   affects what the release can honestly claim to have tested.
2. **The `--version` dump is build-directory-history dependent** (finding 3). If a row for the VST3
   fixture is ever wanted in `advertised-features.tsv`, it would pass on this box's re-configured tree
   and fail on a fresh CI checkout. Fix the dump (enumerate the option list from a fixed set, not
   `get_cmake_property` at a directory position) or correct the file's comment; do not add a row
   before that.
3. **The render constant can be closed out.** `943e3238…` now matches on two runs of a third train,
   with 0 differing frames against both 3A's and 3C's committed artifacts and an unchanged
   `data`-chunk hash. Updating the documented constant (3C's decision #1) is now a three-train
   result. I changed no documented expected value.
4. **`resolve_pair.py`'s ledger banner bug** (finding 6) — worth fixing in the tooling before the
   next train, since it silently duplicates a branch's header text into the ledger on every
   merge where the branch carries no `# ---` banner.
5. **Unchanged from 3A/3B/3C:** where fork evidence lives (I used `tests/integration-logs-3d/`, as
   they used `tests/`), Gate 6's `tools/` category vs `all-sources.txt`, and the stale whole-tree
   baselines.

## Evidence index (`tests/integration-logs-3d/`)

```
run-merge-checks.sh        the per-merge six-step procedure (build+ctest, Gate 9, Gate 6, the ten
                           gates, the manifest re-derivations, a final tree check)
baseline/                  entry-tip local-ci (ctest 70/70), Gate 9 (0), Gate 6 (0);
                           run-all-gates.log is the run killed by finding 4 - SUPERSEDED, not evidence
merge1/ … merge3/          merge.log, checks.log, local-ci.log (+exit), gate9.log, gate6.log,
                           run-all-gates.log, regen-index.log, regen-head.log, precommit-index.log,
                           tree-after-gates.txt; merge1 also diff-cmake-and-header.txt,
                           diff-Vst3Host-cpp.txt, diff-tests-CMakeLists.txt; merge2 also
                           resolve-ledger.log, regen-write-allsources.log, diff-integration-test.txt;
                           merge3 also resolve-ledger.log (union asserted in full)
sides/merge1 … merge3      :1: :2: :3: copies of every conflicted file, from the index
final/                     checks.log + local-ci/gate9/gate6/run-all-gates/regen-head (the finished
                           tree, release configuration), honesty-header.log, honesty-artifacts.log,
                           release-version-gate.log, test-release-version-gate.log,
                           version-untagged.txt, render-1|2.log, render-final-1|2.wav,
                           render-sha256.txt, chunk-parse.log, compare-vs-3A.log,
                           fixture-on-configure.log, fixture-on-build.log, fixture-off-configure.log,
                           regression-named.log, regression-suite.log,
                           negative-control/ (InstrumentView.guarded.cpp.txt,
                           InstrumentView.unguarded.cpp.txt, build-without-guard.log,
                           regression-named-no-guard.log, rebuild-with-guard.log,
                           regression-named-restored.log)
tools/                     3C's resolve_pair.py / regen.py / precommit_check.py / verify_union.py /
                           merge_ledger.py, with regen.py's fork command amended for this train
```

The git-history commits of the train are `221686487`, `d7de12677`, `ea0426199` — **three merges, no
fix-ups** — plus the evidence commit `3f9c7d214` and the report commits. `git log --merges
91d490557..HEAD` returns exactly the three merges and nothing else, at any later tip.
