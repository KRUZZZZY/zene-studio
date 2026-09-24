# BUG-CTL-2 / BUG-CTL-3 / BUG-SRV-1 / BUG-SRV-2 — regression pass, 2026-09-24

Lane `bugs/hunt-2026-09-24` (worktree `zene-bugs`). Four validated fixes were left
uncommitted by the previous session with no tests and no ledger entries. This pass
verified them, added the missing regression tests and registrations, proved each
observable fix load-bearing, and committed the lot.

Scope note: **BUG-CTL-1 is out of scope** — a separate task owns it.

## Where the logs are, and why they are not here as `.log`

Gate 11 (`tests/evidence-gate.sh`) refuses a `.log` **anywhere in the tree**, and
two of this run's transcripts exceed its 1 MiB cap
(`baseline-targeted.log` 1426763 bytes, `final-targeted.log` 1434097 bytes). So a
tracked `.log` cannot be landed under `docs/<run>-logs/`, `tests/integration-logs-*/`
or anywhere else: `bash tests/evidence-gate.sh --tree docs/bug-fix-2026-09-24-logs`
reports **20 of 20 files refused**. (`ls tests/integration-logs-*` matches nothing
in this tree — REPO-1/CP-1 removed all 17 of those directories and kept their
hashes in `tests/evidence-manifest.tsv`.)

The convention used here is the one the tree already keeps, and it satisfies both
rules:

* **prose evidence** under `docs/bug-fix-2026-09-24-logs/` as `.md` — the
  `docs/<run>-logs/` home DELEGATION-RULES names, in the shape `docs/706-logs/`,
  `docs/708-logs/` and `docs/709-logs/` already use (all `.md`, no `.log`);
* **the raw logs' sha256 and size** in `tests/evidence-manifest.tsv` — Gate 11's own
  documented shape ("record hashes, not megabytes").

`bash tests/evidence-gate.sh` → `EXIT=0`, `0 refused (6942 files scanned)`;
`bash tests/unregistered-tests-gate.sh` → `EXIT=0`.

## The regression test

`tests/control-surface-hardening.py` (ctest `ControlSurfaceHardening`, registered in
`tests/CMakeLists.txt`, entry + recipe pathspec in `tests/fork-sources.txt`). It
starts the real binary and drives four cases over `--control-socket`:

| case | BUG | assertion |
| --- | --- | --- |
| `channel targets` | CTL-2 | `ch-foo` → `invalid_args`, `ch-9999` → `not_found`, on `plugin.param_get` **and** `chain.save` |
| `request id validation` | SRV-1 | `1e300`, `-1e300`, `2.5`, `2147483648`, `NaN`, `"1"` each answered `invalid_args`; instance still serving |
| `client cap` | SRV-2a | past `MaxClients` (64) the excess peers see a **close**, not a hang; pre-burst session unaffected |
| `idle deadline` | SRV-2b | a partial read does **not** extend the 30 s deadline; a completed exchange **does** |

The two bounds are asserted against `include/ControlServer.h`'s own text
(`declared_bounds`), so a change to either constant fails loudly instead of the test
silently measuring the old number.

```
cd build/tests
QT_QPA_PLATFORM=offscreen LMMS_PLUGIN_DIR=<abs>/build/plugins \
  CTEST_JOBS=1 ctest -R '^ControlSurfaceHardening$' --output-on-failure
```

## Load-bearing proofs

Method: every production hunk of the four fixes was reverted at once in the working
tree, `zene` was rebuilt once (`cmake --build build --target zene -j2`, 49 s), the
test was run, and the fixed sources were restored and rebuilt. The four cases are
independent code paths, so each failed by name against its own defect.

| BUG | test case / control | red | green |
| --- | --- | --- | --- |
| CTL-2 | `channel targets` | `EXIT=1`, case FAILED (2): `ch-foo` answered `not_found` | `EXIT=0`, case ok |
| SRV-1 | `request id validation` | `EXIT=1`, case FAILED (8): `1e300` served with `id: -2147483648`, `2.5` with `id: 2` | `EXIT=0`, case ok |
| SRV-2a | `client cap` | `EXIT=1`, case FAILED (2): 80 peers accepted, none refused | `EXIT=0`, case ok |
| SRV-2b | `idle deadline` | `EXIT=1`, case FAILED (1): the stalled peer hung, never retired | `EXIT=0`, case ok |
| CTL-3 | 4 controls in `ControlNegativeControl` | `EXIT=1`, 4 × "did NOT detect the defect it targets" | `EXIT=0` |

