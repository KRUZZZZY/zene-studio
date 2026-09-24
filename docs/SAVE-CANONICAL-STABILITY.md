# The save-canonical contract — what two consecutive saves may differ by

**Status:** the contract as it stands after `DEFECT-D4b` (lane `bugs/hunt-2026-09-24`, worktree
`zene-bugs`, 2026-09-24). Written in the shape `docs/UNDO-BOUNDS.md` uses: the decision, what was
measured, and where each half lives, so the next reader does not have to infer the rule from the
code. The stability checks this contract governs are `tests/src/core/ProvenanceSectionTest.cpp`,
`tests/src/core/DocumentIndexTest.cpp` and the canonical form in
`tests/control-feedback-commands.py` (`canonical_project_sha`, whose rotations are recorded in
`docs/709-logs/709-baseline-rotate.md`).

---

## 0. The invariant

> **Two consecutive `project.save` calls that write the SAME session state to the SAME path produce
> the same bytes, modulo the append-only `<z:provenance>` journal block and the single `<z:index>`
> row that digests it.**

"Modulo the journal block" means the block **and its own namespace declaration**: the block is
documented growth (SPEC-ARCH-4 1.9 — a save's own change is appended after its write, so the second
save carries the first save's entry), and nothing that exists only to serve it may leak out of it
into the rest of the document. Everything else — the root element's attribute set first — is content
and must be byte-identical.

Additive rules are unaffected and still hold: a session that recorded no change writes **no** journal
block and **no** root `xmlns:z`; a project this build understands completely still re-saves with no
`<z:index>` and no namespace binding (`tests/src/core/DocumentIndexTest.cpp` pins both halves, and
`verification/arch4-s2-additive-gate.py` falsifies the second from outside the tree). This contract
carves nothing out of those rules; it forbids two saves of one state from *disagreeing with each
other*, which is what the D4b sweep found.

---

## 1. What the defect was (measured, 2026-09-24)

A statistical sweep (`/tmp/zene-cert-w1r-rw/`, seeds 5403 and 5412) saved twice through
`project.save` at 5 checkpoints per run and compared the two files with the journal block stripped.
50 of 60 checkpoints passed. The failing checkpoints were **checkpoint 20 of both runs — the first
checkpoint of the session**, and the failing pairs were not preserved by the runner, so a
capture-pair probe was written to keep both byte images (`docs/d4b-logs/D4b-CAPTURE-PAIR.md`).

Minimal reproduction, no commands at all — a fresh instance, `project.save` twice:

```
-<zene-project version="31" creatorplatform="linux" type="song" next-id="9" … >
+<zene-project version="31" xmlns:z="urn:zene:core:1" creatorplatform="linux" type="song" next-id="9" … >
       <keymap base_key="69" middle_key="60" last_key="127" description="empty" base_freq="440" first_key="0"/>
     </keymaps>
+    
   </song>
```

Two mechanisms, both journal-derived, neither of them content:

| # | mechanism | the delta | disposition |
|---|---|---|---|
| i | the journal's own `xmlns:z` binding was set on the **document root**, and only when the journal was non-empty at write time | a root **attribute** appearing between save 1 and save 2 | **removed from the save** — the declaration moved onto the journal element (§2) |
| ii | `<z:index>`'s row for the journal digests a section this build appends to on every save | `digest="sha256:434098bf…"` → `sha256:9fd20fa7…` with all ten sibling rows byte-identical | **declared** — the row is load-bearing, so it cannot be dropped (§3) |

Mechanism (i) is *state*-derived rather than random: the first save of a session whose journal is
empty writes no block at all, so there is nothing to declare the prefix on; the second save is the
first file that carries one. That is why the failing checkpoint was always the first of a run, and
why a run whose first twenty commands recorded *something* (seed 5401: `transport.play`) never
showed it. The writer's element and attribute order was **not** found to be non-deterministic in this
window — the two captures are identical outside the two deltas above.

(The stray `+    ` hunk is the sweep's normaliser, not the file: stripping only the tags leaves the
line's indentation behind. The strip must take the block's own line with it.)

---

## 2. The fix: the journal declares its own prefix

`src/core/ProvenanceSection.cpp` (`provenance::writeTo`) now sets `xmlns:z` on the `<z:provenance>`
element it creates, not on `file.documentElement()`. The declaration is in scope for the prefixed
name exactly as the root's was — the reader parses with namespace processing **OFF** and matches
`z:provenance` as a literal name (`include/ProvenanceSection.h`), so nothing reads the root binding —
and it still uses the one spelling `DocumentIndex.h` provides, so the two writers cannot drift.

Consequences, all deliberate:

* the journal's declaration now travels with the block whose growth is documented, so the residue
  after a strip is **empty** rather than "one root attribute";
* a document whose only z-prefixed content is the journal is still a legal prefixed document;
* the **root** binding is now written only by the writers that need it there — `<z:index>`
  (`DocumentIndex.cpp`), `<z:lanes>` (`Song.cpp`) and `<z:scenes>` (`SessionModel.cpp`), each of
  which changes only with a state change, so none of them flaps between two saves;
* a document carrying both an index and a journal declares the prefix on the root (index) and on the
  journal element — redundant XML, deterministic, and no longer order-sensitive in any way that
  matters.

The reader-side proof is the unchanged round trip: `ProvenanceSectionTest` re-reads the section from
a fresh load, and `DocumentSectionsTest` / `ProjectContainer*` walk documents whose root binding the
fixture itself supplies.

---

## 3. The declared exception: the journal's own index row

`documentIndex()` indexes every `<song>` child it did not write itself, and it writes a row for the
journal. That row's `digest` is a restatement of the journal's own state, so it moves on every save
by construction.

**The row stays.** The container's entry set is *derived from the index rows*
(`ProjectContainerEntries.cpp` `validatedIndexRows` → `sectionEntryName`), and
`tests/src/core/ProjectContainerTest.cpp` pins `z:provenance` as one of "the names the tree's own
sections use": dropping the row would drop the journal from a v2 container, which is data loss
rather than a stability fix. So this is declared, in the narrowest form the measurement allows —
**exactly one row, matched by name, and only its own presence/digest**:

```python
# tests/control-feedback-commands.py, canonical_project_sha()
#   ...and the ONE index row whose value restates the journal's own per-save growth.
[element.remove(c) for c in list(element)
 if c.tag.endswith("}section") and c.get("name") == "z:provenance"]
```

Every other row must still be byte-identical between the two saves. The declaration is carried by
the canonical form itself (`tests/control-feedback-commands.py` `canonical_project_sha`, whose
threshold the D4b capture is the evidence for) plus the strip table in §4 — the row is deliberately
*not* given a C++ pin of its own, because the two slots that would have carried it and the save-twice
window together pushed `tests/src/core/DocumentIndexTest.cpp` past the 500-line gate-7 ratchet, and a
ratchet is never moved for debt a change creates (see §6). The narrow form of the tolerance is what
keeps it honest: it is one row, matched by name.

---

## 4. What a stability check must strip, and why each entry is not content

| stripped | why it is not content |
|---|---|
| the `<z:provenance>` block, with its own line | the documented append-only journal (SPEC-ARCH-4 1.9); its growth per save is the feature |
| the `xmlns:z` declaration **inside** that block | part of the block since DEFECT-D4b; it exists only to name the block |
| the block's `<z:index>` row | a digest restating the block's own state (§3) |
| the `writer` attributes, `creatorversion`, GUI view sections and window geometry | instance/layout metadata — the exclusions `canonical_project_sha` already documents |

Anything else that differs is a defect, not a tolerance: the root's attribute set, the section order,
every other section's bytes and every other index row are all expected to match.

---

## 5. Reproduction

```bash
# the capture-pair probe: replays a window's step sequence, then saves twice and keeps both files
python3 docs/d4b-logs/capture_pair_probe.py <abs build>/zene <abs build>/plugins 5403 1-20 /tmp/d4b-run
```

The probe's verdicts, the quoted deltas and both mechanisms' raw evidence are in
`docs/d4b-logs/D4b-CAPTURE-PAIR.md`. The regression lives in
`ProvenanceSectionTest::twoConsecutiveSavesDifferOnlyByTheDocumentedJournal` — the whole contract in
one slot: the first save of an empty journal writes no block, the second carries the first save's
entry, and everything outside the block must be byte-identical (the root attribute failure is what
the slot reports before the fix).

## 6. What was NOT done, and why

* The journal's index row is **not** omitted from the index (§3): the container derives its entry
  set from those rows, so that would be data loss, not stability.
* The journal is **not** re-stamped, hashed differently or cached: its per-save growth is the
  documented feature. Only the *declaration* that leaked out of it was moved back in.
* Two extra slots (an index-row pin, and a root-attribute set comparison) were written and then
  dropped: together with the save-twice slot they pushed `tests/src/core/DocumentIndexTest.cpp`
  (479 lines) and `tests/src/core/ProvenanceSectionTest.cpp` (437 lines) past the 500-line gate-7
  ratchet. The payloads were moved instead — the index-row tolerance into the canonical form, the
  parameter-contract checkers into `tests/control_socket_flows.py`, the repo's own home for
  check_* payloads — so no ratchet was moved and no test was weakened.
