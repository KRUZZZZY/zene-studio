ARCH-4 S0 gate — fixtures + byte-identity harness
tree   : /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-s7
binary : /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-s7/build-ci/zene

  corpus     PASS 38 .mmpz + 4 .mmp (want 38 + 4)
  container  PASS 38/38 .mmpz decompress->compress byte-identical
  verbatim   PASS 41/42 exact; 1 not-exact (all documented: ['tests/emptyproject.mmp'])
  cli-verify PASS `mmpz_git.py verify` over 38 .mmpz -> exit 0
  daw-raw    PASS 5 runs -> 5 distinct raw streams (raw byte-identity is NOT the oracle)
  daw-canon  PASS 5 runs -> 1 distinct canonical form(s) (canonical IS the oracle)
  daw-corpus PASS 38 upgraded twice; 0 canonical mismatch, 0 errors; 38/38 differ raw (the witness, corpus-wide)

RESULT: PASS