BUG-CTL-3's proof lives in `tests/control-negative-control.py` — the tree's existing
assertion-level negative-control harness, whose whole purpose is "a test that cannot
fail is not evidence". Two controls feed the group-coverage checker a duplicate and
an empty command id (its `command_ids` must **raise**), two feed the snapshot
checker a fixture bundle through the bridge's own `load_bundle` (its `committed_ids`
must return the refusal). Against the pre-fix checkers all four report "did NOT
detect"; against the fixed ones all four report "rejected". No new registration was
needed — the file is already registered.

### Observed limit on BUG-SRV-1

The fix's `!std::isfinite(idNumber)` clause is **unreachable from the wire**: Qt's
JSON parser rejects the `NaN` literal and `1e400` as "illegal number" before either
can become a `QJsonValue`, so those two ids are refused by the parser (same kind
`invalid_args`, different message) and the range clause is what the test actually
exercises. The `isfinite` clause is defensive, not covered by an observable test.
The test asserts the **kind** for all six ids and the id-check **message** only for
the four that reach it, so it never claims the new code ran when the parser had
already refused the line.

## Registration

* `tests/control-surface-hardening.py` → `tests/fork-sources.txt` (fork-NEW entry, in
  C-sorted position) **and** a named pathspec line in that manifest's own
  "Regenerate with" recipe (the `tests/src/core` awk keep-list cannot derive a
  `tests/*.py`). Verified: the recipe now emits the new path, so the entry is
  derivable rather than hand-placed.

  **Pre-existing defect reported, not repaired (out of scope).** Running
  `tests/fork-sources.txt`'s own "Verify it" recipe shows **14 entries that the
  recipe cannot derive**, every one of them added by an earlier committed batch on
  this branch, and **6 entries out of C order** (`include/MidiOutQueue.h`,
  `include/XmlDepthGuard.h`, `src/core/ScriptCommandQueue.cpp`,
  `tests/src/core/MidiOutQueueTest.cpp`, `tests/src/core/RetroMidiRingTest.cpp`,
  `tests/src/core/StemJobManagerTest.cpp`). Thirteen are `tests/src/core/*.cpp`
  dropped by the recipe's `tests/src/core` awk keep-list; `tests/control-feedback-commands.py`
  is named in no pathspec at all. No gate runs this recipe
  (`tests/all-sources-reproduce.sh` checks `all-sources.txt` only), so Gate 9 is
  green regardless — and re-deriving the list would reorder other lanes' entries and
  relocate their note blocks, which is their registration territory, not this task's.
  The measurement above is the hand-off.
* `tests/CMakeLists.txt` → the `ControlSurfaceHardening` ctest, `TIMEOUT 300`, the
  same offscreen `ENVIRONMENT` as its neighbours. No existing test's registration,
  environment, timeout or property changed.
* `tests/control-negative-control.py` → four controls added; strengthening only, no
  existing control changed.
* `tests/all-sources.txt` → **pre-existing Gate 9 failure repaired.** The committed
  batches added `include/MidiOutQueue.h`, `include/XmlDepthGuard.h` and
  `tests/src/core/MidiOutQueueTest.cpp` to `tests/fork-sources.txt` but not to the
  whole-tree manifest, so Gate 9 was red at HEAD before this pass
  (`bash tests/fork-sources-gate.sh` → `EXIT=1`, "does not reproduce"). The entry list
  was **re-derived by running the manifest's own recipe** (not hand-edited): exactly
  those three paths added, none removed, and `bash tests/all-sources-reproduce.sh`
  now prints `REPRODUCES` (`EXIT=0`).

## Final battery (from `build/tests`, `CTEST_JOBS=1`, unpiped)

```
EXIT=0 — 100% tests passed, 0 tests failed out of 10 (67.97 s)
  ControlDeviceCatalogueTest     Passed    1.51 s
  ControlEditCommandsTest        Passed    1.44 s
  ControlRegistryTest            Passed    1.39 s
  ControlSurfaceReferenceTest    Passed    1.39 s
  ControlShutdownHookTest        Passed    0.05 s
  ControlVerbInverseTest         Passed    1.43 s
  ControlNegativeControl         Passed   14.37 s
  ControlCommandsSnapshot        Passed    2.92 s
  ControlMcpGroupCoverage        Passed    5.20 s
  ControlSurfaceHardening        Passed   37.96 s   (new)
```

`ControlCommandsSnapshot` id-set canary **unchanged**: 350 command ids, 56 groups,
sorted-id-set sha256 `2fcc8f64f1dd…` (the value the 2026-09-23 regeneration commit
records), `0 missing / 0 extra` against the binary in all three bridge modes;
standalone run `EXIT=0`.

## Gate verdicts

