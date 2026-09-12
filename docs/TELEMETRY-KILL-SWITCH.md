# The packager kill switch: `-DZENE_TELEMETRY=OFF` must build, and the ON build must not move

Lane `fix/telemetry-kill-switch`, off `post-alpha/integration` (`f60cd6cc7`), 2026-09-12.
Supersedes the two claims it corrects in [`TELEMETRY-V1.md`](TELEMETRY-V1.md) (§2.3, §2.5) — that
document is the design's implementation report and keeps its §4.5 numbers as the historical record
of the state they measured. The switch itself is documented in the root `CMakeLists.txt:136-140`
and its runnable guard is [`../tests/telemetry-off-build.sh`](../tests/telemetry-off-build.sh).

## 1. The defect, measured

`CMakeLists.txt:136-140` documents the option as a packager kill switch: *"`-DZENE_TELEMETRY=OFF`
removes the client, its 'what we send' screen and its networking code from the build entirely, so a
distribution can ship a binary in which no send path exists."* It did not build.

Reproduced on this worktree, in the OFF configuration, by compiling the one translation unit the
error names (log: `tests/integration-logs-telemetry-off/off-prefix-defect-repro.log`):

```
$ cd build-off/src && cmake --build . --target core/ControlCommandsTelemetry.cpp.o
Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsTelemetry.cpp.o
src/core/ControlCommandsTelemetry.cpp:87:13: error: 'QJsonObject lmms::{anonymous}::consentState(
        const lmms::TelemetryConsent&)' defined but not used [-Werror=unused-function]
cc1plus: all warnings being treated as errors
gmake[1]: *** [.../ControlCommandsTelemetry.cpp.o] Error 1
OFF_PREFIX_SINGLE_TU_EXIT=2
```

The file is the `telemetry.*` command group (SPEC A15). It named the client's types in its own
signatures and its OFF half "answered a typed *not in this build*" from types that a
`-DZENE_TELEMETRY=OFF` build does not define. `-DUSE_WERROR=ON` made the first such use fatal, and
the build stops at the first one, so the compiler never had to reach the rest.

**Why nothing caught it.** `tests/release-honesty-gate.sh` reads the build *options* a binary
reports, not whether a configuration builds. `TELEMETRY-V1.md` §4.5 records a one-off OFF build
from the lane that added the switch; it was never wired into anything runnable. The agent control
surface then merged `ControlCommandsTelemetry.cpp` unguarded, and the configuration has been broken
since — silently, because nothing builds it. That is the structural half of the defect, and §7 is
the answer to it.

## 2. The sites guarded, and why each one was needed

