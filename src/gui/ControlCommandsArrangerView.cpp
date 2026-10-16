/*
 * ControlCommandsArrangerView.cpp - the arranger's view: zoom, zoom history, fit, tool and tool menu
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

/*! 0.5.0 L13: WF-094 zoom history and toggle (arrangement.zoom_back / zoom_forward / zoom_toggle), WF-095 fit the song, the
 *  loop or the selection across the width and every track into the height (arrangement.zoom
 *  {fit}, arrangement.fit_tracks), and WF-108 the tool menu at the pointer (window.tool_menu,
 *  arrangement.set_tool). The zoom and the tool are interface state; track heights are saved
 *  with the project, so fit_tracks is one undo step. */

#include <algorithm>
#include <climits>
#include <cmath>

#include <QCursor>
#include <QJsonArray>
#include <QMenu>
#include <QScrollArea>
#include <QScrollBar>

#include "ArrangerOverlays.h"
#include "ArrangerRulerLanes.h"
#include "ArrangerZoom.h"
#include "ClipView.h"
#include "CommandForm.h"
#include "ControlReversibility.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "ControlWindowCommands.h"
#include "GuiApplication.h"
#include "MainWindow.h"
#include "ShellSelection.h"
#include "Song.h"
#include "SongEditor.h"
#include "TimeLineWidget.h"
#include "Track.h"
#include "TrackView.h"

