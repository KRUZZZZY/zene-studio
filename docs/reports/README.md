# `docs/reports/` — lane reports and transcripts

This directory is where a lane's **report** and its **raw transcript** live. They used to
live in the repository root, and the root grew four `.md` files that had nothing to do with
the root: `CMDN-REPORT.md`, `CMDN-TRANSCRIPT.md` (the `post-alpha/cmd-notes` lane's report
and its verbatim socket transcript) and `DOCS-NAMING.md` (a project decision, since moved
to `docs/DOCS-NAMING.md`).

**REPO-4, 2026-09-13** moved them, on the change-plan register's row ("move lane reports
and transcripts out of the repository root") and on its own note that `DOCS-NAMING.md` is
**cited** elsewhere and so could not be deleted — the move had to carry the pointer with
it. The three pointers that are live today were updated in the same commit: `README.md`'s
link, `docs/STATUS.md`'s "where the documents are" list, and the F27 verdict in
`docs/INDEPENDENT-NOTES-READ.md` (date-stamped, not rewritten — the audit said "in the
repository root" and at the time it was right).

**REPO-4, 2026-09-15 — the second pass.** The seven `LANE-STATE*.md` reports were all written
*after* the 2026-09-13 move ran (`LANE-STATE.md` that same evening, at 22:07 against the
move's 02:07; the other six on 2026-09-14 and 2026-09-15), so the root grew lane reports
back and this file's own measurement of the root stopped being true. They are now in this
directory, same names, each with the dated "Location" note at its head:
`LANE-STATE.md` (four lanes concatenated: `030/folder-tracks`, `030/retro-capture`,
`030/vca-editgroups`, `030/pitch-stretch`), `LANE-STATE-CODE9-NAMED-PIPE.md`,
`LANE-STATE-HOSTCHUNKING.md`, `LANE-STATE-MASTERING-SURFACE.md`,
`LANE-STATE-METER-SURFACE.md`, `LANE-STATE-RECORD-INPUTS.md`,
`LANE-STATE-RENDER-PRESETS.md`. The two live citations moved with them —
`docs/METER-SURFACE.md` (twice) and the header comment of
`tests/src/core/ControlVerbInverseTest.cpp`. No manifest was touched, and that is correct
rather than an omission: the scope lists admit source extensions and scan
`src/ include/ plugins/ tests/ tools/ modules/` only, so a root `.md` was never a candidate
for them.

## What goes here

| Kind | Rule |
|---|---|
| a lane's report | `docs/reports/<LANE>-REPORT.md`, or the report's own title if it already has one |
| a lane's raw transcript | beside its report, `docs/reports/<LANE>-TRANSCRIPT.md` |

The second clause is not decoration: `CMDN-REPORT.md` and `CMDN-TRANSCRIPT.md` already had
titles naming what they were, and `LANE-STATE*.md` was the name seven lanes had already
written under, so the move kept every one of those names. A lane that starts fresh today
should read the rule as: name it for the lane and the artefact, and put it here — the root
is not a candidate. A lane report is a *report*, so it belongs here even when its subject is
documented in `docs/`; the design document is the thing that stays in `docs/` (for the
metering lane, that split is `docs/METER-SURFACE.md` and
`docs/reports/LANE-STATE-METER-SURFACE.md`).

Not here: a document that a reader of the product needs, or a decision the project is
governed by. Decisions live in `docs/` (`CONVENTIONS.md`, `A16-REVERSIBILITY.md`,
`DOCS-NAMING.md`, `KNOWN-LIMITATIONS.md`, `STATUS.md`); product documentation lives in
`doc/` and the wiki. The three trees (`user` / `contributor` / `report`) are the change
plan's `DOC-1`, which is **not** taken — this directory is the part of it that REPO-4
needed, not the split.

## The root is not a filing cabinet

After the 2026-09-13 move the repository root held exactly the three `.md` files that belong
to a repository root — `README.md`, `CONTRIBUTING.md`, `SECURITY.md` — plus the build files.
It did not stay that way: seven `LANE-STATE*.md` reports accumulated there between
2026-09-14 and 2026-09-15, which is why the 2026-09-15 pass exists. Re-measured after that
pass:

```sh
ls -1 *.md
# CONTRIBUTING.md
# README.md
# SECURITY.md
```

The same command before the second pass printed ten names — the three above plus the
seven moved reports and nothing else. (`git ls-files | grep -v /` is the other way to ask;
after this pass the root's non-dotfile entries are `Brewfile`, `CMakeLists.txt`,
`CONTRIBUTING.md`, `INSTALL.txt`, `LICENSE.txt`, `README.md`, `SECURITY.md`, `vcpkg.json`.
Six of the eight also exist in upstream `4e677cb6c`; `CONTRIBUTING.md` and `SECURITY.md` are
this fork's own, and all eight are files a repository root is supposed to have.)

Nothing enforces this. A gate would have to guess which root `.md` is a lane report, and
guessing is how a gate gets disabled; the rule is written down here, in `docs/CONVENTIONS.md`
and applied by hand.

Historical documents that quote a command run against the old path (`docs/WAVE-R-RENAME.md`
prints `./DOCS-NAMING.md:22: …`) keep the transcript as it was: editing a record of a run to
match a later layout is how a record stops being evidence. Each moved file carries a dated
"Location" note at its head instead, which is what a reader who arrives from one of those
citations needs.
