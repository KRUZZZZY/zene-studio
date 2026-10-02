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

/*! 0.5.0 L13: WF-094 zoom history and toggle (arrangement.zoom_back), WF-095 fit the song, the
 *  loop or the selection across the width and every track into the height (arrangement.zoom
 *  {fit}, arrangement.fit_tracks), and WF-108 the tool menu at the pointer (window.tool_menu,
 *  arrangement.set_tool). The zoom and the tool are interface state; track heights are saved
 *  with the project, so fit_tracks is one undo step. */

#include <algorithm>
#include <cmath>

#include <QCursor>
#include <QJsonArray>
#include <QMenu>
#include <QScrollArea>
#include <QScrollBar>

#include "ArrangerOverlays.h"
#include "ClipView.h"
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

constexpr int HistoryDepth = 32;
constexpr int MaxFitHeight = 256;
QVector<int> g_zoomHistory;  //!< earlier zoom model values, newest last (UI thread only)

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
		{QStringLiteral("zoom"), editor->zoomingModel()->value()}, {QStringLiteral("history"), g_zoomHistory.size()}};
}

void pushHistory(int value)
{
	if (!g_zoomHistory.isEmpty() && g_zoomHistory.last() == value) { return; }
	g_zoomHistory.append(value);
	if (g_zoomHistory.size() > HistoryDepth) { g_zoomHistory.removeFirst(); }
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
	else if (fit == QStringLiteral("selection"))
	{
		*begin = std::numeric_limits<tick_t>::max();
		*end = 0;
		for (gui::selectableObject* object : editor->selectedObjects())
		{
			auto* view = dynamic_cast<gui::ClipView*>(object);
			if (view == nullptr) { continue; }
			*begin = std::min<tick_t>(*begin, view->getClip()->startPosition().getTicks());
			*end = std::max<tick_t>(*end, view->getClip()->endPosition().getTicks());
		}
	}
	if (*end <= *begin) { *error = QStringLiteral("nothing to fit: the %1 is empty").arg(fit); }
	return *end > *begin;
}

//! Zoom and scroll so [@a begin, @a end) fills the arranger's width.
void fitSpan(gui::SongEditor* editor, tick_t begin, tick_t end)
{
	const int width = editor->timeLine()->width() - editor->timeLine()->markerX(editor->currentPosition());
	const double bars = static_cast<double>(end - begin) / TimePos::ticksPerBar();
	editor->zoomingModel()->setValue(editor->zoomValueFor(static_cast<int>(std::max(1.0, std::floor(std::max(width, 1) / bars)))));
	editor->horizontalScroll()->setValue(static_cast<int>(begin));
}

ControlResult zoom(const QJsonObject& args)
{
	gui::SongEditor* editor = arranger();
	if (editor == nullptr) { return noArranger(QStringLiteral("arrangement.zoom")); }
	IntModel* model = editor->zoomingModel();
	const int before = model->value();
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
	else if (args.contains(QStringLiteral("pixels_per_bar")))
	{
		model->setValue(editor->zoomValueFor(static_cast<int>(std::lround(args.value(QStringLiteral("pixels_per_bar")).toDouble()))));
	}
	else { model->incValue(args.value(QStringLiteral("step")).toInt(1)); }
	if (model->value() != before) { pushHistory(before); }
	QJsonObject result = zoomState(editor);
	result.insert(QStringLiteral("previous_zoom"), before);
	return ControlResult::success(result);
}

ControlResult zoomBack(const QJsonObject&)
{
	gui::SongEditor* editor = arranger();
	if (editor == nullptr) { return noArranger(QStringLiteral("arrangement.zoom_back")); }
	if (g_zoomHistory.isEmpty()) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no earlier zoom to go back to")); }
	// Back swaps the current zoom with the last one, so pressing it twice toggles between them.
	const int current = editor->zoomingModel()->value();
	const int earlier = g_zoomHistory.takeLast();
	g_zoomHistory.append(current);
	editor->zoomingModel()->setValue(earlier);
	return ControlResult::success(zoomState(editor));
}

