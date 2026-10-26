Verdict: FALSE-POSITIVE

# Cluster new-p21-5 — clip.repeat "dereferences a possibly null cloned clip"

Finding (Luna patrol, historical line 80): `ref.clip->clone()` result is passed to `movePosition`/`clipIdOf` without a
null check; "when clone fails or returns null, the handler dereferences null".

Branch `bugs/qwen-new-p21-5` at `cf3618e61` (no source change; this commit only adds this report).

## Why it is not real

`clone()` cannot return a null pointer on any path in this tree. Every concrete `Clip` subclass overrides it as a plain
`new` of a copy, and the failure mode of `new` is an exception, not a null return:

| Evidence | What it shows |
| --- | --- |
| `include/Clip.h:320` | `virtual Clip* clone() = 0;` — pure virtual, so every concrete clip must override it |
| `include/MidiClip.h:130-133` | `MidiClip* clone() override { return new MidiClip(*this); }` |
| `include/SampleClip.h:258-261` | `SampleClip* clone() override { return new SampleClip(*this); }` |
| `include/AutomationClip.h:362-365` | `AutomationClip* clone() override { return new AutomationClip(*this); }` |
| `include/PatternClip.h:55-58` | `PatternClip* clone() override { return new PatternClip(*this); }` |
| `src/core/ControlCommandsClipArrange.cpp:80` | `Clip* copy = ref.clip->clone();` — the cited line; the handler uses `copy` only after it |
| `src/core/ControlCommandsClip.cpp:463`, `src/core/ArrangementEdits.cpp:75` | sibling callers (clip.duplicate, clip split) also use the result unchecked — the codebase's contract is that `clone()` is non-null |
| `src/core/AudioResampler.cpp:285`, `src/core/DrumSynth.cpp:641` | the only `new (std::nothrow)` uses in `src/core`; no clone override uses `nothrow` |
| `src/core/ControlRegistry.cpp:315-319`, `:455-458` | `catch (const std::exception&)` and `catch (...)` around the handler: a `std::bad_alloc` from the copy becomes a typed control error, not a crash |

So the "clone fails → null → dereference" path does not exist: an allocation failure throws `std::bad_alloc`, which the
registry converts to an error reply before any dereference happens. A null check at line 80 would be dead code, and adding
one would make this handler inconsistent with its three sibling clone call sites. No product change is made.

## Regression test

Not written. A regression test that fails without the fix requires a bug to exist; the reachable behaviour is already
correct (error reply on allocation failure, no null dereference). Procedure step 2 (not real → skip to report) applies.

## Commands run and exit codes

Setup/inspection (read-only, in the worktree):

- `git status --short` → clean; `git log --oneline -1` → `cf3618e61`.
- `grep -n "clone()"` over `src/core` and `include`, `grep -rn "clone() override"`, `grep -rn "nothrow" src include`
  (3rdparty excluded), `grep -rln "fno-exceptions" --include=CMakeLists.txt .` — results cited in the table above.
  `fno-exceptions` appears only in `plugins/NeuralAmp/rtneural/modules/Eigen/CMakeLists.txt` (third-party, not the DAW build).

Gates (run from the worktree; logs in `LOG FOLDER/gate-<name>.log`):

| Command | EXIT |
| --- | --- |
| `python3 .../qwen/feature_check.py <worktree>` (log `feature-check-1.log`) | 1 — **could not run**: the script writes its logs to `<worktree>/../<name>-feature-check` (`/…/projects/zene-studio-program/zene-qwen-new-p21-5-feature-check`), which is outside the write-jail. `PermissionError` at `feature_check.py:71`. Not edited (shared tool). |
| `bash tests/fork-sources-gate.sh` | 0 |
| `bash tests/all-sources-reproduce.sh` | 0 |
| `bash tests/no-upstream-regression-gate.sh` | 0 |
| `tests/scripted/check-namespace` | 0 |
| `bash tests/no-tautology-gate.sh` | 0 |
| `python3 tests/doc-figures-gate.py` | 0 |
| `bash tests/complexity-gate.sh --check` | 1 (7 `REGRESSION` lines, none in a file this branch changes) |
| `bash tests/file-length-gate.sh --check` | 1 (19 `REGRESSION` lines, none in a file this branch changes) |

feature_check's rule for ratchet gates (complexity, file-length) is "fail only on items new in files the branch changed".
This branch changes no source file, so no ratchet item can be "in your files"; the 7 complexity and 19 file-length lines
are the train's standing items (e.g. `WasmSandbox::loadModuleBytes` CCN 15→16, `Vst3Host.cpp` 661→682 lines), unrelated to
this report. Under that rule both ratchet gates pass. The six exit-0 gates above are the other rows of the feature_check
table. The gate was run by hand because the script itself could not write its log directory.

## Not in scope, not verified

`repeatClip` (`ControlCommandsClipArrange.cpp:78-84`) has a different, real-looking question: if `clone()` throws
`bad_alloc` on iteration i>0, earlier copies and the ripple shift are already applied and the handler returns an error
(no `__transaction`). Whether that leaves an unrecoverable half-applied state has not been checked here; it is a separate
finding, not this one.

## Self-check

Filled in after `submit_check.py` (see below).
