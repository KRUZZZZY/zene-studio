# Cluster review-c67e8dfd1-testpreservation — the meter fix had replaced the arm/undo assertions it claimed to keep

Verdict: **FIXED** (test-preservation; no product file changed).

## The finding

Luna's `fix(control): skip no-op meter arm history` (commit `c67e8dfd1`) is a
correct product change, but it **replaced** the assertions in
`tests/control-meter-commands.py`'s `check_master_arm_undo` instead of adding to
them. The original pair

* `armed for the undo check` — `meter.get_state().armed is True` after a real arm,
* `control.undo returns the tap to its previous armed state` — `armed is False`
  after one `control.undo`

was rewritten so that the same two names now assert the *opposite* direction of the
same stack (armed `False` → undo → armed `True`), so the file no longer proves that
`control.undo` disarms an arm at all. The no-op coverage the fix added is genuine
and stays; the original coverage had to come back unchanged beside it.

## Evidence

- `tests/control-meter-commands.py:580` — `check_master_arm_undo()` is restored to
  the pre-`c67e8dfd1` text, byte for byte (`git show c67e8dfd1^:tests/control-meter-commands.py`):
  docstring `The inverse: control.undo takes the armed flag back.`, the same
  `meter.arm false` / `meter.arm true` pair, `armed is True`, then one
  `control.undo` and `armed is False`.
- `tests/control-meter-commands.py:596` — **new** helper
  `check_noop_disarm_adds_no_history()` carries the new scenario verbatim:
  arm/disarm/disarm, `transactions_before_noop`, a repeated disarm,
  `transactions_after_noop`, `armed is False`, one `control.undo`, then
  `a no-op disarm adds no undo step` (`armed is True`) and
  `a no-op disarm adds no transaction record`
  (`transactions_after_noop == transactions_before_noop`). Its setup check is
  renamed to `disarmed for the no-op undo check` only because the recorder appends
  by name and two checks called `armed for the undo check` would read as one
  replaced check in the report; the assertion and its evidence string are the
  new test's own.
- `tests/control-meter-commands.py:648` — `run_checks()` calls the new helper after
  the restored one, so the original scenario runs first and the new one reads the
  stack it left (disarmed), which is deterministic in either entry state: with the
  product fix, a no-op arm pushes no step, so in both cases exactly one `arm` sits on
  top and one `control.undo` reverses it.
- Preserved untouched, as the finding requires: the binary-backed harness
  (`H.start_instance(binary)`, `$<TARGET_FILE:zene>`), the fixture's initial
  disarm in `play_and_measure()` (`tests/control-meter-commands.py:243`) and the
  `re-sending an arm preserves the in-progress measurement` check at `:379`.
- Why the product fix cannot be measured by the restored pair: `meter.arm` declares
  no coalescing target (`src/core/ControlReversibilityTableMeter.cpp:83` sets
  `coalesceTarget = nullptr`), so consecutive arms are separate steps
  (`ProjectJournal::addJournalAction`, `src/core/ProjectJournal.cpp:252`), and the
  restored pair passes on the pre-fix product too — as the RED log below shows. It is
  preserved coverage, not new detection; the no-op checks are the ones that catch the
  product bug.
- No ledger change: `tests/control-meter-commands.py` is a fork-new test script
  (added by `28a9246aa`) that is in none of `tests/fork-sources.txt`,
  `tests/all-sources.txt` or `tests/upstream-modifications.txt`. Registering it now
  would newly expose a 711-line `.py` to the fork-scope file-length ratchet and add a
  regression, so it is left as it was found (this commit changes no new or inherited
  product file).

## Commands run (logs in this run's log folder)

Baseline (the committed, weakened test on the committed product) — a missing
plugin/test binary had to be built first:

```
cmake --build build --parallel 12 --target zene MeterTapTest audiofileprocessor > LOGS/build-1.log 2>&1                 EXIT=0
cd build/tests && ctest -R '^(ControlMeterCommands|MeterTapTest)$' --output-on-failure > LOGS/test-2-baseline-green.log 2>&1   EXIT=0   # 2/2 passed
```

