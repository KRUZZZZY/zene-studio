# TrackFolder — the container that owns child tracks and collapses in the track list

**Status: design only. No C++ was written for this document and no build was run.**
Items 3, 20 and 21 of the register are one item (owner decision 2026-09-12, `BACKLOG.md` item 3 /
item 20 / item 21 and `NEXT-RELEASE-HANDOFF.md` §6). This document grounds that item in the tree,
counts what a folder would cost, and answers the two questions the register leaves open — the
routing half's latency compensation, and whether pinning needs anything the organisational half does
not already give.

Branch `next/trackfolder`, based on the release-line tip `3fd5a4f3c`. Every file:line below was read
at that commit; every count is the output of the command printed beside it. Nothing here was
measured by running code, because the box takes one build at a time and another session has one in
flight — §9 lists everything that therefore stays **UNVERIFIED**.

```
Branch:  next/trackfolder
Base:    3fd5a4f3c  "fix(gates): the manifest entry and the report citation, which the earlier
                     commit left behind"
```

---

## 1. What a track container is today, and how a track is owned

### 1.1 The container

`include/TrackContainer.h` is 124 lines. The entity is

```cpp
class LMMS_EXPORT TrackContainer : public Model, public JournallingObject   // :48
{
    using TrackList = std::vector<Track*>;                                   // :52
    TrackList m_tracks;                                                      // :112 (private)
    mutable QReadWriteLock m_tracksMutex;                                    // :109 (protected)
    friend class gui::TrackContainerView;                                    // :117
    friend class Track;                                                      // :118
```

`TrackList` is a **flat `std::vector<Track*>` of raw pointers**. There is no parent, no depth, no
child list, and no second axis. The public surface over it is `countTracks()` (:66), `addTrack()`
(:69), `removeTrack()` (:70), `moveTrack()` (:71), `clearAllTracks()` (:75) and `tracks()` (:77).

**Subclasses, counted:**

```
$ git grep -n "public TrackContainer\b" -- 'include/**' 'src/**' 'plugins/**' 'tests/src/**' 'tools/**'
include/PatternStore.h:64:class LMMS_EXPORT PatternStore : public TrackContainer
include/Song.h:71:class LMMS_EXPORT Song : public TrackContainer
src/core/PresetPreviewPlayHandle.cpp:42:class PreviewTrackContainer : public TrackContainer
$ git grep -n "public TrackContainer\b" ... | wc -l
3
```

Three, and only one of them is a user-visible container: `Song` (the root of the model tree), with
`PatternStore` as the beat/bassline container and `PreviewTrackContainer` a file-local stub for
preset auditioning. So "the song container holds tracks" means **`Song` is itself the flat list** —
there is no indirection between the song and its tracks, which is why the track list's order and the
project file's document order are the same order:

* `TrackContainer::saveSettings` walks `m_tracks` in vector order and calls `track->saveState(...)`
  per track (`src/core/TrackContainer.cpp:74-80`).
* `TrackContainer::loadSettings` walks the element's child nodes in **document order** and calls
  `Track::create(node.toElement(), this)` for every non-`metadata` element child
  (`src/core/TrackContainer.cpp:135-147`); `Track::create` appends through the ctor
  (`src/core/Track.cpp:90`, `m_trackContainer->addTrack(this)`), and `addTrack` is a `push_back`
  (`src/core/TrackContainer.cpp:182-193`).

### 1.2 Ownership, precisely — because this is where a folder can double-free

The chain, every link read in the tree:

| step | what happens | where |
|---|---|---|
| creation | `Track::create(Type, TrackContainer*)` `new`s the concrete track; the ctor pushes **itself** into the container | `src/core/Track.cpp:131-160`, `:90` |
| container owns | the raw pointer sits in `TrackContainer::m_tracks`; the container never shares it | `include/TrackContainer.h:112` |
| **removal does not delete** | `removeTrack()` erases the pointer from the vector, calls `Song::setModified()` and emits `trackRemoved()` — **it does not `delete`** | `src/core/TrackContainer.cpp:198-220` |
| destruction | `Track::~Track()` deletes its own clips, then calls `m_trackContainer->removeTrack(this)` | `src/core/Track.cpp:109-121` (`:119`) |
| container teardown | `~TrackContainer()` calls `clearAllTracks()`, which loops `delete m_tracks.front()`; each `~Track` erases itself, which is what terminates the loop | `src/core/TrackContainer.cpp:60-63`, `:239-247` |

**The rule that falls out of this: exactly one owner, and it is the `TrackContainer`.** A
`TrackFolder` that deleted its own children would double-free every one of them — once from the
folder and once from `~TrackContainer`/`clearAllTracks`, in an order neither could see. A folder
therefore holds **references**, never ownership, and it never `delete`s a child.

Two hazards in that path are already real and are worth naming because the folder work touches
exactly these functions:

1. **`~Track` emits `destroyedTrack()` before it has unlinked itself.**
   `src/core/Track.cpp:112` emits, `:119` calls `removeTrack`. Any GUI slot on `destroyedTrack()`
   runs while the container still contains the pointer and the object is already a destructing
   `Track`. This is the exact mechanism of the 0.2.0 line's use-after-free fix: `~PatternTrack`
   erased its entry in `PatternTrack`'s static registry, `~Track` then emitted `destroyedTrack()`,
   and the slot (`PatternTrackView::close()`) read that registry with `QMap::operator[]`, *inserting*
   a zero for the dead key. The allocator handed the dead address to the replacement track, the
   replacement derived its pattern number from `s_infoMap.size()`, and `PatternStore::updateComboBox`
   dereferenced `findPatternTrack(0) == nullptr` — SIGSEGV on the UI thread
   (commit `f52b2d924`, `docs/CONTROL-UNDO-CONNECTION-DROP.md`). That fix **is** an ancestor of this
   branch (`git merge-base --is-ancestor f52b2d924 HEAD` → yes). A folder's child list is the same
   shape of hazard: **a child must unlink itself from its folder *before* `emit destroyedTrack()`,
   and a folder must never be reachable from a slot that runs while a child is half-destroyed.**
2. **`moveTrack` is unchecked and unlocked.**
   `m_tracks.erase(std::find(m_tracks.begin(), m_tracks.end(), track))`
   (`src/core/TrackContainer.cpp:224`) — no mutex, and if the track is not in the list the `erase`
   is on `end()`: UB. The folder's drag-reparenting path goes through here
   (`TrackContainerView::moveTrackView` → `m_tc->moveTrack(...)`, `src/gui/editors/TrackContainerView.cpp:202`).
   `clearAllTracks` likewise has its `lockForWrite` commented out (`:241`, `:246`). This document
   does not fix either; it records that the folder work lands on them.

### 1.3 What the pattern container already taught us about nesting

A `PatternStore` track is a `PatternTrack` whose element *contains* a nested `<trackcontainer>`
(Song.cpp's load counts those separately, `src/core/Song.cpp:1236-1240`), and the agent surface
already documents that this nesting is not addressable:
`track.list`'s description says *"Addressing is scoped to the SONG container: a track inside a nested
container (the `<trackcontainer>` a pattern track carries) is not reachable by id, exactly as it is
not addressable by index"* (`src/core/ControlCommandsTransport.cpp:234-237`, repeated at
`ControlCommandsArrangementState.cpp:49-53`). So nesting exists in the model and is **deliberately
out of the addressing model**. A folder must decide which side of that line it sits on; §8 takes the
position that a folder is addressable and its children stay flat-indexed.

---

## 2. The track-list surface: what "collapses in the track list" must change

**The real file name first.** `src/gui/TrackContainerView.cpp` **does not exist**:

```
$ git ls-files | grep -i TrackContainerView
include/TrackContainerView.h
src/gui/editors/TrackContainerView.cpp
```

**There is no separate list widget.** The track list is not a `QListView`, not a model/view pair,
and not a second widget: it is the left column of the same row widgets that carry the clips. The
list is