QJsonArray trackHeights(const QList<gui::TrackView*>& views)
{
	QJsonArray out;
	for (gui::TrackView* view : views) { out.append(view->getTrack()->getHeight()); }
	return out;
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
	static const QStringList names{QStringLiteral("draw"), QStringLiteral("knife"), QStringLiteral("select"),
		QStringLiteral("range"), QStringLiteral("audition"), QStringLiteral("zoom"), QStringLiteral("smart")};
	return names;
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
	for (const char* tool : {"draw", "select", "slice", "erase", "audition"})
	{
		out.append(QJsonObject{{QStringLiteral("editor"), QStringLiteral("note_editor")}, {QStringLiteral("tool"), QLatin1String(tool)},
			{QStringLiteral("command"), QStringLiteral("window.note_editor_set")}});
	}
	return out;
}

ControlResult toolMenu(const QJsonObject&)
{
	if (controlMainWindow() == nullptr) { return noInterface(QStringLiteral("window.tool_menu")); }
	const QJsonArray entries = toolEntries();
	const bool shown = !ControlRegistry::instance()->isHeadless();
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
				ControlRegistry::instance()->invoke(entry.value(QStringLiteral("command")).toString(),
					{{QStringLiteral("tool"), entry.value(QStringLiteral("tool"))}});
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
	return ControlRegistry::instance()->invoke(QStringLiteral("clip.move"),
		{{QStringLiteral("clip"), control::clipIdOf(clip)}, {QStringLiteral("position"), position}});
}

//! ED-024: select the next / previous clip on the selected clip's track (window.select).
ControlResult selectClip(const QJsonObject& args)
{
	Clip* current = gui::ShellSelection::instance()->clip();
	if (current == nullptr) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no clip is selected")); }
	const bool next = args.value(QStringLiteral("direction")).toString() != QStringLiteral("previous");
	const tick_t at = current->startPosition().getTicks();
	Clip* best = nullptr;
	for (Clip* clip : current->getTrack()->getClips())
	{
		const tick_t start = clip->startPosition().getTicks();
		const bool after = next ? start > at : start < at;
		const bool closer = best == nullptr || (next ? start < best->startPosition().getTicks() : start > best->startPosition().getTicks());
		if (clip != current && after && closer) { best = clip; }
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
		{QStringLiteral("previous_zoom"), integerProperty()}});
	add(registry, "arrangement", "zoom", QStringLiteral("Zoom the arranger: to 'pixels_per_bar', by 'step' zoom steps "
		"(negative zooms out), to the span 'start'..'end' (the zoom tool's drag), or to 'fit' the song, the loop or the "
		"selected clips across its width (scrolled to "
		"their start). The previous zoom joins the history arrangement.zoom_back walks. Interface state."),
		objectSchema({{QStringLiteral("pixels_per_bar"), numberProperty()}, {QStringLiteral("step"), integerProperty()},
			{QStringLiteral("start"), tickProperty()}, {QStringLiteral("end"), tickProperty()},
			{QStringLiteral("fit"), enumProperty({QStringLiteral("song"), QStringLiteral("loop"), QStringLiteral("selection")})}}),
		zoomResult, false, zoom);
	add(registry, "arrangement", "zoom_back", QStringLiteral("Go back to the previous zoom. It swaps the current zoom "
		"with the last one, so going back twice toggles between two zooms. Refused when there is no earlier zoom."),
		objectSchema({}), zoomResult, false, zoomBack);
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
	add(registry, "arrangement", "nudge", QStringLiteral("Move the selected clip left or right by 'amount' ticks "
		"(default: the arranger's snap) - clip.move, which records itself."),
		objectSchema({{QStringLiteral("direction"), enumProperty({QStringLiteral("left"), QStringLiteral("right")})},
			{QStringLiteral("amount"), integerProperty(1, 1 << 20)}}, {QStringLiteral("direction")}), objectProperty(), false, nudge);
	add(registry, "arrangement", "select_clip", QStringLiteral("Select the next or previous clip on the selected clip's "
		"track (window.select). Interface state."),
		objectSchema({{QStringLiteral("direction"), enumProperty({QStringLiteral("next"), QStringLiteral("previous")})}},
			{QStringLiteral("direction")}), objectProperty(), false, selectClip);
	add(registry, "window", "tool_menu", QStringLiteral("Open the tool menu at the pointer: the arranger's tools and "
		"the note editor's, each running arrangement.set_tool or window.note_editor_set. Headless, nothing is shown "
		"and the entries are answered."), objectSchema({}),
		objectSchema({{QStringLiteral("shown"), booleanProperty()}, {QStringLiteral("entries"), arrayProperty()}}), false, toolMenu);
}

} // namespace lmms
