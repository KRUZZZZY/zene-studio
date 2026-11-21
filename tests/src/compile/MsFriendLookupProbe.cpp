// RED probe for the ClipView/AutomationTrackView/PatternClipView friend-lookup
// ambiguity: these names must resolve unqualified after the using-directives.
// It is compiled by tests/scripts/ms_friend_lookup_check.py, never built or moc'd.
#include "Clip.h"
#include "ClipView.h"
#include "AutomationTrack.h"
#include "AutomationTrackView.h"
#include "PatternClip.h"
#include "PatternClipView.h"

using namespace lmms;
using namespace lmms::gui;

[[maybe_unused]] static ClipView* s_clipView = nullptr;
[[maybe_unused]] static AutomationTrackView* s_automationTrackView = nullptr;
[[maybe_unused]] static PatternClipView* s_patternClipView = nullptr;