namespace lmms
{

using namespace control;

namespace
{

constexpr int MaxFitHeight = 256;

gui::SongEditor* arranger()
{
	gui::GuiApplication* g = gui::getGUI();
	return g != nullptr && g->songEditor() != nullptr ? g->songEditor()->m_editor : nullptr;
}

ControlResult noArranger(const QString& id)
{
	return noInterface(id);
}

QJsonObject zoomState(gui::SongEditor* editor)
{
	return {{QStringLiteral("pixels_per_bar"), static_cast<double>(editor->pixelsPerBar())},
		{QStringLiteral("zoom"), editor->zoomingModel()->value()}, {QStringLiteral("history"), gui::arrangerzoom::historySize()},
		{QStringLiteral("forward"), gui::arrangerzoom::forwardSize()}};
}

//! The tick range a fit spans, or false (with @a error) when there is nothing to fit.
bool fitRange(gui::SongEditor* editor, const QString& fit, tick_t* begin, tick_t* end, QString* error)
{
	Song* song = Engine::getSong();
	*begin = 0;
	*end = static_cast<tick_t>(std::max<bar_t>(1, song->length())) * TimePos::ticksPerBar();
	if (fit == QStringLiteral("loop"))
	{
		const Timeline& timeline = song->getTimeline(Song::PlayMode::Song);
		*begin = timeline.loopBegin().getTicks();
		*end = timeline.loopEnd().getTicks();
	}
	// 0.5.0 theme 9 (wave 3): the selection as every keyboard zoom reads it - the time range, else the selected clips.
	else if (fit == QStringLiteral("selection")) { gui::arrangerzoom::selectionSpan(editor, begin, end); }
	if (*end <= *begin) { *error = QStringLiteral("nothing to fit: the %1 is empty").arg(fit); }
	return *end > *begin;
}

//! Zoom and scroll so [@a begin, @a end) fills the arranger's width.
void fitSpan(gui::SongEditor* editor, tick_t begin, tick_t end)
{
	const int width = gui::arrangerzoom::visibleWidth(editor);
	const double bars = static_cast<double>(end - begin) / TimePos::ticksPerBar();
	editor->zoomingModel()->setValue(editor->zoomValueFor(static_cast<int>(std::max(1.0, std::floor(width / bars)))));
	editor->horizontalScroll()->setValue(static_cast<int>(begin));
}

//! What is wrong with a zoom span: one end without the other, or the ends reversed; empty when nothing is.
QString spanProblem(const QJsonObject& args)
{
	const bool start = args.contains(QStringLiteral("start"));
	if (start != args.contains(QStringLiteral("end"))) { return QStringLiteral("'start' and 'end' come together"); }
	if (start && args.value(QStringLiteral("end")).toInt() < args.value(QStringLiteral("start")).toInt())
	{
		return QStringLiteral("'end' must not be before 'start'");
	}
	return QString();
}

/*! 0.5.0 theme 9: the tick a step or pixels_per_bar zoom keeps where it is on screen - 'anchor_tick'
 *  (a pointer gesture's: the tick under the pointer), else the 'anchor' named: the selection, the
 *  playhead, the view's middle or its start (the zoom before 0.5.0), or by default (auto) the selection,
 *  else the playhead on screen, else the view's middle. */
tick_t zoomAnchor(gui::SongEditor* editor, const QJsonObject& args)
{
	if (args.contains(QStringLiteral("anchor_tick"))) { return std::max(0, args.value(QStringLiteral("anchor_tick")).toInt()); }
	const QString anchor = args.value(QStringLiteral("anchor")).toString(QStringLiteral("auto"));
	const tick_t start = editor->currentPosition().getTicks();
	if (anchor == QStringLiteral("start")) { return start; }
	if (anchor == QStringLiteral("view")) { return start + gui::arrangerzoom::visibleTicks(editor) / 2; }
	if (anchor == QStringLiteral("playhead")) { return gui::arrangerzoom::playhead(editor); }
	tick_t begin = 0;
	tick_t end = 0;
	if (anchor == QStringLiteral("selection") && gui::arrangerzoom::selectionSpan(editor, &begin, &end)) { return begin + (end - begin) / 2; }
	return gui::arrangerzoom::keyboardAnchor(editor);
}

//! The zoom model value a 'pixels_per_bar' or 'step' zoom asks for (clamped to the model).
int requestedZoom(gui::SongEditor* editor, const QJsonObject& args)
{
	IntModel* model = editor->zoomingModel();
	const long long value = args.contains(QStringLiteral("pixels_per_bar"))
		? editor->zoomValueFor(static_cast<int>(std::lround(args.value(QStringLiteral("pixels_per_bar")).toDouble())))
		: static_cast<long long>(model->value()) + args.value(QStringLiteral("step")).toInt(1);
	return static_cast<int>(std::clamp(value, static_cast<long long>(model->minValue()), static_cast<long long>(model->maxValue())));
}

ControlResult zoom(const QJsonObject& args)
{
	gui::SongEditor* editor = arranger();
	if (editor == nullptr) { return noArranger(QStringLiteral("arrangement.zoom")); }
	if (const QString problem = spanProblem(args); !problem.isEmpty()) { return ControlResult::failure(ControlErrorKind::InvalidArgs, problem); }
	IntModel* model = editor->zoomingModel();
	const int before = model->value();
	tick_t anchor = -1;
	if (args.contains(QStringLiteral("start")) && args.contains(QStringLiteral("end")))
	{
		// ED-008: the zoom tool's span fills the width.
		const tick_t begin = args.value(QStringLiteral("start")).toInt();
		const tick_t end = std::max(begin + 1, args.value(QStringLiteral("end")).toInt());
		fitSpan(editor, begin, end);
	}
	else if (args.contains(QStringLiteral("fit")))
	{
		tick_t begin = 0;
		tick_t end = 0;
		QString error;
		if (!fitRange(editor, args.value(QStringLiteral("fit")).toString(), &begin, &end, &error))
		{
			return ControlResult::failure(ControlErrorKind::Refused, error);
		}
		fitSpan(editor, begin, end);
	}
	else
	{
		anchor = zoomAnchor(editor, args);
		gui::arrangerzoom::zoomAround(editor, requestedZoom(editor, args), anchor);
	}
	if (model->value() != before) { gui::arrangerzoom::remember(before); }
	QJsonObject result = zoomState(editor);
	result.insert(QStringLiteral("previous_zoom"), before);
	if (anchor >= 0) { result.insert(QStringLiteral("anchor"), static_cast<int>(anchor)); }
	return ControlResult::success(result);
}

//! 0.5.0 L13 (WF-094): zoom back / forward walk the arranger's zoom history - each of the several prior
//! extents, like the browser back and forward. A new zoom cuts the forward walk off (nothing to walk back
//! to). Like every keyboard zoom they keep the selection or the playhead in place (0.5.0 theme 9).
ControlResult zoomBack(const QJsonObject&)
{
	gui::SongEditor* editor = arranger();
	if (editor == nullptr) { return noArranger(QStringLiteral("arrangement.zoom_back")); }
	int earlier = 0;
	if (!gui::arrangerzoom::takeEarlier(editor->zoomingModel()->value(), &earlier))
	{
		return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no earlier zoom to go back to"));
	}
	gui::arrangerzoom::zoomAround(editor, earlier, gui::arrangerzoom::keyboardAnchor(editor));
	return ControlResult::success(zoomState(editor));
}

ControlResult zoomForward(const QJsonObject&)
{
	gui::SongEditor* editor = arranger();
	if (editor == nullptr) { return noArranger(QStringLiteral("arrangement.zoom_forward")); }
	int next = 0;
	if (!gui::arrangerzoom::takeNext(editor->zoomingModel()->value(), &next))
	{
		return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no later zoom to go forward to"));
	}
	gui::arrangerzoom::zoomAround(editor, next, gui::arrangerzoom::keyboardAnchor(editor));
	return ControlResult::success(zoomState(editor));
}

//! 0.5.0 L13 (WF-094): swap the current zoom with the extent it last left, so pressing the toggle twice
//! flips between the two; unlike back it walks neither way of the history.
ControlResult zoomToggle(const QJsonObject&)
{
	gui::SongEditor* editor = arranger();
	if (editor == nullptr) { return noArranger(QStringLiteral("arrangement.zoom_toggle")); }
	int extent = 0;
	if (!gui::arrangerzoom::takeToggle(editor->zoomingModel()->value(), &extent))
	{
		return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no earlier extent to swap with"));
	}
	gui::arrangerzoom::zoomAround(editor, extent, gui::arrangerzoom::keyboardAnchor(editor));
	return ControlResult::success(zoomState(editor));
}

QJsonArray trackHeights(const QList<gui::TrackView*>& views)
{
	QJsonArray out;
	for (gui::TrackView* view : views) { out.append(view->getTrack()->getHeight()); }
	return out;
}

//! Whether every entry of @a heights is a whole number of pixels (QJsonValue::toInt would read anything else as 0).
bool wholeHeights(const QJsonArray& heights)
{
	return std::all_of(heights.begin(), heights.end(), [](const QJsonValue& height) {
		return height.isDouble() && height.toDouble() == std::floor(height.toDouble()) && std::abs(height.toDouble()) <= INT_MAX;
	});
}

void setHeights(gui::SongEditor* editor, const QJsonArray& heights)
{
	const QList<gui::TrackView*>& views = editor->trackViews();
	for (int i = 0; i < views.size() && i < heights.size(); ++i)
	{
		const int height = std::max(MINIMAL_TRACK_HEIGHT, heights.at(i).toInt());
		views.at(i)->getTrack()->setHeight(height);
		views.at(i)->setFixedHeight(height);
	}
	editor->realignTracks();
}

ControlResult fitTracks(const QJsonObject& args)
{
	gui::SongEditor* editor = arranger();
	if (editor == nullptr) { return noArranger(QStringLiteral("arrangement.fit_tracks")); }
	const QList<gui::TrackView*>& views = editor->trackViews();
	if (views.isEmpty()) { return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("the arrangement has no tracks")); }
	const QJsonArray before = trackHeights(views);
	QJsonArray wanted = args.value(QStringLiteral("heights")).toArray();
	if (!wholeHeights(wanted)) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'heights' holds whole pixel counts")); }
	if (wanted.isEmpty())
	{
		const int each = std::clamp(editor->contentWidget()->viewport()->height() / static_cast<int>(views.size()),
			MINIMAL_TRACK_HEIGHT, MaxFitHeight);
		for (int i = 0; i < views.size(); ++i) { wanted.append(each); }
	}
	QPointer<gui::SongEditor> guarded(editor);
	control::addUndoStep([guarded, before] { if (!guarded.isNull()) { setHeights(guarded, before); } },
		[guarded, wanted] { if (!guarded.isNull()) { setHeights(guarded, wanted); } });
	setHeights(editor, wanted);
	QJsonObject result{{QStringLiteral("heights"), trackHeights(views)}, {QStringLiteral("tracks"), views.size()}};
	result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("heights"), before}},
		QStringLiteral("arrangement.fit_tracks"), {{QStringLiteral("heights"), before}}, true,
		QStringLiteral("action checkpoint: the recorded undo step gives every track its previous height back")));
	return ControlResult::success(result);
}

