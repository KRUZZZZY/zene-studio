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

## What goes here

| Kind | Rule |
|---|---|
| a lane's report | `docs/reports/<LANE>-REPORT.md`, or the report's own title if it already has one |
| a lane's raw transcript | beside its report, `docs/reports/<LANE>-TRANSCRIPT.md` |

Not here: a document that a reader of the product needs, or a decision the project is
governed by. Decisions live in `docs/` (`CONVENTIONS.md`, `A16-REVERSIBILITY.md`,
`DOCS-NAMING.md`, `KNOWN-LIMITATIONS.md`, `STATUS.md`); product documentation lives in
`doc/` and the wiki. The three trees (`user` / `contributor` / `report`) are the change
plan's `DOC-1`, which is **not** taken — this directory is the part of it that REPO-4
needed, not the split.

## The root is not a filing cabinet

After this move the repository root holds exactly the three `.md` files that belong to a
repository root — `README.md`, `CONTRIBUTING.md`, `SECURITY.md` — plus the build files.
Measured:

```sh
ls -1 *.md
# CONTRIBUTING.md
# README.md
# SECURITY.md
```

Historical documents that quote a command run against the old path (`docs/WAVE-R-RENAME.md`
prints `./DOCS-NAMING.md:22: …`) keep the transcript as it was: editing a record of a run to
match a later layout is how a record stops being evidence. Each moved file carries a dated
"Location" note at its head instead, which is what a reader who arrives from one of those
citations needs.
