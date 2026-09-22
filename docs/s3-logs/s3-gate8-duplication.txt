### gate 8 duplication --check (fork scope)
duplication-gate: scanning 689 fork sources (min-lines 25, min-tokens 120, threshold 5%)

PASS: duplicated lines 0.51% (budget 5%)
G8_FORK_EXIT=0

### gate 8 duplication --check --scope tools
duplication-gate: tools scope (fork-owned tooling; jscpd has no shell format, so the .sh entries are counted by Gates 4 and 7 only)
duplication-gate: scanning 48 tools sources (min-lines 25, min-tokens 120, threshold 5%)

PASS: duplicated lines 0.00% (budget 5%)
G8_TOOLS_EXIT=0
