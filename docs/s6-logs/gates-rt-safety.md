rt-safety whole-tree sweep
  tree      : /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-s6
  scope     : tests/rt-safety-scope.txt
  allowlist : tests/rt-safety-allowlist.txt
  declared  : 31 path:symbol pair(s)
  resolved  : 4 in-class form, 0 whole-file
  measured  : 1044 region line(s); 5 hit(s) in 4 key(s)
  by rule   : allocation 0, locking 3, growth 2
  allowlist : 4 line(s); 4 key(s) allowlisted, 0 key(s) fresh

BOUND of this programme (what it does NOT cover):
  * STATIC ONLY: source text is read; the engine is not run. The runtime half of the rule remains the AllocationProbe tests (tests/src/core/AllocationProbe.h).
  * DECLARED SCOPE, NOT A CALL-GRAPH: only the path:symbol pairs in the scope file are measured. A new audio-thread path that nobody declares is measured by nothing here.
  * NO VIRTUAL DISPATCH / FUNCTION POINTERS / MACROS: an implementation reached through an interface, a macro or an include is checked only if the scope names it too.
  * NO COST, NO SYSCALL AND NO I/O CHECK: only allocation, locking and container growth, the three words of the AGENTS.md rule.
  * NO ALIASING ANALYSIS: a hit is a mention of a construct, not proof that it runs on the audio thread. An allowlist line with a reason is how a tolerated mention is said.

PASS: every hit on a declared audio-thread path is allowlisted with a reason and at its allowed count.