//! The song editor's own modes, in EditMode order, then the overlay's tools (ArrangerRangeOverlay):
//! range, audition, zoom, smart - in ArrangerRangeOverlay::Tool order after None.
const QStringList& toolNames()
{
	return gui::arrangerToolNames();
}

constexpr int EditorModeCount = 3;

ControlResult setTool(const QJsonObject& args)
{
	gui::SongEditor* editor = arranger();
	if (editor == nullptr) { return noArranger(QStringLiteral("arrangement.set_tool")); }
	const QString tool = args.value(QStringLiteral("tool")).toString();
	const int index = static_cast<int>(toolNames().indexOf(tool));
	const bool overlayTool = index >= EditorModeCount;
	// The overlay's tools work over the song editor in select mode (smart moves clips as select does).
	editor->setEditMode(overlayTool ? gui::SongEditor::EditMode::Select : static_cast<gui::SongEditor::EditMode>(index));
	if (auto* overlay = editor->findChild<gui::ArrangerRangeOverlay*>())
	{
		overlay->setTool(overlayTool ? static_cast<gui::ArrangerRangeOverlay::Tool>(index - EditorModeCount + 1) : gui::ArrangerRangeOverlay::Tool::None);
	}
	return ControlResult::success({{QStringLiteral("tool"), overlayTool ? tool : toolNames().at(static_cast<int>(editor->editMode()))}});
}