* `TrackContainerView` (`include/TrackContainerView.h:68`) — a `QWidget` that owns a private
  `scrollArea` whose contents widget has **one** `QVBoxLayout`:

  ```cpp
  auto scrollContent = new QWidget;                                       // TrackContainerView.cpp:102
  m_scrollLayout = new QVBoxLayout( scrollContent );                      // :103
  m_scrollLayout->setSizeConstraint( QLayout::SetMinAndMaxSize );          // :106
  m_scrollArea->setWidget( scrollContent );                               // :108
  ```

  (this is the surface the register cites as `src/gui/editors/TrackContainerView.cpp:103-106`,
  `BACKLOG.md` item 3 — the line numbers are right; a reader who assumes the file lives at
  `src/gui/TrackContainerView.cpp` will not find it);

* `m_trackViews`, a flat `QList<TrackView*>` (`include/TrackContainerView.h:191-192`), appended by
  `addTrackView()` (`TrackContainerView.cpp:156-164`) and laid out by `realignTracks()`
  (`:258-267`), which today does exactly:

  ```cpp
  for (const auto& trackView : m_trackViews) { trackView->show(); trackView->update(); }
  emit tracksRealigned();
  ```

* one row per track is a `TrackView` (`include/TrackView.h:59`) holding `m_trackOperationsWidget`,
  `m_trackSettingsWidget` (the label column) and `m_trackContentWidget` (the clip area)
  (`include/TrackView.h:155-157`) — so the "track list" and the "arrangement" are the same rows.

`TrackContainerView` has exactly two subclasses, so a change here lands in both editors:

```
$ git grep -n "public TrackContainerView"
include/PatternEditor.h:43:class PatternEditor : public TrackContainerView
include/SongEditor.h:56:class SongEditor : public TrackContainerView
```

**What a collapsible folder must change, function by function:**

| function | today | must become |
|---|---|---|
| `TrackContainerView::realignTracks()` (`:258-267`) | shows **every** `TrackView` unconditionally | show a row iff no ancestor folder is collapsed — i.e. `setVisible(!isInsideCollapsedFolder(view->getTrack()))` |
| `TrackContainerView::totalHeightOfTracks()` (`:484-492`) | sums `scaledPixels(trackView->getTrack()->getHeight())` over all views | skip hidden rows; it feeds `SongEditor::m_positionLine->setFixedHeight(totalHeightOfTracks())` (`src/gui/editors/SongEditor.cpp:811`) |
| `TrackContainerView::trackViewAt(const int _y)` (`:304-319`) | accumulates each view's `height()` and returns the view under `_y` | skip hidden rows, or every click and drop below a collapsed folder lands on the wrong track |
| `TrackContainerView::moveTrackView()` (`:190-210`) | `m_trackViews.indexOf` + `m_scrollLayout->insertWidget(indexTo, …)` | a visible-row index and a model-index are no longer the same number; the two indices must be named and kept apart |
| `SongEditor::trackIndexFromSelectionPoint(int yPos)` (`src/gui/editors/SongEditor.cpp:892`) | converts a pixel `y` to a flat track index for rubber-band selection | same skip-hidden rule |
| `TrackView` / `TrackOperationsWidget` | no collapse affordance exists | one chevron (or menu action) that toggles the folder's collapsed model **and declares a registered command** (§8) |

Two more consumers of the same flat view list that are *not* in `TrackContainerView` and must not be
forgotten, because they address a row by index:

* `SongEditor.cpp:423` — `trackViews().indexOf(clip->getTrackView())`;
* `InstrumentTrackWindow.cpp:643` — `trackViews.indexOf(m_itv)`;
* `TrackOperationsWidget.cpp:182` — `tcView->trackViews().indexOf( m_trackView )`.

**The honest size of this section:** the view-side flat list is `m_trackViews`, 20 occurrences in
`src/gui/editors/TrackContainerView.cpp` and 2 in its header
(`git grep -c "m_trackViews" -- include src/core src/gui src/tracks`). The layout itself is **one
`QVBoxLayout`** — which is why the organisational half is cheap: collapsing a row in a `QVBoxLayout`
with `SetMinAndMaxSize` is `setVisible(false)` plus the three height/at-index functions above,
not a new layout and not a new widget. **That is the whole of the view change.** Everything larger —
a nested scroll region, a tree view, per-folder colour headers — is not needed for "collapses in the
track list" and is not designed here.

---

## 3. The size estimate, counted

This is the section that decides days vs weeks. Everything below is first-party code only;
vendored trees and the frozen integration-log fixtures are excluded by the pathspec
(`':!tests/reference/*' ':!src/3rdparty/*'`, and the log trees are not in the searched roots).

### 3.1 The flat-list surface

```
$ R="include src plugins tests/src tools :(exclude)tests/reference/* :(exclude)src/3rdparty/*"
$ git grep -nE '\btracks\(\)|\bm_tracks\b|\btrackViews\(\)|\bm_trackViews\b' -- $R | wc -l
154
$ git grep -lE '\btracks\(\)|\bm_tracks\b|\btrackViews\(\)|\bm_trackViews\b' -- $R | wc -l
45
```

| pattern | occurrences | files |
|---|---|---|
| `tracks()` — `TrackContainer::tracks()`, the container's flat list | **97** | 38 |
| `m_tracks` — the private vector, reached by friends | **24** | 4 |
| `trackViews()` / `m_trackViews` — the view-side flat list | **33** (`trackViews()` 11 + `m_trackViews` 22) | 7 |
| **total (the command above)** | **154** | **45** |

Each row is its own pattern with the same pathspec and the word boundaries kept — run on its own with
`-- $R` and `| wc -l`, the three give **97** in 38 files (`\btracks\(\)`), **24** in 4 files
(`\bm_tracks\b`: `include/MultiTrackRecorder.h`, `include/TrackContainer.h`,
`src/core/TrackContainer.cpp`, `src/core/audio/MultiTrackRecorder.cpp`) and **33** in 7 files
(`\btrackViews\(\)|\bm_trackViews\b`). **97 + 24 + 33 = 154**, the headline. A method note, since the
same surface can be counted two ways: dropping the word boundary from the middle row gives **51 lines
in 8 files**, of which `m_tracksMutex` (17 lines), `m_tracksToRender` (8) and `song_num_tracks` (2) are
27 lines that are not the private vector.

Per file, the top of the list:

```
$ git grep -cE '\btracks\(\)|\bm_tracks\b|\btrackViews\(\)|\bm_trackViews\b' -- $R | sort -t: -k2 -rn | head
src/gui/editors/TrackContainerView.cpp:20
src/core/TrackContainer.cpp:15
src/core/Song.cpp:9
src/core/ControlCommandsArrangement.cpp:9
tests/src/core/StemSplitPipelineTest.cpp:6
tests/src/core/ScriptEngineTest.cpp:6
src/gui/clips/ClipView.cpp:6
src/core/PatternStore.cpp:6
src/core/Mixer.cpp:6
```

### 3.2 The subset that actually has to change: position addressing

A flat list that *iterates* does not care about nesting: a folder is one more row, `Track::play()`
returns false for it, and `updateLength()` (`Song.cpp:663-685`), `insertBar()`/`removeBar()`
(`:861-886`) and the automation walk (`:404-439`) keep working unchanged. What breaks is code that
addresses a track **by position**:

```
$ git grep -nE 'trackIndex|trackViews\(\)\.indexOf|m_trackViews\.indexOf|tracks\(\)\[|tracks\(\)\.begin\(\)|trackIndexFromSelectionPoint' -- include src
52 occurrences in 16 files
```

