# DEFECT-D4b — the capture-pair probe: what two consecutive saves actually differ by

Lane `bugs/hunt-2026-09-24` (worktree `zene-bugs`), 2026-09-24. This is the evidence record for
`docs/SAVE-CANONICAL-STABILITY.md`. The sweep that found the defect (`/tmp/zene-cert-w1r-rw/`, seeds
5403/5412) compared the two saves with a stripping normaliser and never kept the bytes, so the probe
below was written first and its output is quoted here. Raw `.log` files are not landed: Gate 11
refuses a `.log` anywhere in the tree, so the decisive lines are quoted in full instead, and the
probe keeps both captured byte images on disk with their sha256 recorded here.

## The probe

`docs/d4b-logs/capture_pair_probe.py` starts the built binary with the repo's own headless recipe
(`QT_QPA_PLATFORM=offscreen`, a `--config` whose `<audioengine audiodev="Dummy (no sound output)"/>`
is exact, fresh `HOME`/`XDG_*`/cwd, `LMMS_PLUGIN_DIR=<build>/plugins` — `tests/control_socket_harness.py`
lines 20–22), replays a sweep window's exact step sequence up to the failing checkpoint, then
`project.save` → keep bytes → `project.save` → keep bytes, and diffs the RAW bytes. Every run below
ended with `control.quit` and the driver's own `EXIT=0`:

```
python3 docs/d4b-logs/capture_pair_probe.py <build>/zene <build>/plugins 5403 1-20 /tmp/zene-d4b/run-5403
python3 docs/d4b-logs/capture_pair_probe.py <build>/zene <build>/plugins 5412 1-20 /tmp/zene-d4b/run-5412
python3 docs/d4b-logs/capture_pair_probe.py <build>/zene <build>/plugins 0    0-0  /tmp/zene-d4b/run-fresh
python3 docs/d4b-logs/capture_pair_probe.py <build>/zene <build>/plugins 0    0-0  /tmp/zene-d4b/run-index /tmp/zene-d4b/foreign.mmp
```

Verdict, all four runs: `save_a_ok=true`, `save_b_ok=true`, `raw_equal=false`, `normalized_equal=false`.
The comparisons are made **after** removing the whole `<z:provenance>…</z:provenance>` block with the
same rule the sweep used, so everything quoted below is residue the sweep's normaliser could not
remove.

## Verdict 1 — the failing window, reproduced exactly (seeds 5403 and 5412, checkpoint 20)

Both seeds, and a fresh instance with **no commands at all**, produce the identical delta. The
decisive line, raw, from seed 5403:

```diff
-<zene-project version="31" creatorplatform="linux" type="song" next-id="9" creatorversion="0.3.0-alpha.97+528994d" creator="Zene Studio" creatorplatformtype="ubuntu">
+<zene-project version="31" xmlns:z="urn:zene:core:1" creatorplatform="linux" type="song" next-id="9" creatorversion="0.3.0-alpha.97+528994d" creator="Zene Studio" creatorplatformtype="ubuntu">
       <keymap base_key="69" middle_key="60" last_key="127" description="empty" base_freq="440" first_key="0"/>
     </keymaps>
+    
   </song>
```

* the delta is a **root ATTRIBUTE** — `xmlns:z` — so a strip of the journal block cannot reach it;
* the trailing `+    ` line is the sweep normaliser's own artefact (stripping only the tags leaves
  the block's indentation behind), not a file difference;
* `save1.mmp` carries **no** `<z:provenance>` element at all (verified: the captures' own grep for
  `z:provenance` returns 0 in `save1.mmp` and finds the block at line 257 of `save2.mmp`).

Mechanism: `provenance::writeTo()` set `xmlns:z` on `file.documentElement()` — and only when the
journal was non-empty at write time. A save's own change is recorded **after** its write, so the
first save of a session whose journal is empty writes no block and no binding, while the second save
is the first file that carries both. This is state-derived, not random — which is why the failing
checkpoint was the FIRST checkpoint of a run in both seeds, and why seed 5401 (whose first twenty
steps included `transport.play`, i.e. a recorded change) never showed it.

Captures (kept on disk under `/tmp/zene-d4b/`; sha256 as recorded by this run):

