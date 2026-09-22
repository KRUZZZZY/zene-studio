
FAIL: all-sources.txt does not match the recipe in its own header.
      '-' is an entry the recipe does not derive, '+' is a path the recipe derives and
      the manifest is missing. A missing fork-NEW path is a file the whole-tree scope
      measures nowhere while Gate 9 stays green:
      --- /tmp/tmp.M5BLsqkFkh/repro.487196.have	2026-09-22 17:26:01.255392921 +0100
      +++ /tmp/tmp.M5BLsqkFkh/repro.487196.expected	2026-09-22 17:26:01.248392908 +0100
      @@ -319,7 +319,6 @@
       include/ProjectRenderer.h
       include/ProjectRevisions.h
       include/ProjectVersion.h
      -include/ProvenanceSection.h
       include/QuadratureLfo.h
       include/Rack.h
       include/RackMacros.h
      @@ -1250,7 +1249,6 @@
       src/core/ProjectRenderer.cpp
       src/core/ProjectRevisions.cpp
       src/core/ProjectVersion.cpp
      -src/core/ProvenanceSection.cpp
       src/core/Rack.cpp
       src/core/RackMacros.cpp
       src/core/RackNodes.cpp
      @@ -1634,7 +1632,6 @@
       tests/src/core/ProjectRecoveryTest.cpp
       tests/src/core/ProjectRevIdsTest.cpp
       tests/src/core/ProjectVersionTest.cpp
      -tests/src/core/ProvenanceSectionTest.cpp
       tests/src/core/RackMacrosTest.cpp
       tests/src/core/RackTest.cpp
       tests/src/core/RackTestSupport.h

      Fix by re-deriving the list: run the 'Regenerate with' command from the manifest's
      header and write its output over the entry list (never hand-edit, never delete a
      path the recipe derives).
mavis-trash: moved to trash: '/tmp/tmp.M5BLsqkFkh'

FAIL: a scope manifest does not reproduce from its own header recipe (exit 1).
      Registration and derivation are two different questions; this gate now asks both.