//! The tools the menu at the pointer offers: the arranger's, then the note editor's.
QJsonArray toolEntries()
{
	QJsonArray out;
	for (const QString& tool : toolNames())
	{
		out.append(QJsonObject{{QStringLiteral("editor"), QStringLiteral("arranger")}, {QStringLiteral("tool"), tool},
			{QStringLiteral("command"), QStringLiteral("arrangement.set_tool")}});
	}
	for (const char* tool : {"draw", "select", "slice", "erase", "audition", "paint", "mute"})
	{
		out.append(QJsonObject{{QStringLiteral("editor"), QStringLiteral("note_editor")}, {QStringLiteral("tool"), QLatin1String(tool)},
			{QStringLiteral("command"), QStringLiteral("window.note_editor_set")}});
	}
	return out;
}

ControlResult toolMenu(const QJsonObject&)
{
	const QJsonArray entries = toolEntries();
	const bool shown = !ControlRegistry::instance()->isHeadless();
	// Only a menu that is shown needs the window: headless, the entries are the answer.
	if (shown && controlMainWindow() == nullptr) { return noInterface(QStringLiteral("window.tool_menu")); }
	if (shown)
	{
		auto* menu = new QMenu(controlMainWindow());
		menu->setAttribute(Qt::WA_DeleteOnClose);
		QString section;
		for (const QJsonValue& value : entries)
		{
			const QJsonObject entry = value.toObject();
			const QString editor = entry.value(QStringLiteral("editor")).toString();
			if (editor != section) { menu->addSection(editor == QStringLiteral("arranger") ? QObject::tr("Arranger") : QObject::tr("Note editor")); section = editor; }
			QAction* action = menu->addAction(entry.value(QStringLiteral("tool")).toString());
			action->setProperty("controlCommand", entry.value(QStringLiteral("command")).toString());
			QObject::connect(action, &QAction::triggered, menu, [entry] {
				// A refusal (no note editor to set) is shown, never dropped with the closing menu.
				const QJsonValue tool = entry.value(QStringLiteral("tool"));
				const gui::CommandCall call{entry.value(QStringLiteral("command")).toString(), {}, {}, tool.toString()};
				gui::announce(controlMainWindow(), call, ControlRegistry::instance()->invoke(call.id, {{QStringLiteral("tool"), tool}}));
			});
		}
		menu->popup(QCursor::pos());
	}
	return ControlResult::success({{QStringLiteral("shown"), shown}, {QStringLiteral("entries"), entries}});
}

