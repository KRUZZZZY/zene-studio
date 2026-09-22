
==== control: unmutated source must build and pass ====
pristine sha256: d3b1150da05effc23176b6bb79b2623871e5c540d1811852e6ba9cfe50d465d7
mavis-trash: moved to trash: 'build/src/CMakeFiles/lmmsobjs.dir/core/RoutingGraph.cpp.o'
control: pristine TU recompiled, test binary passed 3/3 runs

==== mutants: 30 selected of 193 candidates (seed 0) ====
  #   site                           rule           mutation                                   build    result
  --------------------------------------------------------------------------------------------------------------------
  1   src/core/RoutingGraph.cpp:135  cmp-eq         == -> !=                                   ok       KILLED
  2   src/core/RoutingGraph.cpp:196  cmp-ge         >= -> <=                                   ok       KILLED
  3   src/core/RoutingGraph.cpp:247  cmp-lt         < -> >                                     ok       KILLED
  4   src/core/RoutingGraph.cpp:128  cmp-ne         != -> ==                                   no-build INVALID
  5   src/core/RoutingGraph.cpp:87   const-neg-one  -1 -> 0                                    ok       KILLED
  6   src/core/RoutingGraph.cpp:44   const-one      1 -> 0                                     ok       SURVIVED
  7   src/core/RoutingGraph.cpp:243  const-zero     0 -> 1                                     ok       KILLED
  8   src/core/RoutingGraph.cpp:394  dec-inc        -- -> ++                                   ok       KILLED
  9   src/core/RoutingGraph.cpp:379  greater-less   std::greater<int> -> std::less<int>        ok       KILLED
  10  src/core/RoutingGraph.cpp:267  inc-dec        ++ -> --                                   ok       KILLED
  11  src/core/RoutingGraph.cpp:147  logic-and      && -> ||                                   no-build INVALID
  12  src/core/RoutingGraph.cpp:136  logic-or       || -> &&                                   ok       KILLED
  13  src/core/RoutingGraph.cpp:242  min-max        std::min -> std::max                       ok       KILLED
  14  src/core/RoutingGraph.cpp:330  neg-drop       if (! -> if (                              ok       KILLED
  15  src/core/RoutingGraph.cpp:317  ret-false      return false -> return true                ok       KILLED
  16  src/core/RoutingGraph.cpp:175  ret-true       return true -> return false                ok       KILLED
  17  src/core/RoutingGraph.cpp:405  stmt-delete    delete: m_readyList = m_plan;              ok       SURVIVED
  18  src/core/RoutingGraph.cpp:394  cmp-eq         == -> !=                                   ok       KILLED
  19  src/core/RoutingGraph.cpp:136  cmp-ge         >= -> <=                                   ok       KILLED
  20  src/core/RoutingGraph.cpp:88   cmp-lt         < -> >                                     ok       KILLED
  21  src/core/RoutingGraph.cpp:234  cmp-ne         != -> ==                                   no-build INVALID
  22  src/core/RoutingGraph.cpp:185  const-neg-one  -1 -> 0                                    ok       KILLED
  23  src/core/RoutingGraph.cpp:252  const-zero     0 -> 1                                     ok       SURVIVED
  24  src/core/RoutingGraph.cpp:247  inc-dec        ++ -> --                                   ok       KILLED
  25  src/core/RoutingGraph.cpp:148  logic-and      && -> ||                                   no-build INVALID
  26  src/core/RoutingGraph.cpp:116  logic-or       || -> &&                                   ok       KILLED
  27  src/core/RoutingGraph.cpp:241  min-max        std::min -> std::max                       ok       KILLED
  28  src/core/RoutingGraph.cpp:319  neg-drop       if (! -> if (                              ok       KILLED
  29  src/core/RoutingGraph.cpp:112  ret-false      return false -> return true                ok       KILLED
  30  src/core/RoutingGraph.cpp:406  ret-true       return true -> return false                ok       KILLED

==== final control: tree restored, pristine source still builds and passes ====
mavis-trash: moved to trash: 'build/src/CMakeFiles/lmmsobjs.dir/core/RoutingGraph.cpp.o'
final control: pristine source recompiled, test binary passed
note: uncommitted paths unrelated to the sweep (not caused by this run):
?? docs/mc-logs/mutation-gate-slice01.txt
?? docs/mc-logs/rt-safety-slice01.txt

==== result ====
mutation-gate: src/core/RoutingGraph.cpp
candidates generated : 193
mutants run          : 30 (seed 0; deterministic stratified sample)
valid mutants        : 26
  killed             : 23
  survived           : 3
invalid (no build)   : 4
kill score           : 23/26 = 88.5%  (threshold 80%)

mutant table (build ok => the mutated TU really was recompiled and relinked):
  #   site                           rule           mutation                                   build    result
  1   src/core/RoutingGraph.cpp:135  cmp-eq         == -> !=                                   ok       KILLED
  2   src/core/RoutingGraph.cpp:196  cmp-ge         >= -> <=                                   ok       KILLED
  3   src/core/RoutingGraph.cpp:247  cmp-lt         < -> >                                     ok       KILLED
  4   src/core/RoutingGraph.cpp:128  cmp-ne         != -> ==                                   no-build INVALID
  5   src/core/RoutingGraph.cpp:87   const-neg-one  -1 -> 0                                    ok       KILLED
  6   src/core/RoutingGraph.cpp:44   const-one      1 -> 0                                     ok       SURVIVED
  7   src/core/RoutingGraph.cpp:243  const-zero     0 -> 1                                     ok       KILLED
  8   src/core/RoutingGraph.cpp:394  dec-inc        -- -> ++                                   ok       KILLED
  9   src/core/RoutingGraph.cpp:379  greater-less   std::greater<int> -> std::less<int>        ok       KILLED
  10  src/core/RoutingGraph.cpp:267  inc-dec        ++ -> --                                   ok       KILLED
  11  src/core/RoutingGraph.cpp:147  logic-and      && -> ||                                   no-build INVALID
  12  src/core/RoutingGraph.cpp:136  logic-or       || -> &&                                   ok       KILLED
  13  src/core/RoutingGraph.cpp:242  min-max        std::min -> std::max                       ok       KILLED
  14  src/core/RoutingGraph.cpp:330  neg-drop       if (! -> if (                              ok       KILLED
  15  src/core/RoutingGraph.cpp:317  ret-false      return false -> return true                ok       KILLED
  16  src/core/RoutingGraph.cpp:175  ret-true       return true -> return false                ok       KILLED
  17  src/core/RoutingGraph.cpp:405  stmt-delete    delete: m_readyList = m_plan;              ok       SURVIVED
  18  src/core/RoutingGraph.cpp:394  cmp-eq         == -> !=                                   ok       KILLED
  19  src/core/RoutingGraph.cpp:136  cmp-ge         >= -> <=                                   ok       KILLED
  20  src/core/RoutingGraph.cpp:88   cmp-lt         < -> >                                     ok       KILLED
  21  src/core/RoutingGraph.cpp:234  cmp-ne         != -> ==                                   no-build INVALID
  22  src/core/RoutingGraph.cpp:185  const-neg-one  -1 -> 0                                    ok       KILLED
  23  src/core/RoutingGraph.cpp:252  const-zero     0 -> 1                                     ok       SURVIVED
  24  src/core/RoutingGraph.cpp:247  inc-dec        ++ -> --                                   ok       KILLED
  25  src/core/RoutingGraph.cpp:148  logic-and      && -> ||                                   no-build INVALID
  26  src/core/RoutingGraph.cpp:116  logic-or       || -> &&                                   ok       KILLED
  27  src/core/RoutingGraph.cpp:241  min-max        std::min -> std::max                       ok       KILLED
  28  src/core/RoutingGraph.cpp:319  neg-drop       if (! -> if (                              ok       KILLED
  29  src/core/RoutingGraph.cpp:112  ret-false      return false -> return true                ok       KILLED
  30  src/core/RoutingGraph.cpp:406  ret-true       return true -> return false                ok       KILLED

invalid mutants are excluded from the score; survivors are listed above and
named in tests/QA-GATES.md Gate 5. Logs: build/mutation-gate/logs/
PASS: kill score 88.5% >= 80%
mavis-trash: moved to trash: '/tmp/tmp.HF9yhuwFkm'