| gate | before | after |
| --- | --- | --- |
| `tests/evidence-gate.sh` | `EXIT=0` | `EXIT=0` (0 refused / 6944 scanned) |
| `tests/unregistered-tests-gate.sh` | `EXIT=0` | `EXIT=0` |
| `tests/fork-sources-gate.sh` | **`EXIT=1`** (all-sources.txt did not reproduce) | `EXIT=0` (725 fork-NEW, 1112 inherited, 48 tooling, 0 stale) |
| `tests/file-length-gate.sh` | **`EXIT=1`** | **`EXIT=1`** — pre-existing, untouched (see below) |

`tests/file-length-gate.sh` fails on `src/core/ProjectContainerEntries.cpp`, which
grew 573 → 580 lines in commit `68ac6ee9f` ("fix(ARCH-4): bound document parsing…")
without the baseline being re-anchored. Verified pre-existing: at `HEAD` the file is
580 lines and `tests/file-length-baseline.tsv` still says 573, and this commit touches
neither the file nor the baseline. **Not re-anchored**: a ratchet is moved only with the
owner's recorded reason, and blessing another lane's 7-line growth is not this task's
decision. The new test file (368 lines) and the strengthened one (437 lines) are both
under the 500-line limit and add no violation.

## The raw logs (removed; hashes kept)

20 files, 3388008 bytes, all recorded in `tests/evidence-manifest.tsv`. Reproduce any
line with `sha256sum <file>` and compare against the manifest's third column.

```
c41cda4d585ebef39647fe19912bf23dad8cde84fa3b3472c42d166dff1c27ee    2444  ControlDeviceCatalogueTest.log
b3842b705290bd362355952095a2e8d771076fdd2ff1f23d54ec7864ffd66485    1267  ControlEditCommandsTest.log
df84f5e58f4d52e1c47e0e89599dc4d455115f02833fb4629349fb22a759be66    2133  ControlRegistryTest.log
eb46270a60c982281109f7b14992625a212855a70a2cf99321fbacdd313cbf36     732  ControlShutdownHookTest.log
92a3dc308740de55e5c672efebe49dbe0b25d2554551b2fd88aac0eedca81e23    1728  ControlSurfaceReferenceTest.log
e1e4599fa03bc7627d701775acfedf2a78accc4a32d898d2a5ab8f214ec804c3    1301  ControlVerbInverseTest.log
a6f7506199088ff4df3e5abb82516e9630c046e18c95ff496a8a5fbf2a7ec3f1  467272  baseline-list.log
5d53c1c2ee4e96392ccb927c21e5f33894a4d12d9322f3b3ae781ad9df4c6382 1426763  baseline-targeted.log
e238f0a9ebc787ffe88c599085a94552d2e5ecc882bcf8300c01ba3a50f43c3f 1434097  final-targeted.log
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855       0  build-tests-final.log
3d75ae851dfa59f7f96d92c87785cc5ea6b6a6ea1d018952e6e9b591b60473e3    8620  build-zene-final.log
25a57b128fde8f7fec8f55750586987db6867c33c6e88638a9a067f192fb7fe8     149  commit-stems.log
6dab6742c6d67b6f12d5040c72f3a4fce7312fdc3d64f6e21b2ddaafe0b4e181    2939  final-build.log
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855       0  python-checkers.log
   … plus the six identical-shape files under baseline/ (see the manifest)
```

## Unverified

* The 30 s deadline is observed by **waiting 35 s** on this box (no shortened
  override exists in the product). The refresh half and the no-partial-extension
  half are measured on one timeline, but the absolute timing on a heavily loaded CI
  runner is not proven here — only the 3 s margin over the sweep interval is.
* `tests/fork-sources.txt`'s own "Verify it" recipe does **not** reproduce: 14
  pre-existing entries are not derivable and 6 are out of C order (measured above).
  This is not enforced by any gate and is not this task's to repair, but it is not
  green either — only `tests/all-sources-reproduce.sh`, which Gate 9 does run, prints
  `REPRODUCES`.
* `ControlMcpGroupCoverage`'s bridge half needs the `mcp` distribution; it passed
  here, but a box without it reports the test as Skipped (exit 77), not Passed.
* `tests/file-length-gate.sh` is red at `HEAD` on `src/core/ProjectContainerEntries.cpp`
  (pre-existing, measured above) and this commit does not change it.
* `BUG-CTL-3`'s registration-time `qFatal` (a duplicate or empty id at
  `ControlRegistry::registerCommand`) has **no death-test** and is not observable
  from a passing binary: every registered id is unique by construction, so the
  branch only fires on a programmer error at start-up. Its behaviour is documented
  in the commit message rather than tested.