//! ED-024: move the selected clip by @a direction x amount (default: the arranger's snap) - clip.move.
ControlResult nudge(const QJsonObject& args)
{
	gui::SongEditor* editor = arranger();
	Clip* clip = gui::ShellSelection::instance()->clip();
	if (editor == nullptr) { return noArranger(QStringLiteral("arrangement.nudge")); }
	if (clip == nullptr) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no clip is selected")); }
	const int snap = std::max(1, static_cast<int>(std::lround(editor->getSnapSize() * TimePos::ticksPerBar())));
	const int amount = args.value(QStringLiteral("amount")).toInt(snap);
	const int sign = args.value(QStringLiteral("direction")).toString() == QStringLiteral("left") ? -1 : 1;
	const int position = std::max(0, clip->startPosition().getTicks() + sign * amount);
	// A clip already at the song's start cannot go further left: say so rather than answer a move that did nothing.
	if (position == clip->startPosition().getTicks())
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("the clip is already at the start"));
	}
	return ControlRegistry::instance()->invoke(QStringLiteral("clip.move"),
		{{QStringLiteral("clip"), control::clipIdOf(clip)}, {QStringLiteral("position"), position}});
}

//! ED-024: select the next / previous clip on the selected clip's track (window.select).
ControlResult selectClip(const QJsonObject& args)
{
	Clip* current = gui::ShellSelection::instance()->clip();
	if (current == nullptr) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no clip is selected")); }
	const bool next = args.value(QStringLiteral("direction")).toString() != QStringLiteral("previous");
	// Clips are ordered by start, then by their place on the track, so clips that start together are each reached.
	const Track::clipVector& clips = current->getTrack()->getClips();
	const auto keyAt = [&clips](std::size_t i) { return std::make_pair(clips[i]->startPosition().getTicks(), i); };
	const auto at = std::make_pair(current->startPosition().getTicks(),
		static_cast<std::size_t>(std::find(clips.begin(), clips.end(), current) - clips.begin()));
	Clip* best = nullptr;
	std::pair<tick_t, std::size_t> bestKey;
	for (std::size_t i = 0; i < clips.size(); ++i)
	{
		const auto key = keyAt(i);
		const bool after = next ? key > at : key < at;
		const bool closer = best == nullptr || (next ? key < bestKey : key > bestKey);
		if (after && closer) { best = clips[i]; bestKey = key; }
	}
	if (best == nullptr) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no clip that way")); }
	return ControlRegistry::instance()->invoke(QStringLiteral("window.select"), {{QStringLiteral("clip"), control::clipIdOf(best)}});
}

void add(ControlRegistry& registry, const char* group, const char* verb, const QString& description,
	const QJsonObject& args, const QJsonObject& result, bool mutating, ControlResult (*handler)(const QJsonObject&))
{
	ControlCommand cmd = guiCommand(group, verb);
	cmd.description = description;
	cmd.argsSchema = args;
	cmd.resultSchema = result;
	cmd.mutating = mutating;
	cmd.handler = handler;
	registry.registerCommand(cmd);
}

} // namespace

