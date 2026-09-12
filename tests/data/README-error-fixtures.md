# Error-carrying fixtures (task #625 — headless load path)

`project.open` must never hang, and when a project loads with errors the typed
result must carry the per-item error list. Two projects are used to prove that;
both carry errors for a documented, deterministic reason.

## `error-carrying-project.mmp`

The control-socket fixture (`agent-control-fixture.mmp`) with two user wave
files in its TripleOscillator set to paths that do not exist:

| attribute | value | why it errors |
|---|---|---|
| `userwavefile1` | `samples/shapes/nosuch_wave_625.ogg` | bare old-style relative path; `PathUtil::oldRelativeUpgrade` finds it under no base (user sample, factory sample, VST), so `toAbsolute` resolves it to `/samples/...`, which does not exist |
| `userwavefile2` | `nosuchdir/missing_sample_625.wav` | same resolution, and the directory does not exist either |

`TripleOscillator::loadSettings` (`plugins/TripleOscillator/TripleOscillator.cpp`)
records `"Sample not found: <path>"` through `Song::collectError` for each
non-empty `userwavefile` that does not exist, so the project produces **two
distinct items** — a summary string cannot satisfy the test by accident.

## `../data/projects/tutorials/editing_note_volumes.mmp` (shipped, unmodified)

The upstream tutorial project references `samples/shapes/smooth_inv_saw.ogg`
from a TripleOscillator with the same bare relative form. The file it names does
exist as `data/samples/shapes/smooth_inv_saw.ogg`, but the old-relative upgrade
does not find it under any base, so the resolved path is
`/samples/shapes/smooth_inv_saw.ogg` and `TripleOscillator::loadSettings` records
`"Sample not found: samples/shapes/smooth_inv_saw.ogg"`.

This is the "real project" case: a project the product ships, opening exactly as
a user or an agent would, on a tree where it genuinely carries a load error.
The same project is what the pre-fix reproduction hangs on — that hang is itself
proof that `Song::hasErrors()` is true for it (the modal is only raised when the
load collected errors).

## Regenerating the checks by hand

```
$ build/lmms render tests/data/error-carrying-project.mmp -o /tmp/out.wav
```

The CLI render path is core-only (`getGUI() == nullptr`), so it prints the error
summary to stderr instead of a dialog — a way to read the list without a socket.