| file | count | what it is |
|---|---|---|
| `include/ScriptBindings.h` | 7 | `LuaPatternClip`'s `trackIndex` (pattern container) |
| `src/core/ScriptBindings.cpp` | 9 | the Lua `trackIndex` surface |
| `src/gui/clips/ClipView.cpp` | 5 | **writes** a `trackIndex` attribute into a dragged clip's XML (`:540-542`, `:553-560`) |
| `src/core/ScriptEngine.cpp` | 5 | `trackAt(int)` → `tracks[trackIndex]` (`:804-811`) |
| `src/gui/editors/TrackContainerView.cpp` | 4 | `m_trackViews.indexOf(...)` (`:171`, `:196`, `:217`, `:227`) |
| `src/gui/editors/SongEditor.cpp` | 4 | `trackIndexFromSelectionPoint`, `trackViews().indexOf` |
| `src/gui/tracks/TrackContentWidget.cpp` | 4 | **reads** the `trackIndex` attribute back and does index arithmetic on drop (`:389-390`, `:505-506`) |
| `src/core/Song.cpp` | 3 | the session-view track index (`:382-384`) |
| `src/core/ControlCommandsArrangement.cpp` | 3 | `trackIndexInContainer()` (`:72`), `tracks()[i]` (`:130`), the snapshot index (`:199`) |
| `src/core/ControlEditSupport.cpp` | 1 | `ref.trackIndex = t` (`:115`) — one resolver for every `trk-<n>` consumer |
| `include/ControlEdit.h` | 1 | `ClipRef::trackIndex` (`:84`, "index of the owning track in the song") |
| `src/core/ControlCommandsProjectFiles.cpp` | 1 | `tracks()[i]` (`:247`) |
| `src/core/ControlCommandsArrangementState.cpp` | 1 | `ref.trackIndex == i` (`:74`) |
| `src/gui/tracks/TrackOperationsWidget.cpp` | 1 | `trackViews().indexOf` (`:182`) |
| `include/ScriptEngine.h` | 2 | `trackAt(int)`, `patternClipAt(int,int)` |
| `include/SongEditor.h` | 1 | `trackIndexFromSelectionPoint(int)` |

**Reading of that table.** The load-bearing entries are the ones on the *addressing* path — the
`trk-<n>` resolver (`control::resolveTrack`, `ControlEditSupport.cpp`), the `trackIndex` written by
`ClipView` and re-read by `TrackContentWidget` on drop, the rubber band's
`trackIndexFromSelectionPoint`, and the four `indexOf` lookups in the view — roughly **ten call
sites**, not fifty-two. The rest are the pattern container's, which a song-side folder does not
touch, or tests.

The drag-and-drop pair deserves its own line, because it is the one that can silently corrupt a
project rather than crash: `ClipView` writes `trackIndex` into the clip's XML as a distance into
`tracks()` (`ClipView.cpp:538-542`), and `TrackContentWidget` recomputes it on drop
(`TrackContentWidget.cpp:389-390`). With a collapsed folder, "the row under the cursor" and "the
index in `tracks()`" are different numbers, and this pair is where that difference becomes data.

### 3.3 The enumerator surface: `Track::Type`

A folder needs a `Track::Type` sentinel, and every switch on that enum is an exhaustive switch that
will not warn (`Count` is a `break`, not an `abort`):

```
$ git grep -c "case Track::Type::" -- include src/core src/gui src/tracks plugins/MidiImport plugins/HydrogenImport tests/src
src/core/ScriptBindings.cpp:8
src/core/ControlEditSupport.cpp:8
src/core/ControlDeviceSupport.cpp:8
src/core/ControlCommandsTransport.cpp:8
src/core/ControlCommandsAutomation.cpp:8
src/core/TrackContainer.cpp:3
$ git grep -n "case Track::Type::" ... | wc -l
43        over 6 files
```

Plus `Track::create`'s own factory switch, which spells the arms as `Type::Instrument` — seven
`case Type::` lines, two of them (`Event`, `Video`) commented out
(`src/core/Track.cpp:137-148`) — and 136 total `Track::Type::` references. **43 switch arms across 6
files** is the smallest honest count of "places that must learn about a folder type"; five of the six
are 8-arm switches that map a type to a string (`src/core/ControlEditSupport.cpp:62-76` is the
`trackTypeNameOf` one) and will silently return `"unknown"` if the new arm is missed.

**Where the enumerator goes matters and is checkable.** `type` is persisted as its integer
(`element.setAttribute("type", static_cast<int>(type()))`, `src/core/Track.cpp:224`) and read back
the same way (`Track.cpp:175`, `Song.cpp:1236`), so the new enumerator must go **immediately before
`Count`**, leaving `Instrument…HiddenAutomation` at 0…6 unchanged; only `Count`'s own value moves,
and `Count` is only used in-process (as `countTracks`'s "all types" default,
`include/TrackContainer.h:66`, checked at `TrackContainer.cpp:170`).

### 3.4 The mixer's per-track channels

A folder's routing mode rewires the per-track channel binding, so that surface is counted too:

```
$ git grep -c "mixerChannelModel" -- include src plugins tests/src   # 46 occurrences, 10 files
src/core/Mixer.cpp:14
src/tracks/InstrumentTrack.cpp:9
src/tracks/SampleTrack.cpp:7
src/gui/tracks/SampleTrackView.cpp:3
src/gui/tracks/InstrumentTrackView.cpp:3
include/SampleTrack.h:3
include/InstrumentTrack.h:3
src/gui/MixerView.cpp:2
src/gui/SampleTrackWindow.cpp:1
src/gui/instrument/InstrumentTrackWindow.cpp:1
```

Every one of them belongs to `InstrumentTrack`/`SampleTrack` — the only two types that *have* a
channel — and none of them is a folder, so the routing half changes **the binding**, not those call
sites. See §5.

### 3.5 The rest of the countable surface

| surface | count | command (add `-- $R` from §3.1) |
|---|---|---|
| `Track::create` call sites | 59 | `git grep -n "Track::create" -- include src plugins tests/src` |
| `countTracks` | 5 | `git grep -n "countTracks" -- include src plugins tests/src` |
| `saveTrackSpecificSettings` / `loadTrackSpecificSettings` (declarations, calls, implementations) | 12 / 12 | `git grep -n "saveTrackSpecificSettings" -- include src plugins tests/src` |
| project-upgrade methods | **31** | `sed -n '74,92p' src/core/DataFile.cpp \| grep -o '&DataFile::[A-Za-z0-9_]*' \| wc -l` |
| registered agent commands | **79** | `git grep -oh 'cmd.id = QStringLiteral("[a-z_]*\.[a-z_]*")' -- include src plugins tests/src \| wc -l` |
| of which `track.*` | **8** | `git grep -n 'cmd.id = QStringLiteral("track\.' -- include src plugins tests/src` |
| GUI track-list surface (lines, 10 files) | **4306** | `wc -l` over `TrackContainerView.{h,cpp}`, `TrackView.{h,cpp}`, `TrackContentWidget.{h,cpp}`, `TrackOperationsWidget.{h,cpp}`, `SongEditor.cpp`, `PatternEditor.cpp` |
| fork-NEW sources the manifests must gain (Gate 9) | 242 today | `grep -cvE '^\s*#\|^\s*$' tests/fork-sources.txt` |

### 3.6 What the numbers say

154 flat-list accesses in 45 files is the number that looks like "weeks". It is not the number that
decides the band. The number that decides it is **52 position-addressing sites in 16 files, of which
about ten are load-bearing**, **43 switch arms in 6 files**, and **one `QVBoxLayout`**. If a folder
is a `Track` in the existing flat list (§4 and §7 argue it must be), then iteration keeps working
untouched, the file's document order stays the track order, and the position-addressing core is a
day's work per file — which is why §9's band is **days–weeks** for the organisational half, with the
register's **weeks** as the upper end rather than the floor.

---

## 4. Serialisation: how tracks save and load, and which upgrade paths must learn about a folder

### 4.1 The track save/load path today

`Track::saveSettings` / `loadSettings` are one-line delegations to `saveTrack` / `loadTrack`
(`src/core/Track.cpp:378-388`), both taking the track's element:

* `saveTrack(doc, element, presetMode)` (`src/core/Track.cpp:218-264`) sets `element.setTagName("track")`
  and the attributes `type`, `name`, `id`, `muted`, `solo`, `mutedBeforeSolo`, optional `trackheight`
  and `color`; appends **one** child element named `nodeName()` — the track-specific element
  (`<instrumenttrack>`, `<sampletrack>`, `<patterntrack>`, `<automationtrack>`) — and calls
  `saveTrackSpecificSettings` into it; then saves each clip as a further child of the same `<track>`
  element.
* `loadTrack(element, presetMode)` (`src/core/Track.cpp:279-366`) reads those attributes and walks the
  children: a child named `nodeName()` goes to `loadTrackSpecificSettings`; `muted`/`solo`/`metadata`
  are skipped; **every other element child becomes a real `Clip`** (`:349-356`).

