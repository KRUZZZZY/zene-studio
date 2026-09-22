post-commit static gates on 40f468dc04e5dd4821baabc8417bc8c9c087e671 — re-run after the evidence logs were renamed to .md (Gate 6's docs class allows *.md only; the first post-commit run flagged the seven .txt logs as undeclared changed files and every other gate green)
G3_no-tautology EXIT=0
G4_complexity EXIT=0
G4_complexity_tools EXIT=0
G6_upstream-regression EXIT=1
G7_file-length EXIT=0
G7_file-length_tools EXIT=0
G8_duplication EXIT=0
G8_duplication_tools EXIT=0
G9_fork-sources EXIT=0
G10_unregistered-tests EXIT=0
G11_evidence_selftest EXIT=0
G11_evidence EXIT=0
G13_scripted_verify EXIT=0
G13_check-namespace EXIT=0
G13_check-strings EXIT=0
G14_release-fitness-selftest EXIT=0
G15_verification-debt-selftest EXIT=0

== after commit c35143079 (logs renamed .txt -> .md, docs reference fix) ==
G6_upstream-regression EXIT=0
G9_fork-sources EXIT=0
G11_evidence EXIT=0
git status --porcelain: empty (tree clean)
