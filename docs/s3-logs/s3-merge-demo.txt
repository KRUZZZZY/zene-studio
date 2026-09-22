

################################################################
# 1. base: a fresh project, saved (no object has ever been revised)
################################################################

$ ZENE_S3_DEMO_SIDE=base ZENE_S3_DEMO_OUT=/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-s3/build/s3-rev-demo/base.mmp QT_QPA_PLATFORM=offscreen /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-s3/build/tests/ProjectRevIdsTest
    Totals: 10 passed, 0 failed, 0 skipped, 0 blacklisted, 1530ms
EXIT=0

$ grep -c  rev=" /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-s3/build/s3-rev-demo/base.mmp
4
EXIT=0
^ 0 revision attributes in the base, as an unrevised file must have


################################################################
# 2. ours: a THIRD process loads the base, edits track 1, saves
################################################################

$ ZENE_S3_DEMO_SIDE=ours ZENE_S3_DEMO_OUT=/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-s3/build/s3-rev-demo/ours.mmp QT_QPA_PLATFORM=offscreen /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-s3/build/tests/ProjectRevIdsTest
    Totals: 10 passed, 0 failed, 0 skipped, 0 blacklisted, 1562ms
EXIT=0


################################################################
# 3. theirs: a FOURTH process loads the same base, edits track 2, saves
################################################################

$ ZENE_S3_DEMO_SIDE=theirs ZENE_S3_DEMO_OUT=/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-s3/build/s3-rev-demo/theirs.mmp QT_QPA_PLATFORM=offscreen /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-s3/build/tests/ProjectRevIdsTest
    Totals: 10 passed, 0 failed, 0 skipped, 0 blacklisted, 1581ms
EXIT=0


################################################################
# 4. what the writer stated (head writer + the two revised tracks)
################################################################

$ grep -nE writer=|rev=" /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-s3/build/s3-rev-demo/ours.mmp
3:  <head mastervol="100" writer="23eae776-935" timesig_denominator="4" masterpitch="0" bpm="140" timesig_numerator="4"/>
6:      <track muted="0" id="1" rev="2" name="A-edited-by-ours" writer="23eae776-935" type="0" mutedBeforeSolo="0" solo="0">
19:        <midiclip muted="0" id="7" rev="1" pos="0" steps="16" name="" writer="292e39c1-131" type="0" len="192" autoresize="1" off="0"/>
20:        <midiclip muted="0" id="8" rev="1" pos="192" steps="16" name="" writer="292e39c1-131" type="0" len="192" autoresize="1" off="0"/>
22:      <track muted="0" id="2" rev="1" name="B" writer="292e39c1-131" type="0" mutedBeforeSolo="0" solo="0">
EXIT=0

$ grep -nE writer=|rev=" /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-s3/build/s3-rev-demo/theirs.mmp
3:  <head timesig_numerator="4" writer="cc65939e-63b" masterpitch="0" mastervol="100" timesig_denominator="4" bpm="140"/>
6:      <track muted="0" id="1" name="A" mutedBeforeSolo="0" writer="292e39c1-131" solo="0" type="0" rev="1">
19:        <midiclip autoresize="1" muted="0" id="7" name="" off="0" writer="292e39c1-131" steps="16" len="192" pos="0" type="0" rev="1"/>
20:        <midiclip autoresize="1" muted="0" id="8" name="" off="0" writer="292e39c1-131" steps="16" len="192" pos="192" type="0" rev="1"/>
22:      <track muted="0" id="2" name="B-edited-by-theirs" mutedBeforeSolo="0" writer="cc65939e-63b" solo="0" type="0" rev="2">
EXIT=0


################################################################
# 5. two branches in git, merged by mmpz-git's merge driver
################################################################

$ git init -q -b main /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-s3/build/s3-rev-demo/repo
EXIT=0

$ git -C /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-s3/build/s3-rev-demo/repo config user.email s3-demo@zene.invalid
EXIT=0

$ git -C /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-s3/build/s3-rev-demo/repo config user.name ARCH-4 S3 demo
EXIT=0

$ cp /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-s3/build/s3-rev-demo/base.mmp /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-s3/build/s3-rev-demo/repo/project.mmp
EXIT=0

$ git -C /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-s3/build/s3-rev-demo/repo add project.mmp
EXIT=0

$ git -C /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-s3/build/s3-rev-demo/repo commit -q -m base
EXIT=0