There is a comment in the tree that is a hard constraint on this design, and it is worth quoting
because it is exactly the trap a folder's serialisation could fall into
(`src/core/Track.cpp:228-233`):

> SPEC-stable-ids.md 3.1: an ATTRIBUTE on the track's own element, never a child element —
> `Track::loadTrack` turns an unrecognised child element into a real Clip, so an id element would
> make every track grow a phantom clip on load, on every track, in every project.

**So: the folder relation is an attribute on the `<track>` element, and the folder's own state is a
`<trackfolder>` child element that `TrackFolder::nodeName()` returns** — the same shape as
`<instrumenttrack>`, and the only shape `Track::loadTrack` routes to the specific loader.

### 4.2 The project file schema, and the one thing that must not be nested

The container element is `<trackcontainer>` (`TrackContainer::classNodeName()`,
`include/TrackContainer.h:84-87`), it carries `type` from the virtual `nodeName()` — overridden by
`Song` (`include/Song.h:349`, returns `"song"`) and by `PatternStore` (`include/PatternStore.h:76`)
— and it holds its tracks as **direct children** (`src/core/TrackContainer.cpp:74-80` writes them;
`:135-147` reads them).

Two flat walks depend on that and must stay flat:

1. `TrackContainer::loadSettings` (`src/core/TrackContainer.cpp:135-147`) treats **any** non-metadata
   element child as a track: `Track::create(node.toElement(), this)`. A `<trackfolder>` element as a
   *sibling* of the `<track>` elements would be constructed as a track of an unrecognised type.
2. `Song::loadProject` counts `<track>` children of every `<trackcontainer>` to size the load
   progress dialog (`src/core/Song.cpp:1226-1244`, `m_nLoadingTrack`), and restores containers with
   `restoreState` on each `<trackcontainer>` node (`:1250-1253`).

Both hold automatically if the folder is a `<track>` with a `type` of its own and the relation is an
attribute. A nested `<trackcontainer>` inside a folder would put a second nesting axis next to the
pattern container's, and would break (2)'s count — this document does not propose it.

### 4.3 Two load-order requirements, both with a precedent in the tree

1. **The parent link must be resolved after the walk.** A folder created *after* the tracks it
   contains sits **after** them in `m_tracks` (the ctor pushes back, `TrackContainer.cpp:188`) and
   therefore after them in the file; a child that names its parent by id will name an id that has not
   been constructed yet when its own element is read. The tree already has the pattern for this — a
   link that is serialised by id and finished once every object exists:
   `ControllerConnection::finalizeConnections()` (`src/core/Song.cpp:1356`) and
   `AutomationClip::resolveAllIDs()` (`:1364`), both called after the walk, before
   `doneChangeInModel()`. A folder needs the same: one `Song::resolveTrackFolders()` beside them.
2. **The id pass is the place a dangling parent is repaired or reported.** `Song.cpp:1330-1352`
   already walks `tracks()` after a load to detect two live tracks sharing an id and re-assign from
   the high-water mark, counting each repair through `ProjectIds::noteLoadAssignment()` so it shows
   up in `ids_assigned` instead of hiding. A `folder` attribute that names no live track (a deleted
   folder, a hand-edited file, a merged `.mmpz`) is the same class of defect and belongs in the same
   pass, with the same counting discipline — a child whose folder is gone is re-parented to the
   container root and the count reported.

### 4.4 The version / upgrade path — every one that must learn about a child-owning folder

`DataFile` versions the file as `m_fileVersion = UPGRADE_METHODS.size()`
(`src/core/DataFile.cpp:134`, re-stamped at `:2206`), and `DataFile::upgrade()` runs
`UPGRADE_METHODS.begin() + max … end` where `max = min(m_fileVersion, UPGRADE_METHODS.size())`
(`:2197-2203`). **31** methods exist today (command in §3.5). The relevant ones:

| upgrade | where | why a folder touches it | verdict |
|---|---|---|---|
| `upgrade_noHiddenAutomationTracks` | `DataFile.cpp:1744-1791` | It finds `song.firstChildElement("trackcontainer")`, builds `<track>` elements and `insertBefore(track, trackContainer.firstChild())` — i.e. **it rewrites the container's child order**, and it assumes every non-`track` child is ignorable. Because parenting is by attribute (an id), a reorder is harmless — but it is the one upgrade that moves a child ahead of its folder, so it must be *proved*, not assumed. | **must be checked**; no change expected |
| `upgrade_noHiddenClipNames` | `DataFile.cpp:1793-1820` | `elementsByTagName("track")` then looks for clip children (`pattern`, `automationpattern`, `bbtco`). A folder is a `<track>` with no clips; the walk must not assume a `<patterntrack>` or a clip child exists. | **must be checked** |
| `upgrade_mixerRename` | `DataFile.cpp:1942-2001` | Rewrites `fxch` → `mixch` on `<instrumenttrack>` and `<sampletrack>`. The folder's routing mode writes `mixch`; the two must not both claim the attribute. | **must be checked**; the upgrade reads only instrument/sample elements, which is the correct scope |
| `upgrade_0_4_0_20080118` | `DataFile.cpp:1013-1032` | The precedent to reason from: a *fork-added* child element inside a track's element (`<rack>`) whose name an old `elementsByTagName` walk can match (`docs/RACKS.md` §10 records the same audit for `rack`). The folder's element name must be checked against every existing walk. | **name verified free**: `git grep -niE '"folder"\|trackfolder\|<folder' src/core/DataFile.cpp` → exit 1, no hits |
| `findProblematicLadspaPlugins` | `DataFile.cpp:2136` | A check, not a version; does not iterate tracks. | unaffected |
| `upgrade()`'s own tail | `DataFile.cpp:2216-2229` | Adds `timesig_numerator/denominator` and `mastervol` to `m_head` when absent. A folder adds nothing to `<head>`. | unaffected |

**The upgrade path that must actually change is the *next one*, which does not exist yet.** Adding
`Track::Type::Folder` and a `folder` attribute is a schema addition, and by this file's own rule
(a new element or attribute is discovered by the version stamp) the new enumerator and attribute are
the 32nd entry in `UPGRADE_METHODS` only if the *old value* needs repairing. It does not: an old
project has no `folder` attribute and no folder tracks, so every legacy file loads with zero folders
and re-saves with zero folder attributes — the same forward-compatible degradation `Phase D`'s
`<bus>` marker and `#599`'s `<rack>` element both rely on. **Therefore the correct upgrade-table
entry for this feature is: none, and that is a claim to prove with a save/load test on a
pre-feature file, not an omission.**

**What a folder must *not* do, stated as the anti-goal:** it must not rename, reorder or re-nest any
existing element, and it must not make an old build's render change. An older LMMS reading a
`type` it does not know would `default: break` in `Track::create` (`src/core/Track.cpp:147`) and
return `nullptr` — a **dropped track**, not a degrading one. That is a real forward-compatibility
cost of the enumerator approach and it is *worse* than the `<bus>`/`<rack>` precedent (which are
unknown attributes and elements, ignored safely). It is stated in §7 as the reason the routing mode
defaults to organisational and in §9 as an open question.

---

## 5. The routing half: what "the folder's own mixer channel" means, and the latency question

### 5.1 The existing model it rides

The register's own words are that the routing half "rides the **existing** submix/multi-channel
model, which is already recorded as HAVE" (`NEXT-RELEASE-HANDOFF.md:88-89`, `BACKLOG.md:798-799`),
and the register's evidence for HAVE is `DAW-GAP-ANALYSIS.md` §2 row 9: bussing via mixer channels /
multi-channel ports. The tree agrees, with one naming trap:

| piece | where | what it is |
|---|---|---|
| the channel | `include/Mixer.h:65` `class MixerChannel : public ThreadableJob` | one per mixer channel; index 0 is master; the channel list is an unbounded `std::vector<MixerChannel*>` — declared at `include/Mixer.h:531`, cleared one by one in `~Mixer` (`Mixer.cpp:608-613`) |
| a fresh channel | `Mixer::createChannel()` (`Mixer.cpp:618-648`) → `clearChannel()` (`:1772-1818`) | unity volume, mute/solo off, **no FX, and one send to master** (`:1800-1811`) |
| **a bus channel is not this** | `Mixer::createBusChannel()` (`:1131-1153`), `m_isBus` (`include/Mixer.h:127-137`) | "parallel bus channels: they never receive instrument output and their incoming sends default to pre-fader" |
| how a track reaches a channel | `AudioBusHandle::setNextMixerChannel()` (`include/AudioBusHandle.h:71-72`) driven by `IntModel m_mixerChannelModel` (`InstrumentTrack.h:239-241`) | `InstrumentTrack::updateMixerChannel()` (`InstrumentTrack.cpp:765-767`), `SampleTrack::updateMixerChannel()` (`SampleTrack.cpp:259-261`) |
| the binding is persisted | the `mixch` attribute | `InstrumentTrack.cpp:969`, `SampleTrack.cpp:207`; loaded at `:1038-1041` / `:230-231`; range `0..numChannels()-1` (`InstrumentTrack.cpp:83`) |
| the sum | `Mixer::mixToChannel(const AudioBus&, mix_ch_t)` (`Mixer.cpp:1409-1428`) | `MixHelpers::add(...)` straight into the target channel's buffer |

**The naming trap, and it decides the design:** the register says "submix", which is the word for a
*bus*; but `Mixer::mixToChannel` refuses a bus outright —

```cpp
// Phase D: a parallel bus never receives instrument output directly
// (spec 5.5) - it is fed exclusively by pre-fader sends.
if (mixerChannel->isBus()) { return; }                                  // Mixer.cpp:1412-1417
```

