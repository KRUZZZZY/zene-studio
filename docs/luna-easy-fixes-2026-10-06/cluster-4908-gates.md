# Cluster 4908 required ratchets

Both checks ran after the code and regression changes, before the fix commit.

Working directory: repository root.

```sh
bash tests/complexity-gate.sh --check
```

```text
#TOTAL 14053 66
complexity-gate: 14053 functions in the fork scope; 66 exceed CCN 10
PASS (check mode: no regressions; baseline not written)
EXIT=0
```

```sh
bash tests/file-length-gate.sh --check
```

```text
file-length-gate: 1512 fork-scope sources measured; 5 exceed 500 code lines
PASS (check mode: no regressions; baseline not written)
EXIT=0
```