GREEN with the strengthened test and the product fix in place:

```
cd build/tests && ctest -R '^ControlMeterCommands$' --output-on-failure > LOGS/test-3-restored-green.log 2>&1            EXIT=0   # 1/1 passed
python3 tests/control-meter-commands.py build/zene > LOGS/test-3b-restored-checks.log 2>&1                               EXIT=0
cd build/tests && ctest -R '^(ControlMeterCommands|MeterTapTest)$' --output-on-failure > LOGS/test-5-green-family.log 2>&1    EXIT=0   # 2/2 passed
```

`test-3b-restored-checks.log` (43 checks, all `ok`) ends with the restored pair and
the new scenario side by side:

```
  armed for the undo check                                           ok
  control.undo returns the tap to its previous armed state           ok
  disarmed for the no-op undo check                                  ok
  a no-op disarm adds no undo step                                   ok
  a no-op disarm adds no transaction record                          ok
```

RED — the test catches the product bug. `git diff -- <product files changed by this
commit>` is **empty** (no product file changed), so the `git apply -R` recipe has no
patch to reverse; the equivalent proof is to reverse the product fix this suite
guards, `git show c67e8dfd1 -- src/core/ControlCommandsMeter.cpp | git apply -R`
(`LOGS/product-fix.patch`), then re-apply it:

```
git apply -R LOGS/product-fix.patch                                                                                    # reversed, exit 0
cmake --build build --parallel 12 --target zene > LOGS/build-2-prefix-red.log 2>&1                                    EXIT=0
python3 tests/control-meter-commands.py build/zene > LOGS/test-4-red-no-product-fix.log 2>&1                            EXIT=1
git apply LOGS/product-fix.patch                                                                                        # re-applied, exit 0
cmake --build build --parallel 12 --target zene MeterTapTest audiofileprocessor > LOGS/build-3-green.log 2>&1          EXIT=0
```

`test-4-red-no-product-fix.log` — the three no-op checks fail on the pre-fix
product, and the restored original pair holds on both products, exactly as the
evidence above says:

```
  re-sending an arm preserves the in-progress measurement            FAILED
      before=174 after=1
  armed for the undo check                                           ok
  control.undo returns the tap to its previous armed state           ok
  disarmed for the no-op undo check                                  ok
  a no-op disarm adds no undo step                                   FAILED
      armed=False after one undo
  a no-op disarm adds no transaction record                          FAILED
      before=[{... 'before': {'armed': True} ... 'step': 18}] after=[{... 'before': {'armed': False} ... 'step': 19}]
```

### Ratchet gates

```
bash tests/complexity-gate.sh --check  > LOGS/gate-complexity.log 2>&1        EXIT=1
bash tests/file-length-gate.sh --check  > LOGS/gate-file-length.log 2>&1       EXIT=1
```

Both gates **fail on this branch before and after this change, identically**: the
same run over a pristine `git archive HEAD` export gives EXIT=1 for both and the
`REGRESSION:` lines diff clean (`LOGS/gate-complexity-pristine-HEAD.log`,
`LOGS/gate-file-length-pristine-HEAD.log`). Every entry is a C++ file this commit
does not touch (e.g. `src/core/SmfInterchangeReader.cpp` (504),
`lmms::adjust@src/core/ControlCommandsMixerTools.cpp` (CCN 12)); no gate limit or
baseline was re-anchored. The only file changed here is a `.py` outside both gates'
source list, so it cannot move either result.

## Files changed

- `tests/control-meter-commands.py` — original `check_master_arm_undo` restored
  verbatim; new `check_noop_disarm_adds_no_history` carries the repeated-disarm
  scenario; `run_checks` calls it; the docstring's "WHAT IT ASSERTS" list names the
  restored inverse check.