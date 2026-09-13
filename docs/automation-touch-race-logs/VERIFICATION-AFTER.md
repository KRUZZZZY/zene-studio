# AutomationModesTest SIGSEGV — verification of the fix

> Committed evidence for branch `fix/automation-touch-race`. Every exit code below was
> measured unpiped (`cmd > log 2>&1; echo EXIT=$?`), on this machine, from the build this
> branch's own `build/` directory (RelWithDebInfo, `-DUSE_WERROR=ON -DWANT_QT6=ON
> -DWANT_VST3=ON`, Qt 6.4.2). The full raw logs of the two runs are `build/ctest-final.log`
> and `build/gates-final.log`; they are not committed (332 KB) — the summary and the exit
> codes are, and one command reproduces them.

## 1. The reproduction that crashed before, after the fix

```
$ cd build/tests
$ for p in 1 2 5 17 85 170 255; do MALLOC_PERTURB_=$p ./AutomationModesTest > /dev/null 2>&1; echo "MALLOC_PERTURB_=$p EXIT=$?"; done
MALLOC_PERTURB_=1 EXIT=0
MALLOC_PERTURB_=2 EXIT=0
MALLOC_PERTURB_=5 EXIT=0
MALLOC_PERTURB_=17 EXIT=0
MALLOC_PERTURB_=85 EXIT=0
MALLOC_PERTURB_=170 EXIT=0
MALLOC_PERTURB_=255 EXIT=0
$ MALLOC_CHECK_=3 ./AutomationModesTest > /dev/null 2>&1; echo "MALLOC_CHECK_=3 EXIT=$?"
MALLOC_CHECK_=3 EXIT=0
$ ./AutomationModesTest > /dev/null 2>&1; echo "UNPERTURBED EXIT=$?"
UNPERTURBED EXIT=0
```

Before the fix, the same sweep was `MALLOC_PERTURB_=1/5/17/85 -> EXIT=139` (the other three
happened not to fault; the entry that is dereferenced was a destroyed model whose freed
memory only faults when the allocator has poisoned it — see STACK-AND-REPRO-BEFORE.md).

## 2. The regression test this branch adds, on its own

```
$ ./AutomationModesTest testDestroyedControlIsNotDereferenced > /dev/null 2>&1; echo EXIT=$?
EXIT=0
```
It was `EXIT=139` before the fix, every run, without any perturbation (the storage it
destroys the control in is poisoned by the test itself). That is the property that makes
it able to go red again if the model-destruction hook is ever removed.

## 3. ctest, from build/tests (the CI's own command)

```
$ cd build/tests && ctest --output-on-failure > ../../build/ctest-final.log 2>&1; echo "CTEST EXIT=$?"
CTEST EXIT=0
100% tests passed, 0 tests failed out of 92
Total Test time (real) = 149.70 sec
```

## 4. `bash tests/run-all-gates.sh`

```
$ bash tests/run-all-gates.sh > build/gates-final.log 2>&1; echo "GATES EXIT=$?"
GATES EXIT=3

================ SUMMARY ================
gate   name                     result
1      ctest                    PASS
2      coverage                 SKIP
3      no-tautology             PASS
4      complexity               PASS
5      mutation                 PASS
6      upstream-regression      PASS
7      file-length              PASS
8      duplication              PASS
9      fork-sources             PASS
10     unregistered-tests       PASS

scope: fork (tests/fork-sources.txt) + tools — the enforced scope, the same one CI's
       static-gates job runs. The WHOLE-TREE scope (gates 4/7/8 --scope all) was NOT
       measured by this run; use --whole-tree for it (tests/QA-GATES.md, 'Scope policy').

skipped: 1 of 10 gates did not run
  gate 2 (coverage): --with-coverage was not passed — to run it: pass --with-coverage

RESULT: PASS-WITH-SKIPS (exit 3) — 1 of 10 gates did not run;
        this run is INCOMPLETE, not green. Run the gates listed above, then re-run.
GATES EXIT=3
```

Exit 3 is the documented PASS-WITH-SKIPS: every gate that ran passed, gate 2 (coverage)
was not requested. Gate 1 of that run is the same `ctest` as section 3.
