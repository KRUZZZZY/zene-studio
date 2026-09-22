=== self-test 1: a clean tree (+ a .wav under data/) -> expect 0 ===
  PASS clean tree                                                 exit 0
=== self-test 2: a committed run log -> expect 1 ===
  PASS a .log is refused                                          exit 1
=== self-test 3: a 2 MB file against a 1 MiB cap -> expect 1 ===
  PASS an over-cap file is refused                                exit 1
  PASS the over-cap refusal names the file                        names blob.bin
=== self-test 4: a 1 KB archive, under the cap -> expect 1 ===
  PASS an archive is refused                                      exit 1
  PASS the archive refusal names the file                         names ctest-logs.zip
=== self-test 5: a 4-byte object file -> expect 1 ===
  PASS a compiled artefact is refused                             exit 1
  PASS the compiled-artefact refusal names the file               names RoutingGraph.o
=== self-test 6: a render outside data/ -> expect 1 ===
  PASS a stray render is refused                                  exit 1
=== self-test 7: the same 2 MB file under an exempted prefix -> expect 0 ===
  PASS an exempted prefix passes                                  exit 0
=== self-test 8: an exemption with a blank reason -> expect 2 ===
  PASS a blank reason is a setup error                            exit 2
=== self-test 9: a TRACKED 2 MB file, scanned through git ls-files -> expect 1 ===
  PASS a tracked over-cap file is refused                         exit 1
  PASS the git-index refusal names the file                       names build/tracked-dump.bin
mavis-trash: moved to trash: '/tmp/tmp.aGA5cTd2qW'

RESULT: PASS - the gate goes red on a log, an over-cap file, an archive and a
        compiled artefact, naming each refused path; goes green on a clean tree
        and on an exempted path; refuses a blank reason; and refuses a TRACKED
        over-cap file through the git index (git absent would fail case 9, not
        pass it: the case asserts the NAME, not only the exit code).