| run | save1.mmp | save2.mmp |
|---|---|---|
| fresh, no commands | `f8b6b2ce8be6738ff185df2f8d7343a41479b1151705c870368c5d803532d8d6` | `72725ebc6448e2d9be16a7d948748d1c74ab1e37e787ccee13ba0ed8d0a563a3` |
| seed 5403, steps 1–20 | `f99591c682aa95fb3b3449ad8956591c247e83f579e822bc87ca904a9652cd4d` | `51ef5eb3be32485df130a4dfe4e85a7b1e4a8bf4efae440c642423aaf5ad47d8` |
| seed 5412, steps 1–20 | `e6061db43d402b47444ef5d0a11eb538f47966ade458625eef47435f1c04b314` | `a39bb7a0c9d09c10a461e908a3c16bb6f6d196ee76f9e0a665975e8b1fd9fd7d` |

## Verdict 2 — the index-carrying window (a preserved foreign section)

The probe was also pointed at a document carrying an unclaimed `<song>` child (`<z:mything>`), which
is the only condition under which `<z:index>` is written. There the root binding is present in BOTH
saves (the index writer sets it), and the residue is the index's own row for the journal:

```diff
-    <z:section name="z:provenance" v="31" digest="sha256:434098bfa307b4f9e85789026c970bfae22517480891fa09b2a82851fabf3fb0" entry="z:provenance.xml"/>
+    <z:section name="z:provenance" v="31" digest="sha256:9fd20fa77aece3b116280ff96d7fa1ecbd3f79278e91fcd4833d60aa58d3c227" entry="z:provenance.xml"/>
```

The other **ten** rows are byte-identical (`trackcontainer`, `mixer`, `ControllerRackView`,
`pianoroll`, `automationeditor`, `projectnotes`, `timeline`, `controllers`, `scales`, `keymaps`), and
the digest row's siblings' digests are unchanged — this is the "one row restates the journal's
per-save growth" that `docs/SAVE-CANONICAL-STABILITY.md` §3 declares rather than removes, because the
container derives its entry set from those rows.

Captures: `da5c70915cf1432ca556752a7bd3d307a493e1ac4c173f0bc320e74c6c7c42e4` (save1) and
`e598cf2e76d290d124950380c1f76e94f806105793e3f8a0e775b5a8cf90fda2` (save2).

## What was NOT found

* **No serialisation nondeterminism in this window.** The two captures are byte-identical outside the
  two deltas quoted above, element for element and attribute for attribute, across four runs: the
  writer's attribute order did not vary between the two saves of one process. (Attribute order is
  still a per-process QDom detail, which is why the stability checks sort attributes — see
  `canonical_project_sha` and the note in `DocumentIndex.h`.)
* **No second journal appears.** A grep of both captures finds exactly one `<z:provenance>` element,
  in `save2` only, so the "journal re-emitted twice" shape is not present here.

## The fix and its red/green

`src/core/ProvenanceSection.cpp` declares `xmlns:z` on the journal element instead of the document
root (`docs/SAVE-CANONICAL-STABILITY.md` §2). Load-bearing, measured with only the production files
reverted:

```
build/tests$ ctest -R ProvenanceSectionTest --output-on-failure     # fix reverted
FAIL!  : ProvenanceSectionTest::scriptedSessionChangesAreReadBackFromTheFileAfterAFreshLoad() Compared values are not the same
   Loc: [.../ProvenanceSectionTest.cpp(303)]
FAIL!  : ProvenanceSectionTest::twoConsecutiveSavesDifferOnlyByTheDocumentedJournal() Compared values are not the same
   Actual   (withoutJournal(second)): "<zene-project creatorplatform="linux" … version="31" … xmlns:z="urn:zene:core:1">…
   Expected (withoutJournal(first)) : "<zene-project creatorplatform="linux" … version="31" …>…
   Loc: [.../ProvenanceSectionTest.cpp(434)]
CTEST_EXIT=8

build/tests$ ctest -R "ProvenanceSectionTest|DocumentIndexTest|DataFileSaveIntegrityTest|ControlCommandsSnapshot|
                       ReversibilityContractTest|ReversibilityUndoTest|ControlReversibilityTranscript|
                       ControlFeedbackCommands|ControlStableIds|DocumentSectionsTest|ProjectContainerTest"
100% tests passed, 0 tests failed out of 12                             # fix restored
CTEST_EXIT=0
```
