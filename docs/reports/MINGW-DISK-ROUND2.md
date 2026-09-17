# MINGW-DISK-ROUND2 — what filled the runner's disk, and the fix that fits

**Scope.** The `mingw64` job of `.github/workflows/build.yml` on the 0.3.0 line:
run `35212797698`, job `105174080269`, head `8edfe30d5`. Branch
`030/mingw-disk2`. Round 1 (`docs/reports/WPLAT-WIN-REPORT.md` item 6, commit
`d6ed6d2bc`) added a free-disk step and `CCACHE_MAXSIZE: 2G`; the job still died
`No space left on device` in **Build at 66%**, on the same `ar` line as before:

```
/bin/x86_64-w64-mingw32-ar: CMakeFiles/TimelineTest.dir/objects.a: error reading
  ../src/CMakeFiles/lmmsobjs.dir/gui/editors/PatternEditor.cpp.obj: No space left on device
gmake: *** [Makefile:156: all] Error 2
```

**What this round changes.** One thing in that job: its build stops writing DWARF
(RelWithDebInfo's flags minus `-g`), plus the ccache cache step finally points at
the directory ccache actually uses, plus two measurements so the next failure
carries its own numbers. Every target still compiles, every test binary still
links, `-Werror` is untouched, no step is removed, and no other job's region is
touched.

---

## 1. What consumed the disk — from the job's own log

Round 1's step **ran and worked**: its `df -h /` pair is the only disk
measurement in the whole log, and it brackets the build exactly.

| line | measurement |
|---|---|
| log 1743-1745 | `[free-disk-space] df -h / before`: `/dev/root 145G 61G used 84G avail 43%` |
| log 1841-1843 | `[free-disk-space] df -h / after`: `/dev/root 145G 41G used 104G avail 29%` |
| log 1838-1839 | `Total reclaimed space: 1.88GB` (docker) + the pre-installed trees (~18 G) |

So the build began with **104 G free** and died when `/` was full: **~104 G was
written by Configure + Build**. The step after Build never ran — there is no
`[ccache stats]` block in this log at all (the "Trim ccache and print statistics"
step is ordered after Build, which is why round 1's ccache figures come from the
last *successful* mingw run, `104064095838`, not from the failing one).

Everything else the job puts on `/` is small and accounted for:

| component | size | source |
|---|---|---|
| apt mingw toolchain + deps | 1,317 MB | apt's own "After this operation" line (log 931) |
| restored `build/vcpkg_installed` | ~54 MB | the cache step's `Cache Size: ~54 MB` (log 771) — so vcpkg buildtrees are **not** the consumer; the manifest install is skipped on a hit |
| restored VST3 SDK + CLAP headers | ~4 MB + ~6 MB | cache steps (log 1676) |
| ccache writes | bounded (2 G job cap) | `CCACHE_MAXSIZE: 2G` in the job env |
| image residual after the free-disk step | 41 G used | the `df -h / after` line |

The remaining ~100 G is the build tree itself, and where make stopped names the
two things in it that are large:

* **134 test executables had linked by 66%** (134 `Linking CXX executable` lines,
  every one of them a `*Test.exe`; the product links later). This tree builds
  **207** test executables and this job **runs none of them** — it has no test
  step, so the binaries are built and thrown away.
* The failure line is `ar` writing a **test target's own archive**,
  `CMakeFiles/TimelineTest.dir/objects.a`, from
  `../src/CMakeFiles/lmmsobjs.dir/*.obj`. That is CMake's rule for a
  **Windows-system target** that links an OBJECT library: the target's link
  command is `rm -f objects.a; ar qc objects.a @objects1.rsp; g++ -Wl,--whole-archive
  objects.a …`, and `objects1.rsp` lists the target's own objects **plus every object
  of the object library** — i.e. each test target writes a *copy* of the whole
  lmmsobjs object set. Reproduced on this box with a two-file project and a
  Windows toolchain file; on Linux the same CMake writes a plain `objects1.rsp`
  and links the shared objects directly, which is why the Linux builds never pay
  this.

### The rate, from two independent failures

| run | job | free space at build start | died at | test binaries linked | implied cost |
|---|---|---|---|---|---|
| run #3 | `104895806930` | ~84 G (no free-disk step; the untouched image) | 64% | ~108 | ~0.77 G per test target |
| run #4 | `105174080269` | 104 G (measured, log 1841) | 66% | 134 | ~0.78 G per test target |

Two failures, two different starting points, the same rate: **~0.75-0.8 G of
runner disk per test target**. 207 of them is **~155 G**, against the 104 G a
145 G root disk (`/dev/root`, the job's own `df`) can lend after the image, apt
and the workspace have taken theirs. Round 1 moved the wall from 64% to 66%
because the free-disk step *did* add 20 G — the job simply needs ~50 G more than
104 G, and the free space it can be given is bounded by the disk.

### What the numbers on this box say the bytes are

No MinGW toolchain exists on this machine (`which x86_64-w64-mingw32-gcc` → nothing),
so the mingw object and executable sizes **could not be measured here** — and that
is exactly why the fix had to be one that scales every part of the per-test
payload rather than one guessed part of it. What *could* be measured is the same
tree's `RelWithDebInfo` build here:

| measurement (this box, `zene-030/build`, same tree) | figure |
|---|---|
| whole build tree | 42 G |
| of which `build/tests` | **39 G** (207 test executables, 37.6 G, avg 186 MB) |
| `strip --strip-debug` on 12 test binaries | 2266 MB → **117 MB** (94% is DWARF) |
| `strip --strip-debug` on 20 lmmsobjs objects | 30 MB → **0 MB** (97% is DWARF) |
| `src/CMakeFiles/lmmsobjs.dir` (the set every test copies) | 832 MB / 562 objects |

So the per-test gigabyte is debug info, written **twice over**: once into the
lmmsobjs object library, once into each test's `objects.a` copy of it, and once
more into the test executable that links both.

### The ccache path defect (round 1 defect 2), confirmed and fixed

ccache 4.9.1's default `cache_dir` is `~/.cache/ccache` — the last successful
mingw run prints it: `(default) cache_dir = /home/runner/.cache/ccache`. The
cache step cached `~/.ccache`, so nothing ever met: every run prints
`Cache not found for input keys: ccache-mingw-64-…` and the Post step closes with
`[warning]Path Validation Error: Path(s) specified in the action for caching
do(es) not exist, hence no cache is being saved`. That run's stats
(`Cacheable calls 1618 / 2690`, `Hits 2`, `Misses 1616`, `Cache size (GB) 0.5/0.5`,
`Cleanups 188`) are the cache being re-filled and repeatedly clamped, never read.

**This is a compile-time fix, not the disk fix.** A cache hit still writes its
object into the build tree, and the cache itself is bounded at 2 G by the job env
(round 1's own choice), so it cannot return the ~50 G the job is short of. It is
in this round because it is cheap, measured, and it stops ~1,600 objects being
recompiled from scratch every run.

---

## 2. The fix

`.github/workflows/build.yml`, **mingw job only** — four hunks:

1. **Configure** gains two flag overrides, which are CMake's `RelWithDebInfo`
   flags for GNU (`-O2 -g -DNDEBUG`, the value every other job in the file still
   uses) **minus `-g`**:

   ```
   -DCMAKE_CXX_FLAGS_RELWITHDEBINFO="-O2 -DNDEBUG" \
   -DCMAKE_C_FLAGS_RELWITHDEBINFO="-O2 -DNDEBUG" \
   ```

   Same optimisation (`-O2`), same `-DNDEBUG`, same `-Werror`, same build type
   name (the guards that read `build/lmmsversion.h` see the same text), every
   target still compiled and linked. What is gone is the DWARF that the two
   failures show filling the runner.
2. **Cache ccache data**: `path: ~/.ccache` → `path: ~/.cache/ccache`.
3. **Free disk space**: `df -h /` → `df -h` (every mount) before and after, so
   the next log answers — in its own lines — whether this runner has any volume
   beside `/`.
4. **New step after Build, "Disk profile at failure"** (`if: failure()`,
   `continue-on-error: true`): `df -h`, `du -sh build`, and the summed size and
   count of `objects.a` archives and `build/tests` files. The Build step's
   failure stays the gate; this only makes the next failing run measure the
   thing round 1 and round 2 both had to infer.

### Why not the other candidates

| candidate | why not |
|---|---|
| ccache path fix alone (round 1's "next lever") | implemented, but bounded at 2 G and compile-time only — it cannot return ~50 G of build-tree bytes |
| move the build tree to a larger volume | `/` is `/dev/root`, 145 G, and the job's own `df` says so. The arithmetic above needs ~150 G for the tree alone, so no volume this runner class exposes can hold it, and round 1 already called the move "bigger workflow surgery". The free-disk step now prints every mount, so the next log settles it with a measurement instead of an assumption |
| `CMAKE_BUILD_TYPE=Release` | `-O3 -DNDEBUG` is a *different* configuration from the one the other jobs prove — a real coverage change |
| build the test targets "behind an option" / skip them | forbidden: it reduces coverage. The fix keeps every target |
| strip test binaries after linking | only removes the executable's DWARF; the per-target `objects.a` copy of the lmmsobjs set keeps its own. It also needs a post-link hook on ~207 targets |
| attack the `objects.a` duplication directly (link a static archive instead of the object library, or a generator change) | the strongest remaining lever, but it is a build-system change whose mingw behaviour **cannot be verified on this box**; recorded here as the next lever if this round's arithmetic is wrong |

### The packaging trade-off, stated plainly

This job's Windows artifacts are built without embedded DWARF. `build/zene-*.exe`
is uploaded on tags only, and the **msvc-x64** and **msys2** jobs build their own
installers with the full flag set, so the release keeps symbolized Windows
packages from those two. The x64 installer this job produces is smaller and
cannot be symbolized from itself. If LMMS ever changes its `RelWithDebInfo`
flags, the two overrides move with them (noted at the site).

### The arithmetic the next run falsifies

```
available after round 1's free-disk step ............ 104 G   (job's own df)
consumed by apt + caches + workspace ................  ~5 G   (apt line, cache sizes)
consumed by the build at 66% ....................... ~104 G   (41 G used -> full)
  of which / 134 test targets ...................... ~0.78 G  (run #4)
  run #3, no free-disk step, ~108 targets .......... ~0.77 G  (run #3)
full build, 207 test targets ....................... ~155 G
after the flag change (DWARF ~94-97% of the bytes) . ~10-20 G
```

Expected after the fix: **~60 G used by the whole job**, against 104 G available —
a headroom that survives being wrong by a factor of two. The new "Disk profile at
failure" step prints `df -h`, `du -sh build`, the `objects.a` total and the test
binary total, so a failure now carries the numbers instead of a percentage.

---

## 3. What was verified here, and what only CI can

Run unpiped from the worktree root, exit codes as returned:

| check | exit |
|---|---|
| `yamllint` over every git-tracked `*.yml` (and `build.yml` alone) | 0 |
| `python3 -c "yaml.safe_load(…)"` — jobs and step counts parse | 0 |
| `bash tests/release-staging-path-gate.sh` | 0 |
| `bash tests/test-package-upload-guard.sh` | 0 |
| `bash tests/file-length-gate.sh --check` | 0 |
| `bash tests/all-sources-reproduce.sh` | 0 |
| `bash tests/evidence-gate.sh` | 0 (6670 files scanned, 0 refused) |
| `bash tests/release-ref-fitness.sh` | 0 |
| `bash tests/test-release-ref-fitness.sh` | **1 — pre-existing** |

The last row is red **before** this round: control 5, "a leg whose tool is
absent refuses (not exit 0)", fails identically at `8edfe30d5` in a pristine
worktree (`git worktree add /tmp/wpristine 8edfe30d5`; exit 1, same line), so it
is the branch's state and not this change. The A1/A2 lines it prints under
control 6 are the *expected* refusals of its deliberately weakened workflow
copy — that leg passes on both trees.

**This machine has no MinGW and no MSVC**, so nothing here is a mingw build: the
per-test gigabyte, the `objects.a` rule's effect on the runner, and the ccache
restore are *CI-only* claims. What this round does to make them checkable is
stated as arithmetic above and printed by the job on its next failure.

**What the next CI round must prove.** (1) Build reaches 100% and `Package`
produces `zene-*.exe`; (2) the free-disk step's `df -h` block shows the volumes
and the headroom it bought; (3) the Configure comment's arithmetic holds — the
"Disk profile at failure" step's `du -sh build` and the ccache step now report a
`Cache restored from key: ccache-mingw-64-…` (with the first run after the flag
change still a miss, since the compile command changed).