$ python3 /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-s3/tools/mmpz-git/mmpz_git.py install --repo /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-s3/build/s3-rev-demo/repo
git config filter.mmpz.clean = "/usr/bin/python3" "/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-s3/tools/mmpz-git/mmpz_git.py" dump
git config filter.mmpz.smudge = "/usr/bin/python3" "/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-s3/tools/mmpz-git/mmpz_git.py" compress
git config filter.mmpz.required = false
git config diff.mmpz.textconv = "/usr/bin/python3" "/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-s3/tools/mmpz-git/mmpz_git.py" textconv
git config merge.mmpz.driver = "/usr/bin/python3" "/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-s3/tools/mmpz-git/mmpz_git.py" merge %O %A %B %L %P
git config merge.mmpz.name = LMMS project 3-way merge
git config diff.mmpz.binary = false
EXIT=0

$ cp /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-s3/tools/mmpz-git/gitattributes.sample /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-s3/build/s3-rev-demo/repo/.gitattributes
EXIT=0

$ git -C /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-s3/build/s3-rev-demo/repo add .gitattributes
EXIT=0

$ git -C /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-s3/build/s3-rev-demo/repo commit -q -m let mmpz-git diff and 3-way merge project files
EXIT=0

$ git -C /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-s3/build/s3-rev-demo/repo checkout -q -b ours
EXIT=0

$ cp /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-s3/build/s3-rev-demo/ours.mmp /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-s3/build/s3-rev-demo/repo/project.mmp
EXIT=0

$ git -C /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-s3/build/s3-rev-demo/repo commit -qam ours: track edited by writer A
EXIT=0

$ git -C /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-s3/build/s3-rev-demo/repo checkout -q main
EXIT=0

$ git -C /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-s3/build/s3-rev-demo/repo checkout -q -b theirs
EXIT=0

$ cp /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-s3/build/s3-rev-demo/theirs.mmp /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-s3/build/s3-rev-demo/repo/project.mmp
EXIT=0

$ git -C /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-s3/build/s3-rev-demo/repo commit -qam theirs: another track edited by writer B
EXIT=0

$ git -C /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-s3/build/s3-rev-demo/repo checkout -q ours
EXIT=0

$ git -C repo merge --no-edit theirs   (rc 1 is the DOCUMENT-level conflict below)
mmpz-git: 1 conflict(s) to resolve in project.mmp

  [1] project root
      writer: base 292e39c1-131 -> ours 23eae776-935, theirs cc65939e-63b
      location: /head

Resolve by keeping the correct value in the project file
(ours is already in place) and deleting the marked CONFLICT
comment; `mmpz-git conflicts <file>` re-prints this report.
Auto-merging project.mmp
CONFLICT (content): Merge conflict in project.mmp
Automatic merge failed; fix conflicts and then commit the result.
EXIT=1


################################################################
# 5b. the one conflict the driver found - and why it is not an object
################################################################
Both writers necessarily stamped <head writer> (that is what the
document-level instance id MEANS), so the root attribute conflicts on
every two-sided merge. The driver's report says to keep ours - it is
already in place - and drop its comment; NO per-object rev/writer pair
conflicted, because each object was revised by exactly one side.

$ python3 /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-s3/tools/mmpz-git/demo_edits.py resolve /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-s3/build/s3-rev-demo/repo/project.mmp
EXIT=0

$ python3 /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-s3/tools/mmpz-git/mmpz_git.py verify /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-s3/build/s3-rev-demo/repo/project.mmp
SKIP /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-s3/build/s3-rev-demo/repo/project.mmp (not a .mmpz container)
EXIT=0

$ git -C /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-s3/build/s3-rev-demo/repo add project.mmp
EXIT=0

$ git -C /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-s3/build/s3-rev-demo/repo commit -q --no-edit
EXIT=0

$ git -C /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-s3/build/s3-rev-demo/repo log --oneline --graph --all
*   405f167 Merge branch 'theirs' into ours
|\  
| * 40e3d94 theirs: another track edited by writer B
* | 58e4bfb ours: track edited by writer A
|/  
* 46f2dd3 let mmpz-git diff and 3-way merge project files
* f3a9e40 base
EXIT=0


################################################################
# 6. rev_report.py names which side changed each object (stated rev/writer)
################################################################

$ python3 /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-s3/tools/mmpz-git/rev_report.py --base /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-s3/build/s3-rev-demo/base.mmp --ours /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-s3/build/s3-rev-demo/ours.mmp --theirs /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-s3/build/s3-rev-demo/theirs.mmp --ours-label ours --theirs-label theirs
track id=1               changed by ours (writer=23eae776-935, rev 1 -> 2)
track id=2               changed by theirs (writer=cc65939e-63b, rev 1 -> 2)
rev_report: 2 object(s) differ, 0 conflict(s)
EXIT=0


################################################################
# 7. assertions
################################################################
PASS: each side's writer bumped rev on exactly the track IT edited
      (only the document-level <head writer> conflicted, resolved per
      the driver's report), and rev_report attributed one change per
      side with zero conflicts.
mavis-trash: moved to trash: '/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-s3/build/s3-rev-demo'