and `Mixer::updateLatencyCompensation` skips a bus channel when accumulating direct-instrument
latency (`Mixer.cpp:1551-1553`: *"A bus never receives instrument output (mixToChannel refuses), so a
handle pointing at a bus contributes nothing"*). So the folder's channel is **a regular
`Mixer::createChannel()` channel, not `createBusChannel()`** — an ordinary channel that other
tracks' handles point at, which is precisely "a channel that several tracks feed", the thing the
tree already supports and the thing the register records as HAVE.

"In the existing model", then, **the folder's own mixer channel is**: one `MixerChannel` whose index
the folder holds (and persists, as `mixch` on its `<trackfolder>` element), at which every child's
`m_mixerChannelModel` points while routing mode is on, which sums the children through
`mixToChannel`, runs its own `m_fxChain` on the sum, and sends to master — like every other channel.

Two lifecycle facts the design must respect, both read off `Mixer`:

* `Mixer::deleteChannel()` **renumbers every channel and rewrites every track's `mixch`**
  (`Mixer.cpp:955-990`, with the track renumber at `:886-925`). The folder channel must be created
  before any child points at it and removed after the last child stops; a folder holding index `k`
  across a `deleteChannel(k)` is a dangling binding, not a crash — but it is a silent wrong route.
* `deleteChannel` already carries the VCA lane's bookkeeping (`refreshGroups()`, `:958`); the
  folder's child list is the same kind of thing and belongs in the same place.

### 5.2 The latency-compensation question, stated precisely

The register names it twice and attributes it to the VCA work:

> "Watch the latency-compensation question the VCA work had to answer (`docs/VCA-GROUPS.md`)."
> — `NEXT-RELEASE-HANDOFF.md:91`

> "the routing mode's sum and the graph's PDC compensation — the same class of question the VCA work
> had to answer" — `BACKLOG.md:808-809`

**Correction, recorded rather than smoothed over:** `docs/VCA-GROUPS.md` does not state that
question. Read from the release line as instructed —

```
$ git -C …/zene-remote show post-alpha/integration:docs/VCA-GROUPS.md | wc -l
299
$ git -C …/zene-remote show post-alpha/integration:docs/VCA-GROUPS.md | grep -nic "latency\|compensat"
0        (grep exit 1)
```

— the document is 299 lines and contains **zero** occurrences of `latency` or `compensat`, and it is
byte-identical to this branch's copy (`diff` of the two `git show` outputs → exit 0, 0 lines). The
question is stated in the register itself, in the VCA row
(`BACKLOG.md:261-262`):

> **Interaction to settle:** a VCA scales its members *and* the graph is PDC-compensated, so the
> **scale must apply before the latency-compensation sum, not after it.**

That is the precise question: **on which side of the compensation point does the new operation sit?**
The VCA lane answered it by putting the multiply on `m_buffer` immediately after the FX chain and
guarded (`src/core/Mixer.cpp:539-545`, `if (vcaGain != 1.0f)`), i.e. on the signal that a receiving
channel then reads through its route's delay line (`MixerRoute::compensatedBuffer`, `:445-446`): the
scale is upstream of every receiver's compensation.

### 5.3 The answer for folders

**The ordering is structural, not a decision the folder can get wrong — and no new PDC code is
needed.** The chain, every line read:

1. PDC landed as #605 and is recorded as landed in the tree's own status page
   (`docs/STATUS.md`: "Plugin delay compensation | Landed #605 (2026-09-10): chain/graph latency +
   summing-point alignment; zero-latency graphs bit-identical").
2. The mixer publishes one alignment point per channel: `resolveLatency()` computes
   `input = max(direct sources, all incoming routes)` and `output = input + m_fxChain.latencyFrames()`
   (`Mixer.cpp:1481-1526`), and `updateLatencyCompensation()` publishes `setInputLatencyFrames(...)`
   for every channel (`:1562-1571`) before any worker runs (`prepareMasterMix()`, `:1433-1437`).
3. **A track's own handle aligns itself to that channel:**
   ```cpp
   m_compensation.setDelayFrames(
       Engine::mixer()->channelInputLatency(m_nextMixerChannel) - latencyFrames());   // AudioBusHandle.cpp:268-269
   m_compensation.processInPlace(m_buffer.data(), fpp);                               // :270
   … Engine::mixer()->mixToChannel(m_bus, m_nextMixerChannel);                          // :275
   ```
4. `mixToChannel` **adds** the aligned block into the target channel's own buffer
   (`Mixer.cpp:1422`) — the child's summand lands in the folder channel's `m_buffer` **before** that
   channel's `doProcessing()` runs its FX chain, and therefore before the folder channel's own
   outbound route is compensated at master.

**Therefore**: point the children's `mixch` at the folder channel and each child's handle delays
itself to the folder channel's published alignment point, exactly as it would for any other channel.
The sum sits upstream of the folder's FX chain and upstream of every compensation point downstream
of it. Switching the folder's mode moves the sum's *grouping*, not its position relative to the
compensation point — which is the VCA question's answer for folders, and it is answered with the
existing machinery and zero new delay lines.

Two residuals, both stated rather than fixed:

* the folder's own FX-chain latency **raises every child's alignment point** (that is what
  `channelInputLatency` returning the folder channel's `inputLatencyFrames` does), which is correct
  behaviour and is the same thing that happens today when two tracks share a mixer channel;
* `LatencyCompensation::MaxFrames` is 16384 frames (`include/LatencyCompensation.h:57`) and the
  delay is clamped, with `updateLatencyCompensation()` tracking `clamped` so `totalLatencyFrames()`
  reports only what the graph can honour (`Mixer.cpp:1573-1586`). A folder whose chain exceeds the
  clamp inherits that bound; it does not add one.

**UNVERIFIED — and it is the honest state of this answer.** No render was made; the whole of §5.3 is
read from the code, not measured. What would settle it, exactly:

1. **PDC alignment (the load-bearing claim).** A project with two instrument children of the same
   folder, where child A's chain (or its instrument) reports latency and child B's reports none,
   rendered in routing mode. The folder channel's published alignment point is the **max** over the
   handles pointing at it, so the assertion is: A's handle computes `max - A_latency` (0 when A is the
   maximum) and B's computes `max - 0` — B is delayed into A's alignment point, and the two children
   arrive aligned at the folder's summing point. `tests/src/core/PdcMixerTest.cpp` already builds
   exactly this shape for two tracks on one channel, so the harness exists.
2. **The mode switch (the register's question).** The same project rendered (a) in organisational
   mode, children on their own channels, and (b) in routing mode with the folder channel at unity,
   no folder FX and its send at 1.0. **Byte-identity is *not* expected** and must not be claimed:
   the children's blocks are grouped differently on the way into master (`((m+a)+b)` when two routes
   reach master, versus `m+(a+b)` when one does, with the extra multiply by 1.0 exact but the
   addition re-associated), so the honest measurement is `max|Δ|` and its bound, plus the exact
   sensitivity control that the two renders are not identical by accident. A `0 LSB` result would be
   a finding; a small-and-bounded `max|Δ|` is the expected outcome and is a *pass* for a change whose
   claim is "adds no DSP", not "is bit-identical".
3. **Behaviour preservation for projects without folders.** A fixture rendered by a pre-feature
   binary and by the feature branch must be **byte-identical** (sha256 of the WAV plus its `data`
   chunk) — that is the claim the tree's own covenant calls the strongest evidence, and it is the
   claim a folder can actually make.

A build and `lmms render` settle all three. Neither was run here: the box takes one build at a time
and another session has one in flight.

---

## 6. Pinning / visibility sets on the same container

### 6.1 The smallest version

The register's own smallest honest version for item 20 was *"pin the top N tracks in the track list so
they stay visible while scrolling — one model flag, one persistence attribute, one affordance"*
(`BACKLOG.md:760-762`), with pinning at **days**, visibility sets at **weeks**, workspace presets at
**months**. The owner's decision of 2026-09-12 moves pinning onto the folder container
(`NEXT-RELEASE-HANDOFF.md:87`: "**pinning/visibility sets** on the same container").

The smallest version on a folder is therefore:

* **one persisted bool** on `TrackFolder` — `pinned`, serialised as an attribute on the
  `<trackfolder>` element, exactly like `collapsed` and `routing` (§4.1). *Free*: the element and its
  loader already exist by the time pinning is added.
* **one affordance** — a pin toggle in `TrackOperationsWidget` (the per-row operations widget,
  `src/gui/tracks/TrackOperationsWidget.cpp`, 341 lines) or on `TrackLabelButton`. It declares a
  registered command (§8).
* **one layout region** — a non-scrolling strip above the scroll area holding the pinned rows.

### 6.2 Does it need anything the organisational half does not already give?

**In the model: no. In the view: yes — exactly one thing.**

| need | given by the organisational half? | evidence |
|---|---|---|
| the persisted flag and its attribute | **yes** — the same `<trackfolder>` element and the same load path | §4.1 |
| the row widget to put the toggle on | **yes** — `TrackView`/`TrackOperationsWidget` exist and are per-row | `include/TrackView.h:81-89` |
| a child/parent relation so a pinned folder can keep its children visible | **yes** — the same relation | §1.2 |
| a **non-scrolling region** | **no** | the track list is **one** `QScrollArea` whose contents widget has **one** `QVBoxLayout` (`src/gui/editors/TrackContainerView.cpp:102-108`); "stays visible while scrolling" means a second layout region outside that scroll area, i.e. one new widget in `TrackContainerView` and one line in the layout at `:96-99` |

So pinning is "one model flag, one persistence attribute, **one affordance plus one layout region**"
for folders. Days, and the days are the widget, not the flag.

**Visibility sets are not given by the organisational half at all** and are not designed here beyond
naming them: named, id-based lists of tracks plus a switcher plus persistence. The persistence target
is the open question — sets are per-project, so they belong either in the project file (another
element and another `<head>`/view-state question, §4.2) or in the project's view state
(`Song::SaveOptions`, `include/Song.h`), and the tree's only existing view-state persistence is
`MainWindow::saveWidgetState`/`restoreWidgetState` via `TrackContainerView::saveSettings`
(`TrackContainerView.cpp:139-151`). The register's band is **weeks**; that is the band.

---

## 7. Per-project workspace / layout presets — the owner's decision, and no design

Owner decision, 2026-09-12, in the register's own terms (`BACKLOG.md:802-805`,
`NEXT-RELEASE-HANDOFF.md:89-91`):

> **One part of item 20 is not weeks — per-project workspace/layout presets are months and no rival
> row requires them: sequence it last or drop it.**

The register reaches that by its own route (`BACKLOG.md:763-764`): pinning **days**, visibility sets
**weeks**, a general workspace system **months**; and it records the exclusion so the lift does not
silently carry a months-scale tail. **This document does not design them.** They are named here only
so their absence is not read as an oversight: no element, no model, no command and no file is
proposed for workspace/layout presets, and they are the one part of the item family that is
explicitly **sequenced last or dropped**.

---

## 8. The agent surface: what this feature must declare

### 8.1 The rule, and where it is enforced

Programme rule **A15**: every action in every menu and toolbar of the real UI must resolve to a
registered command id. It is gate-enforced by the `agent_surface` **ctest**
(`tests/CMakeLists.txt:1004-1012`) running `tests/agent-surface-gate.py` (**496** lines) against a
live `lmms` binary and a fixture project. The gate's own header states the contract precisely
(`tests/agent-surface-gate.py`, "DECLARATION CONTRACT"): an action declares its command through
`objectName()`, the dynamic property `controlCommand`, or — `QAction` only — `data()`; a value that
*looks* like a command id but is not in the registry is a **hard failure that cannot be
grandfathered**. The ratchets are `tests/agent-surface-baseline.txt` (**49** lines, shrink-only, the
unregistered actions), `tests/agent-surface-allowlist.txt` (**30** lines, commands that declare a
`requires` and so cannot be swept), and `tests/agent-surface-negative-control.md` (the deliberate
failure, so the gate is not taken on trust). `control.surface_report` reflects the live UI over the
control socket, so the gate's action list is **not maintained by hand**.

### 8.2 The registry, and its naming conventions

The registry is `include/ControlRegistry.h`: `struct ControlCommand` (`:81-100`) with a stable
`"group.verb"` `id`, `group`, `verb`, `description`, `requiresDecl`, `argsSchema`, `resultSchema`,
`mutating` and a handler; `ControlRegistry::registerCommand` (`:128`), `invoke` (`:136`),
`describeAll` (`:145`); the per-group registration functions declared at `:268-326`. A mutating
command's inverse is **not** the handler's to claim: the registry stamps the contract class from
`src/core/ControlReversibilityTable.cpp` and refuses a handler that claims an inverse the table says
does not exist (`ControlRegistry::stampContract`, `ControlRegistry.h:241-243`).

Conventions read off the 79 registered ids (`git grep -oh 'cmd.id = QStringLiteral("[a-z_]*\.[a-z_]*")'`):

| group | ids | verbs in use |
|---|---|---|
| `track` | **8** | `add`, `remove`, `rename`, `set_arm`, `set_mute`, `set_solo`, `list`, `get_state` |
| `mixer` | 5 | `add_channel`, `remove_channel`, `set_volume`, `set_pan`, `get_state` |
| `clip` | 7 | `add`, `move`, `resize`, `split`, `delete`, `duplicate`, `select` |
| `note` | 6 | `add`, `remove`, `move`, `resize`, `velocity_set`, `select` |
| `control` | 8 | `ping`, `version`, `commands_list`, `undo`, `redo`, `quit`, `transactions`, `surface_report` |
| `arrangement` | 1 | `get_state` |
| others | 44 | `plugin.*` (11), `transport.*` (5), `automation.*` (5), `project.*` (4), `test.*` (5), `dsp`, `roll`, `render`, `settings`, `audio`, `midi`, `app`, `script`, `telemetry` |

**The rules the conventions imply:** ids are lowercase `group.verb`; a multi-word verb uses
`_` (`get_state`, `set_mute`, `add_channel`, `velocity_set`); **no group is multi-word** (there is no
`trackFolder.*`, no camelCase anywhere), so a folder's commands belong in the existing **`track`**
group. The eight `track.*` commands live in three files —
`src/core/ControlCommandsArrangement.cpp` (`add` `:106`, `remove` `:181`, `rename` `:268`,
`set_mute` `:411`, `set_solo` `:432`), `src/core/ControlCommandsArrangementState.cpp`
(`set_arm` `:97`, `arrangement.get_state` `:46`) and `src/core/ControlCommandsTransport.cpp`
(`list` `:231`, `get_state` `:260`) — and the group's registration entry point is
`registerArrangementCommands(ControlRegistry&)` (`ControlCommandsArrangement.cpp:453-462`, declared
`ControlRegistry.h:301`).

### 8.3 The commands this feature needs

| id | kind | args | notes |
|---|---|---|---|
| `track.add` *(existing id, extended schema)* | mutating | `type` enum gains `"folder"` | `ControlCommandsArrangement.cpp:112-115`; the handler's `trackTypeForName` (`:54-60`) must learn it, and the declared enum is part of the wire schema, so the gate re-sweeps it. Already in the reversibility table (`ControlReversibilityTable.cpp:93`) |
| `track.set_folder` *(new)* | mutating | `track` (`trk-<n>`), `folder` (`trk-<n>` or empty for the container root) | the reparenting operation; returns the resulting flat `index`. Needs a `TrueInverse` row in `ControlReversibilityTable.cpp` or the registry refuses the inverse |
| `track.folder_set_collapsed` *(new)* | mutating | `track`, `collapsed` (bool) | the collapse affordance's command. `TrueInverse` (the previous value); saved state, so it is genuinely mutating |
| `track.set_routing` *(new)* | mutating | `track`, `routing` (bool) | refuses unless the track is a folder, with the typed `Refused` pattern `track.set_arm` uses (`ControlCommandsArrangementState.cpp:117-120`). Its inverse is not the mode alone but **the mode plus every child's previous `mixch`**, so its table row is snapshot-class and the snapshot is the child→channel map, not a single value |
| `track.set_pinned` *(new)* | mutating | `track`, `pinned` (bool) | `TrueInverse`; §6 |
| `track.list`, `track.get_state` *(existing ids, extended result)* | not mutating | — | each entry gains a `folder` field (the parent's `trk-<n>`, empty at the root). Their descriptions already carry the "addressing is scoped to the SONG container" sentence (`ControlCommandsTransport.cpp:234-237`) and that sentence becomes where the folder relation is stated |
| `arrangement.get_state` *(existing id, extended result)* | not mutating | — | same `folder` field per track (`ControlCommandsArrangementState.cpp:49-53`, `:70`) |
| `track.visibility_set_*` *(named, not specified)* | — | — | belongs with the weeks-scale visibility-set half (§6.2); not designed here |

**Two design decisions inside that table**, both taken to keep existing clients working:

1. **The `tracks` array stays flat and the folder is a *field*, not a nesting.** `control::trackEditState`
   already emits `index` (`ControlEditSupport.cpp:330-341`) and every consumer of `ClipRef::trackIndex`
   (`include/ControlEdit.h:84`) reads a flat index. Nesting the `tracks` array would break every
   client of `track.list`, `track.get_state` and `arrangement.get_state` for no gain. The folder's
   flat `index` is its own row number in `tracks()`, and the parent is named by id — which is already
   how the surface prefers to address things (`trk-<n>` is "assigned at creation and persists in the
   project file", `ControlCommandsTransport.cpp:234-236`).
2. **The collapse affordance declares its command and does not touch the baseline.** The collapse
   chevron lives on the track row. If it is a `QToolButton` or a `QAction`, `controlCommand` on the
   action is all A15 needs; `tests/agent-surface-baseline.txt` should need **no new line**, and a new
   line would mean an action shipped without a command. Any *menu* item ("New folder", "Move into
   folder", "Route children here") is the same obligation. A collapse command is model state and
   headless-safe, so it declares no `requires` and is swept — an allowlist entry would be wrong.

**Reverse completeness, stated:** the gate requires every *declared* command to be swept headlessly
or excused with a `requires`. Every command in the table above is headless-safe (folder, collapse
mode, routing mode and pin state are all model state addressable from a fixture project), so none of
them may be added to `tests/agent-surface-allowlist.txt`, and the `track.set_routing` refusal path
(not a folder) is a **typed result**, which is exactly what the sweep accepts.

---

## 9. The smallest honest version, its band, and the first three commits

### 9.1 The smallest honest version

**A `TrackFolder` that is one row of the existing flat track list, references (never owns) its child
tracks, persists the relation as an attribute on the child's own `<track>` element, and collapses in
the track list. Children keep their own mixer channels. No audio path is touched.**

The register agrees and names the same shape (`BACKLOG.md:796-799`): *"the entity above,
organisational mode first — a folder that contains and collapses tracks, children keeping their own
mixer channels, saved and reloaded — because that is the half users ask for and it needs no
audio-path work."* The two structural choices that make it the smallest version are argued in this
document:

* **the folder is a `Track`, not a new `TrackContainer`** — so `m_tracks`, `tracks()`, the document
  order, `Track::create`, `TrackContainerView::addTrackView`, `track.list` and the whole save/load
  path keep working, and the change is a new enumerator plus an attribute plus a visibility pass
  (§1, §2, §3.6, §4.2);
* **children are referenced, never owned** (§1.2) — because `TrackContainer` is the only owner and a
  second owner is a double free on teardown.

### 9.2 The band

| half | band | what this inventory says |
|---|---|---|
| the entity + organisational mode (folder, children, collapse, save/load) | **days–weeks** (register: *weeks*) | the position-addressing core is **52 sites in 16 files** of which ~10 are load-bearing; **43 switch arms in 6 files**; **one `QVBoxLayout`**. The iteration surface (154 accesses / 45 files) needs no change. The register's weeks is the **upper** end of the band |
| the routing mode (folder channel, child `mixch` snapshot/restore, PDC proof) | **weeks** | ~46 `mixerChannelModel` references, but they are all instrument/sample-track call sites that need **no** change — the change is the binding plus `Mixer::deleteChannel` bookkeeping plus a render proof |
| pinning | **days** | flag + attribute are free; the cost is one affordance **and one non-scrolling region** (§6.2) |
| visibility sets | **weeks** | named, id-based lists + switcher + persistence; the persistence target is the open question (§6.2) |
| per-project workspace/layout presets | **months** — owner's decision, **sequenced last or dropped** | §7; not designed |

### 9.3 The first three commits

**Commit 1 — the entity, with reference-owned children.**
`feat(track): TrackFolder as a Track type, with reference-owned children`

| file | what |
|---|---|
| `include/TrackFolder.h` *(new)* | `class TrackFolder : public Track`; `std::vector<Track*> m_children`; `BoolModel`s for `collapsed`/`routing`/`pinned`; `int m_mixerChannel{-1}` |
| `src/tracks/TrackFolder.cpp` *(new)* | the model, no view |
| `include/Track.h` | `Track::Type::Folder` immediately before `Count`; `TrackFolder* parentFolder()` / `setParentFolder()`; `m_parentFolder` |
| `src/core/Track.cpp` | one arm in `Track::create`'s factory switch (`:137-148`); `~Track` unlinks from the parent **before** `emit destroyedTrack()` (`:112`) |
| `src/core/TrackContainer.cpp` | the ownership rule stated and enforced in `removeTrack` / `clearAllTracks` — a folder never deletes a child |
| `src/core/CMakeLists.txt`, `tests/fork-sources.txt` | registration (Gate 9) |

Functions: `TrackFolder::TrackFolder(TrackContainer*)`, `addChild`, `removeChild`, `children()`,
`isCollapsed`/`setCollapsed`, `Track::parentFolder`/`setParentFolder`, `TrackContainer::removeTrack`,
`TrackContainer::clearAllTracks`. Not in this commit: any file I/O, any GUI, any command.

**Commit 2 — serialisation and the deferred parent link.**
`feat(saveload): the folder relation as an attribute, resolved after the walk`

| file | what |
|---|---|
| `include/Track.h` | `TrackFolder::nodeName()` returns `"trackfolder"` |
| `src/core/Track.cpp` | `saveTrack` writes the `folder` attribute (the parent's id, **never** a child element — `:228-233`); `loadTrack` reads it |
| `src/core/TrackFolder.cpp` | `saveTrackSpecificSettings` / `loadTrackSpecificSettings` for `collapsed`, `routing`, `pinned`, `mixch` |
| `src/core/Song.cpp` | `Song::resolveTrackFolders()` called beside `ControllerConnection::finalizeConnections()` (`:1356`); a dangling `folder` attribute repaired and counted in the id pass (`:1330-1352`) |
| `src/core/TrackContainer.cpp` | `findTrackById(int)` for the resolution and for the id pass |
| `tests/src/core/TrackFolderTest.cpp` *(new)*, `tests/CMakeLists.txt`, `tests/fork-sources.txt` | the round-trip test: folder with children saved, loaded, re-saved; a pre-feature file loading to **zero** folders and re-saving with **no** `folder` attribute (§4.4) |

**Commit 3 — the collapse in the track list, and the commands.**
`feat(gui+control): folder collapse in the track list, with its commands`

| file | what |
|---|---|
| `src/gui/editors/TrackContainerView.cpp` | `realignTracks()` (`:258-267`) shows a row iff no ancestor folder is collapsed; `totalHeightOfTracks()` (`:484-492`) and `trackViewAt()` (`:304-319`) skip hidden rows; `moveTrackView()` (`:190-210`) keeps the model index and the visible-row index apart |
| `include/TrackContainerView.h` | a visible-row helper (const) |
| `src/gui/editors/SongEditor.cpp` | `trackIndexFromSelectionPoint(int)` (`:892`) skips hidden rows |
| `src/gui/tracks/TrackOperationsWidget.cpp` | the chevron, declaring `controlCommand` |
| `src/core/ControlCommandsArrangement.cpp`, `include/ControlRegistry.h` | `registerTrackFolderCommands(ControlRegistry&)` with `track.set_folder`, `track.folder_set_collapsed`, `track.set_routing`, `track.set_pinned`; the `type` enum of `track.add` gains `"folder"`; the `folder` field on `track.list` / `track.get_state` / `arrangement.get_state` |
| `src/core/ControlReversibilityTable.cpp` | one row per new command (`TrueInverse` for three, snapshot for `track.set_routing`) |
| `tests/src/core/TrackFolderTest.cpp` | collapse visibility, the reparent round-trip, the `track.set_routing` refusal on a non-folder |

`tests/agent-surface-baseline.txt` is expected to be **unchanged** (every new affordance declares a
command id); if it grows, an action shipped without one.

---

## 10. What this document does not know

Everything in this section is **UNVERIFIED** and each item names what would settle it. None of it was
run: no `cmake`, no build, no `ctest`, no `lmms render`.

1. **No code was compiled.** The proposed `TrackFolder`, the new enumerator, the `folder` attribute
   and the new commands have never been built. It is not known that they compile, that the 43 switch
   arms are the complete set a compiler would require, or that `-Werror=reorder` and the
   `-Wswitch`-family warnings are satisfied. A build settles it.
2. **The render claims in §5.3 are read, not measured.** (a) that the two children of a routing
   folder are sample-aligned at the folder's summing point, (b) the `max|Δ|` between organisational
   and routing renders, and (c) the byte-identity of a folder-free fixture against a pre-feature
   binary. `lmms render` plus `tests/src/core/PdcMixerTest.cpp`'s existing harness settles all three.
3. **That `setVisible(false)` on a row of a `QVBoxLayout` with `SetMinAndMaxSize`
   (`TrackContainerView.cpp:103-108`) actually collapses the row and re-flows the scroll area** is a
   claim about Qt behaviour read from the code, not observed. A GUI run — or the repository's own
   headless capture tooling (the `lmms-lab` MCP server in `mcp-lmms-lab/`) — settles it.
4. **The persistence choice in §4.1 is a design decision, not a measurement.** An attribute on the
   child's own `<track>` element is chosen because `Track::loadTrack` turns an unrecognised child
   element into a real `Clip` (`src/core/Track.cpp:228-233`). It is not known whether a *later*
   requirement (a folder carrying per-child ordering, a folder's own clips, a folder in the pattern
   container) would force a child element and therefore a change to `loadTrack`'s clip rule.
5. **The forward-compatibility cost in §4.4 is stated, not solved.** An older LMMS reading
   `Track::Type::Folder` hits `default: break` in `Track::create` (`src/core/Track.cpp:147`) and
   returns `nullptr` — the folder row is dropped, so a folder project opened in a pre-feature build
   loses the folder and (if the folder is what the children hang from) their grouping. What an older
   build does with the children themselves is **not known** without building the older binary and
   opening the file. This is *worse* than the `<bus>`/`<rack>` precedents (where an unknown element
   or attribute is ignored safely) and is the strongest argument for a folder being organisational
   state that a legacy build can degrade gracefully on. A two-binary test settles it.
6. **Whether `Track::Type::Folder` needs arms in places this document did not look.** The count is
   43 `case Track::Type::` arms in 6 files plus the factory switch, but the count is over the
   searched roots; a plugin or a test outside them could switch on the type. The compiler is the real
   enumeration.
7. **How `PatternEditor` should behave.** `TrackContainerView` has exactly two subclasses
   (`SongEditor`, `PatternEditor`), so the collapse change lands in both. Whether a folder in the
   *pattern* container is meaningful, or should be refused there, is not decided — the register's
   decision is about the song's track list and nothing in it speaks to the pattern editor.
8. **Whether a folder needs a `TrackView` subclass.** `TrackView` is instantiated through
   `Track::createView(TrackContainerView*)` (`include/Track.h:107`), and a folder's row has no
   `TrackContentWidget` content. Whether it reuses `TrackView` with an empty content area or needs
   its own view class is not decided here, and it is where the view-side line count in §3.5 could
   move.
9. **The size of the pinning widget.** §6.2 says "one layout region"; the number of lines that is in
   `TrackContainerView` is not counted, because the design of the region (a second `QVBoxLayout`
   above `m_scrollArea`, versus a pinned *section* inside the scroll content) is not settled.
10. **Whether the register's own 2026-09-12 wording and this document agree on one point.**
    The register calls the routing half's model "submix/multi-channel" and records it as HAVE; this
    document finds that `createBusChannel()`'s bus is precisely *not* the channel that can receive
    instrument output (`Mixer.cpp:1412-1417`, `:1551-1553`), so the folder's channel is a regular
    channel. Both are "already HAVE" in the sense the register means, but a reader who takes
    "submix" to mean `createBusChannel()` will build the wrong thing. The register is not edited here
    (this document is the deliverable); the correction is recorded in §5.1.

---

## Corrections applied after the independent audit (2026-09-12)

The independent audit (`NEXT-WAVE1-DOC-AUDIT.md`, commit `c22fa23` in the workspace repo) read this
document at `f27a1dcc9` and reported two false claims. Both were re-derived with the document's own
pathspec `R` before the text changed; the band, the design and every decision are untouched.

1. **§3.1's flat-list breakdown rows.** Was: `tracks()` **98**/38, `m_tracks` **51**/8, `trackViews()` / `m_trackViews` **12 / (22 total)**/7 — which did not re-derive and summed to 161 against the table's own 154. Now: **97**/38, **24**/4, **33** (11 + 22)/7, which sum to 154. Settled by `git grep -nE '\btracks\(\)' -- $R | wc -l` → 97; `git grep -nE '\bm_tracks\b' -- $R | wc -l` → 24; `git grep -nE '\btrackViews\(\)|\bm_trackViews\b' -- $R | wc -l` → 33. The 51/8 row reproduces only **without** a word boundary (`git grep -nE 'm_tracks' -- $R | wc -l` → 51 in 8 files), which also counts `m_tracksMutex` (17 lines), `m_tracksToRender` (8) and `song_num_tracks` (2); both readings are now stated with their method. The headline 154/45 and the per-file table are unchanged and reproduce.
2. **§5.1's channel-list citation.** Was: the unbounded `std::vector<MixerChannel*>` cited at `Mixer.cpp:608-613`. Now: declared at `include/Mixer.h:531` (`grep -n 'std::vector<MixerChannel\*> m_mixerChannels;' include/Mixer.h` → `531:`) and cleared one by one in the `~Mixer` loop at `Mixer.cpp:608-613` (`while( m_mixerChannels.size() ) { … delete f; }`) — the old citation pointed at the destructor, not at the type.

**Audit claims that did not reproduce:** none in §B. Both findings re-derived exactly, including the
161-vs-154 sum and the word-boundary count.