void registerArrangerViewCommands(ControlRegistry& registry)
{
	const QJsonObject zoomResult = objectSchema({{QStringLiteral("pixels_per_bar"), numberProperty()},
		{QStringLiteral("zoom"), integerProperty()}, {QStringLiteral("history"), integerProperty()},
		{QStringLiteral("forward"), integerProperty()}, {QStringLiteral("previous_zoom"), integerProperty()},
		{QStringLiteral("anchor"), integerProperty()}});
	add(registry, "arrangement", "zoom", QStringLiteral("Zoom the arranger: to 'pixels_per_bar', by 'step' zoom steps "
		"(negative zooms out), to the span 'start'..'end' (the zoom tool's drag), or to 'fit' the song, the loop or the "
		"selection (the time range, else the selected clips) across its width (scrolled to "
		"its start). A 'pixels_per_bar' or 'step' zoom keeps one tick where it is on screen ('anchor' in the answer): "
		"'anchor_tick' (a pointer gesture passes the tick under the pointer), else 'anchor' - selection, playhead, view "
		"(its middle), start (its left edge) or auto (the default: the selection, else the playhead when it is on "
		"screen, else the middle). The previous zoom joins the history arrangement.zoom_back walks back over. "
		"Interface state."),
		objectSchema({{QStringLiteral("pixels_per_bar"), numberProperty()}, {QStringLiteral("step"), integerProperty()},
			{QStringLiteral("start"), tickProperty()}, {QStringLiteral("end"), tickProperty()},
			{QStringLiteral("fit"), enumProperty({QStringLiteral("song"), QStringLiteral("loop"), QStringLiteral("selection")})},
			{QStringLiteral("anchor"), enumProperty({QStringLiteral("auto"), QStringLiteral("selection"), QStringLiteral("playhead"),
				QStringLiteral("view"), QStringLiteral("start")})}, {QStringLiteral("anchor_tick"), tickProperty()}}),
		zoomResult, false, zoom);
	add(registry, "arrangement", "zoom_back", QStringLiteral("Go back one step in the zoom history, to an earlier "
		"extent - the same way the browser goes back, to walk back over several prior extents. A new "
		"arrangement.zoom cuts what can be walked forward off; the selection, else the playhead on screen, stays "
		"where it is. Refused when there is nothing earlier."),
		objectSchema({}), zoomResult, false, zoomBack);
	add(registry, "arrangement", "zoom_forward", QStringLiteral("Go forward one step in the zoom history, back over "
		"what arrangement.zoom_back went past (the way the browser goes forward). The selection, else the playhead "
		"on screen, stays where it is. Refused when there is nothing later."),
		objectSchema({}), zoomResult, false, zoomForward);
	add(registry, "arrangement", "zoom_toggle", QStringLiteral("Swap the arranger's zoom with the extent it last "
		"left (the zoom before this one, or the extent a back or forward step left behind), so pressing the "
		"toggle twice returns to where you were. The selection, else the playhead on screen, stays where it is. "
		"Refused before any second extent is known."),
		objectSchema({}), zoomResult, false, zoomToggle);
	add(registry, "arrangement", "fit_tracks", QStringLiteral("Give every track the height that fits them all in the "
		"arranger's height (clamped to 32..256 px), or the 'heights' given, in track order. Track heights are "
		"saved with the project, so this is one undo step."),
		objectSchema({{QStringLiteral("heights"), arrayProperty()}}),
		objectSchema({{QStringLiteral("heights"), arrayProperty()}, {QStringLiteral("tracks"), integerProperty()}}), true, fitTracks);
	add(registry, "arrangement", "set_tool", QStringLiteral("Choose the arranger's tool: draw, knife, select, range, audition, zoom or smart. "
		"Beyond the song editor's draw / knife / select: range (draws time ranges, range.set), audition (plays from the "
		"pressed point while held, the pressed track alone), zoom (the dragged span fills the width) and smart (a clip's "
		"upper half draws a range, its lower half moves). Interface state."),
		objectSchema({{QStringLiteral("tool"), enumProperty(toolNames())}}, {QStringLiteral("tool")}),
		objectSchema({{QStringLiteral("tool"), stringProperty()}}), false, setTool);
	{
		// Declared as a coalescing wrapper: a burst of presses is one undo step (the clip.move it runs joins its run).
		ControlCommand nudgeCommand = guiCommand("arrangement", "nudge");
		nudgeCommand.description = QStringLiteral("Move the selected clip left or right by 'amount' ticks "
			"(default: the arranger's snap) - clip.move, which records itself. Refused when the clip is already at the start.");
		nudgeCommand.argsSchema = objectSchema({{QStringLiteral("direction"), enumProperty({QStringLiteral("left"), QStringLiteral("right")})},
			{QStringLiteral("amount"), integerProperty(1, 1 << 20)}}, {QStringLiteral("direction")});
		nudgeCommand.resultSchema = objectProperty();
		nudgeCommand.mutating = false;
		nudgeCommand.coalescesWrapped = QStringLiteral("clip.move");  // the nested call whose run a burst joins
		nudgeCommand.handler = nudge;
		registry.registerCommand(nudgeCommand);
	}
	add(registry, "arrangement", "select_clip", QStringLiteral("Select the next or previous clip on the selected clip's "
		"track (window.select). Interface state."),
		objectSchema({{QStringLiteral("direction"), enumProperty({QStringLiteral("next"), QStringLiteral("previous")})}},
			{QStringLiteral("direction")}), objectProperty(), false, selectClip);
	add(registry, "window", "tool_menu", QStringLiteral("Open the tool menu at the pointer: the arranger's tools and "
		"the note editor's, each running arrangement.set_tool or window.note_editor_set. Headless, nothing is shown "
		"and the entries are answered."), objectSchema({}),
		objectSchema({{QStringLiteral("shown"), booleanProperty()}, {QStringLiteral("entries"), arrayProperty()}}), false, toolMenu);
	registerRulerLaneCommands(registry);  // 0.5.0 theme 9: arrangement.ruler_lanes
}

} // namespace lmms