Everything is guarded by `ZENE_TELEMETRY_ENABLED` (set by the root `CMakeLists.txt` when the option
is ON, `#undef`'d when it is OFF). No new source file was added: `#ifdef`s where a guard fits,
build-system conditions where the option's own wording is "removes … from the build".

| site | what it is | why it needed a guard |
| --- | --- | --- |
| `src/core/ControlCommandsTelemetry.cpp` | the `telemetry.consent` / `telemetry.status` group | names `TelemetryConsent`, `Telemetry`, `TelemetryPayload`, `TelemetryHardware` in `consentState()`, `telemetryStatus()` and `openTelemetryConsentScreen()`; this is the reported failure. Whole file guarded: `#ifdef` after `#include "lmmsconfig.h"`, `#else`/`#endif` at the bottom (see §2.1) |
| `src/core/Telemetry.cpp` | the client (payload builder, consent store, `submit()`) | its `#else` branch kept one symbol alive (`Telemetry::isCompiledIn()`). That definition is gone: the differential a packager can run is "is it there?", not "is it unused?", and a predicate whose job is to report the client's absence is still a client symbol. The TU now compiles to nothing |
| `src/core/ControlRegistry.cpp` | `registerControlCommands()` — the call site | `registerTelemetryCommands(registry)` would otherwise name a function that no longer exists, and the registry would still advertise the two ids in a binary that cannot answer them |
| `src/core/ControlReversibilityTable.cpp` | the A16 contract table | its two `R("telemetry.*", …)` rows must be absent when the commands are, or `ReversibilityContractTest` fails in both directions (every row must name a registered command; every registered command must have a row). **Also needs `#include "lmmsconfig.h"`** — see §2.2 |
| `src/core/CMakeLists.txt` | the source lists | `core/Telemetry.cpp`, `core/TelemetryNetworkTransport.cpp` and `core/ControlCommandsTelemetry.cpp` are listed only when the option is ON, so with OFF the compiler is not invoked on them at all and not even their **debug-info file names** reach the binary. Both list positions are preserved, so the ON source list is byte-for-byte the same list |
| `include/ControlRegistry.h`, `include/Telemetry.h` | the declarations/contract comments | stated the superseded contract ("Registered whatever `-DZENE_TELEMETRY` says", "collapses to `isCompiledIn() == false`"). Rewritten line-for-line, so no compiled line moves |
| `tests/src/core/ControlRegistryTest.cpp` | the registry suite | the required-id list and the telemetry slot are split into ON/OFF halves; the OFF half asserts the ids are **absent** and that the count is the product surface (72) plus this binary's five synthetic commands |
| `tests/src/core/TelemetryTest.cpp` | the client suite | its OFF slot called `Telemetry::isCompiledIn()`, which no longer exists; it now asserts the observable half (the registry declares no `telemetry.*`) |
| `tests/agent_surface_lib.py`, `tests/agent-surface-gate.py`, `tests/CMakeLists.txt`, `tests/agent-surface-allowlist.txt` | the A15 agent-surface gate | its allowlist names `telemetry.consent`, which an OFF build does not declare — the gate's own rule calls that a stale entry. `tests/CMakeLists.txt` passes `--compiled-out telemetry.consent` **only** when the option is OFF, and the gate checks the flag in both directions (a name declared compiled out that the registry *does* declare is a failure), so the flag cannot be used to excuse a real entry |

Not touched, deliberately: `src/gui/MainWindow.cpp` (its Help-menu entry has been inside
`#ifdef ZENE_TELEMETRY_ENABLED` since #617), `src/gui/TelemetryConsentDialog.cpp` and
`src/core/TelemetryNetworkTransport.cpp` (already CMake-conditional), and the client's class
declarations in `include/Telemetry.h` (headers emit no code, and in OFF nothing includes it).

### 2.1 Why the guard sits after `#include "lmmsconfig.h"`

`ZENE_TELEMETRY_ENABLED` is defined by the *generated* `lmmsconfig.h`, not on the command line. A
guard placed before the first include is therefore read with the macro undefined **in every
configuration** — the file compiles to an empty translation unit in ON too, and the ON link failed
with `undefined reference to lmms::openTelemetryConsentScreen()` from `MainWindow.cpp:440`. The
repair is one line: `#include "lmmsconfig.h"` first, the guard immediately after it. (Caught by
building ON; the object-hash check alone would not have caught it.)

### 2.2 A real ON-side regression, caught by the differential

`ControlReversibilityTable.cpp` includes only `ControlReversibility.h`, which does **not** transitively
include `lmmsconfig.h`. Guarding its two rows there made the `#ifdef` read "off" in the *ON* build
too, so the ON build silently dropped two rows the registry does declare. The first controlled
comparison showed it: `.rodata` was 672 bytes smaller than the baseline and 28,356 bytes of `.text`
had moved (address shifts). Fixed by including `lmmsconfig.h` explicitly, with a comment saying why.
This is the argument for the ON side of the proof being a *build comparison* and not a reading of
the diff.

## 3. The registry: 74 commands ON, 72 OFF

Measured on the live control socket of each build — the same surface an agent or a packager sees —
by asking `control.commands_list` and sorting the ids
(`tests/integration-logs-telemetry-off/on-command-ids.tsv`, `off-command-ids.tsv`):

```
ON : 74 commands       OFF: 72 commands
diff on-command-ids.tsv off-command-ids.tsv
  60,61d59
  < telemetry.consent
  < telemetry.status
```

Exactly the two ids, and nothing else. 72 is the release notes' own figure for the surface before
the telemetry fix ("it held 72 before the telemetry fix that added the two `telemetry.*` commands").
The `agent_surface` gate sees the same thing from the other side (see §4.3).

## 4. The differential — both configurations, both measures

`nm -C <binary> | grep -ci telemetry` counts *symbols*; `strings` counts *lines*. The strings number
is taken twice, because `-g` embeds the **absolute build path** in every DWARF string and this
workspace's worktrees are literally named `…/zene-next-telemetry-off`:

| measure | ON | OFF |
| --- | --- | --- |
| `nm -C zene \| grep -ci telemetry` | **99** | **0** |
| `strings zene-no-debug \| grep -ci telemetry` (debug-stripped: what ships) | **156** | **0** |
| `strings zene \| grep -ci telemetry` (raw, DWARF paths included) | 690 | 385 — **all 385** on this checkout's own path |
| `agent_surface` report: commands / swept / allowlisted / compiled out | 74 / 73 / 1 / 0 | 72 / 72 / 0 / 1 |

The raw OFF count is not a leak: every one of the 385 lines is the string
`/home/…/zene-next-telemetry-off/…` recorded in the debug info, and the check
`strings build-off/zene | grep -i telemetry | grep -cF "$PWD"` returns 385 of 385. Stripping the
debug info removes exactly that and nothing the code carries, which is why
`tests/telemetry-off-build.sh` measures the stripped copy and prints the raw figure beside it.

The release session's figures for the ON build were `nm` **99** and `strings` **305**; my `nm` is
identical, so the configuration matches. I could not reproduce 305: on this build the raw count is
690 and the stripped count 156, neither of which is 305 — if that reading came from an unstripped
binary, or a different binutils/`strings` default, the number moves with the environment. What the
differential is for is the ratio and the OFF side, and the OFF side is 0 under either method.

### 4.1 What the OFF binary no longer contains

Not merely "unused": the client's translation units are not compiled, so there is no `Telemetry`
symbol (`submit`, `buildPayload`, `loadConsent`, `saveConsent`, `isCompiledIn`), no
`TelemetryNetworkTransport`, no Qt Network reference, no `TelemetryConsentDialog`, no
`telemetry.*` command id, no consent or payload string, and no source file named `Telemetry.*` in
the symbol table. The pre-fix binary carried 99 symbol lines of that material.

### 4.2 A second finding on the way there

The first OFF build compiled and *still* reported 1 symbol: `_GLOBAL__sub_I_Telemetry.cpp`, the
static-initialisation thunk every non-empty TU gets, whose name embeds the file's basename. It was
the residue of a TU that only included headers. Moving the guard above the project includes (with
`lmmsconfig.h` still first) makes the OFF translation unit genuinely empty, and the symbol is gone.
Without the differential this would have shipped as "the count is 1, close enough".

### 4.3 The registry assertion inside the suite

`ControlRegistryTest::telemetryCommandsAreAbsentWhenTheClientIsCompiledOut` (OFF half) asserts
`!hasCommand("telemetry.consent")`, `!hasCommand("telemetry.status")`, `command(...) == nullptr` for
both, that no registered id starts with `telemetry.`, and the count. The count in a *test* binary is
the product surface plus the five synthetic commands that file registers itself (72 + 5 = 77), which
is why it is written that way rather than as a bare 72. In the ON half the same suite asserts the
two commands are present, `telemetry.status` declares no `requires`, `telemetry.consent` declares
`human`, and the registry refuses it for an automated caller before the handler runs.

## 5. The ON configuration is untouched

### 5.1 The object hash

```
1278d6bf062ea5cf8c8f7a0f15ff9267659817132216406a1216d41e12e45979  (the release session's value)
66296a8c61b7ed4872c6552a573edf0f19c2ec2c5f7be63518edbde540291fb2  before the change, in this worktree
66296a8c61b7ed4872c6552a573edf0f19c2ec2c5f7be63518edbde540291fb2  after the change, in this worktree
```

The two values differ because the object embeds the **absolute source path**: `readelf
--debug-dump=info` on it reports `DW_AT_name:
/home/…/zene-next-telemetry-off/src/core/ControlCommandsTelemetry.cpp` and `DW_AT_comp_dir:
/home/…/zene-next-telemetry-off/build/src`. That is verifiable in one compile: the same file, same
flags, plus `-ffile-prefix-map=$PWD=/probe/other/path`, hashes to a third value
(`3d69b086c8b1a5707535b349983767e507c65d2d826eeca2011356e8520fd919`). So the release's hash is not
reproducible from another worktree, and the meaningful comparison is the one the brief asks for:
**before and after, inside one worktree, on the same path** — where it is byte-identical, and has
stayed byte-identical through three separate rebuilds (baseline, the controlled A/B of §5.2, and
the final tree).

### 5.2 The controlled A/B — every section of the binary

The object's identity is the required proof, but it is not sufficient on its own: another
translation unit could have moved. So the whole binary was compared the only way that isolates the
patch — **stash the diff, rebuild in the same build directory with the same CMake cache, then pop
and rebuild**. In that A/B the only variable is the source diff. Per section, on debug-stripped
binaries with identical section sizes:

```
.text                      IDENTICAL   4273257 bytes
.rodata                    DIFFERS       331592 bytes   (3 bytes)
.rela.dyn .dynsym .data .data.rel.ro .got .eh_frame
.gcc_except_table .plt* .init_array ...   IDENTICAL
.note.gnu.build-id         DIFFERS            36 bytes   (a hash of the linked content)
```

`.text` — the code — is byte-identical, as are every relocation, the exception tables, the GOT and
all data. The three `.rodata` bytes decode as a millisecond timestamp: Qt's `rcc` records each
resource's modification time in `qt_resource_struct`, and the resource here is
`build/CONTRIBUTORS`, which the configure step rewrites. The anchor's value is
`0x1a0960b7520` → `2026-09-12T14:35:28.416Z`; the after-build's is `0x1a09611590e` →
`2026-09-12T14:41:54.446Z`, which is exactly `build/CONTRIBUTORS`'s mtime. **No code differs; a
build-directory file's timestamp does** (full table:
`tests/integration-logs-telemetry-off/on-anchor-vs-after-sections.txt`).

This also means the shipped binary's whole-file sha256 is not reproducible across reconfigures in
this tree, for reasons unrelated to any patch: `build/zene` hashed to `d9d5b85c…` (first build),
`09fdbfee…` (the stashed anchor) and `5425764d…` (with the patch) — three values, the first
difference being that timestamp. The object hash and the section comparison are therefore the sound
evidence, and the whole-file hash is recorded for the record, not as a criterion.

### 5.3 The suite and the render

| check | before | after |
| --- | --- | --- |
| `ctest` (from `build/tests`, `-j2`) | exit 0, **86/86** | exit 0, **86/86** |
| render of `data/projects/shorties/sv-DnB-Startup.mmpz` | `943e3238…` | `943e3238…` |
| render data chunk | `b37cefc5…` | `b37cefc5…` |

The render recipe is the one this project has been using:

```
QT_QPA_PLATFORM=offscreen build/zene render data/projects/shorties/sv-DnB-Startup.mmpz -o <out>.wav -f wav
```

Full hashes: `943e323864cb5c07bb8da264736978be9294513b93c1b6b81421cb6d8a890526` (2,177,120 bytes),
`data` chunk `b37cefc5a97e2d4664bbb0087a187935cdb9421030492f3b21972c3a7e3e59ca`
(`tests/integration-logs-telemetry-off/on-baseline-render-sha256.txt`,
`on-after-render-sha256.txt`, both `.wav` files kept, `on-render-chunks.txt`).
`agent_surface` in ON reports 74 commands / 73 swept / 1 allowlisted / 0 compiled out, unchanged.

## 6. The OFF configuration now works

Command, from the repository root, with every exit code measured unpiped
(`tests/integration-logs-telemetry-off/off-kill-switch-gate.log`):

```
bash tests/telemetry-off-build.sh build-off --jobs 4
configure EXIT=0    (ZENE_TELEMETRY:BOOL=OFF; -DUSE_WERROR=ON; deviation: no Qt5 here, so -DWANT_QT6=ON)
build     EXIT=0
nm -C build-off/zene | grep -ci telemetry       = 0
strings (debug-stripped) | grep -ci telemetry   = 0
raw strings (DWARF build paths included)        = 385   (385 of 385 on this checkout's path)
ControlRegistryTest EXIT=0
OFF_KILL_SWITCH_GATE_EXIT=0
```

and the suite in that configuration: `ctest` from `build-off/tests` → **exit 0, 86/86**
(`off-ctest.log`), including `ReversibilityContractTest` (table ↔ registry agreement),
`ControlRegistryTest` (the 72 assertion) and `agent_surface` (72 commands with the one allowlist
entry accounted for by `--compiled-out`).

### 6.1 The repository's own gates, after the two commits

```
$ bash tests/run-all-gates.sh
gate   name                     result
1      ctest                    PASS
2      coverage                 SKIP      (needs --with-coverage)
3      no-tautology             PASS
4      complexity               PASS
5      mutation                 PASS
6      upstream-regression      FAIL      <- pre-existing, see below
7      file-length              PASS
8      duplication              PASS      (1.39% of a 5% budget)
9      fork-sources             FAIL      <- pre-existing, see below
10     unregistered-tests       PASS
RESULT: FAIL — see the failing gate above
```

Two gates fail, and both failures are on the base tip, not from this lane — each is a file this
lane never touched, added before these commits:

* **Gate 6** — 22 `.log` files under `docs/release-verification-0.2.0-alpha/`. Gate 6 allows
  `tests/*`, `*.md`, `*/CMakeLists.txt` and `tools/*` by shape; a `.log` under `docs/` is none of
  those, so it is "undeclared change". `git log -1 -- docs/release-verification-0.2.0-alpha/` is the
  base tip itself (`f60cd6cc7`, *"docs(evidence): release-verify's run on the frozen tip, logs
  only"*), and `git diff --name-only HEAD~2..HEAD | grep -c release-verification` is **0**.
* **Gate 9** — `tools/diagnostics/abort-trace.c`, added by `f2be62fec` (also on the base tip), and
  not in my diff. It is registered nowhere, which is the gate-debt lane's item, not this one's; this
  lane's own new file (`tests/telemetry-off-build.sh`) is a `.sh` under `tests/` and needs no
  manifest entry (§7).

Gate 4 initially failed *on this lane's own work* — `tests/agent-surface-gate.py` at 514 lines and
`tests/src/core/ControlRegistryTest.cpp` at 513, both over its 500-line limit for a file with no
baseline entry. Both were trimmed below the limit (500 and 498) rather than baselined, because
raising a ratchet to fit new code is how the ratchet stops meaning anything.

## 7. The guard that stops the next merge doing the same thing

`tests/telemetry-off-build.sh <build-dir>` configures `-DZENE_TELEMETRY=OFF` on top of the release
flag set, builds, and then *measures* the resulting binary: `nm` and `strings` must both be 0, and
when `ControlRegistryTest` was built it is run too. It fetches nothing, prints every deviation it
applies, and is registered in `tests/QA-GATES.md` beside the release-honesty guard (section
"Packager kill-switch build guard"). It is not a ctest test — it needs a full configure and build.

`-DUSE_WERROR=ON` is load-bearing and the script always passes it: without it the original defect is
a warning, and the configuration builds while shipping the client.

**The negative control.** A guard that cannot fail is not a guard. The same two commands, run on the
ON binary, read 99 and 156, so the script's `≠ 0` test fires — and the pre-fix tree's OFF
configuration is recorded failing at the compile step in §1 (exit 2, the exact `-Werror` error).
What the script does *not* do is run the suite in that configuration; that half is the ctest run in
§6 and the ON-side proof in §5.

**Registration.** `tests/telemetry-off-build.sh` is a `.sh` under `tests/`, and this repository's
Gate 9 (`tests/fork-sources-gate.sh`) scopes a non-`tools/` path by the C/C++ extension set
(`is_source`), so a shell script under `tests/` is outside every manifest it reads and needs no
home — stated in `tests/fork-sources.txt` itself for the `tests/*.py` scripts ("a `.py` under
`tests/` is outside every list this gate reads and needs no home"). Registering it there would widen
the fork-scoped file-length/complexity/duplication ratchets onto a shell script. Gate 9's run on
this tree reports **0 stale entries** and the one pre-existing unregistered file it already had
before this lane (`tools/diagnostics/abort-trace.c`, untouched here — it is the gate-debt lane's).

## 8. Is this a guard-sized fix, or a bigger job?

**Guard-sized.** No missing symbol or half-included code was revealed across the audio core, the
engine or the DSP sources: the OFF build compiles, links and passes 86/86, and the ON build's
`.text` is byte-identical. Nothing under `src/core/audio/`, the mixer or any DSP source was touched
(`git diff --name-only` lists only the sites in §2 plus docs). Two things were larger than "add an
`#ifdef`", and both are recorded above rather than smoothed over: the `lmmsconfig.h` include that
one file needed to *see* the switch at all (§2.2), and the build-system condition that keeps the
client's translation units — and their debug-info file names — out of the OFF build entirely (§2).

## 9. Honest limits

* The OFF build here was configured with `-DWANT_QT6=ON` (this box has no Qt5 development files).
  Printed as a deviation by the script; the defect and the fix do not involve Qt's major version.
* Cross-worktree object hashes are not comparable (§5.1). The proof is intra-worktree, as asked.
* `release-honesty-gate.sh` still cannot see this class of defect, because it judges a build's
  reported OPTIONS and this defect is about whether a configuration builds. A follow-up worth having
  (not taken here: outside this release's frozen scope, and `.github/workflows/` was explicitly out
  of bounds) is a CI job that runs `tests/telemetry-off-build.sh` on one job of the matrix, and/or
  exposing a `LMMS_HAVE_TELEMETRY` row in `tests/advertised-features.tsv` so `lmms --version` can
  report whether the binary carries the client.
* The `strings` figure for the ON build could not be reproduced as the brief's 305 (§4); both of my
  methods and their numbers are recorded rather than adjusted.
* Two of the repository's own gates (gate 6 upstream-regression and gate 9 fork-sources, §6.1) fail
  on this tree for files that were already unregistered on the base tip and are untouched here. This
  lane does not change either verdict, and deliberately does not register another lane's file.

## 10. What changed

New: `tests/telemetry-off-build.sh`, `docs/TELEMETRY-KILL-SWITCH.md`, and the evidence under
`tests/integration-logs-telemetry-off/` (both commands' configuration logs and build logs, both
differentials, both command-id lists, both ctest logs, both render logs and wavs, the section
comparison, the pre-fix defect reproduction).

Changed: `src/core/Telemetry.cpp`, `src/core/ControlCommandsTelemetry.cpp`,
`src/core/ControlRegistry.cpp`, `src/core/ControlReversibilityTable.cpp`,
`src/core/CMakeLists.txt`, `include/ControlRegistry.h`, `include/Telemetry.h`,
`tests/CMakeLists.txt`, `tests/agent-surface-gate.py`, `tests/agent_surface_lib.py`,
`tests/agent-surface-allowlist.txt`, `tests/src/core/ControlRegistryTest.cpp`,
`tests/src/core/TelemetryTest.cpp`, `tests/QA-GATES.md`, `docs/TELEMETRY-V1.md`,
`docs/AGENT-SURFACE-TELEMETRY-FIX.md`, `docs/KNOWN-LIMITATIONS.md`.

Committed in three steps on `fix/telemetry-kill-switch`: the guards and the build-system condition;
the gate and the suite's two halves; the report and the evidence.
