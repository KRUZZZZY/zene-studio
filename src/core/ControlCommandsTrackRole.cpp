/*
 * ControlCommandsTrackRole.cpp - track.set_role: give a neutral row an instrument, and take
 *                                it back to neutral keeping the instrument (Q050-283.2)
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
 */

/*! 0.5.0 L04, Q050-283 stage 2 of 3 (docs/050-features/Q050-283.2.md).
 *
 *  A row's role is the class of its Track object (NeutralTrack, InstrumentTrack), so a role change
 *  REPLACES the object - but never the row: the replacement is built from the row's own captured
 *  XML, which carries its stable `id`, name, mute/solo, colour, height, folder and lane flags, and it
 *  is put back at the index the row had. Everything else in the project is untouched.
 *
 *  - neutral -> instrument: the row's kept instrument role (the instrument track it was before, with
 *    its plug-in settings and clips) comes back if it has one; otherwise a new instrument track is made
 *    and the named plug-in loaded into it.
 *  - instrument -> neutral: the whole instrument track is captured and KEPT by the neutral row
 *    (NeutralTrack::keptInstrumentRole), saved with it, and is what the next switch restores.
 *
 *  The inverse is the operation (true_inverse): ONE structural undo step holds the row's XML before
 *  and after, and its undo / redo replace the row with the other one at the same index. All of it
 *  runs on the UI/control thread; the object swap goes through the product's own track removal and
 *  creation, which take the audio engine's model-change lock themselves.
 */

#include <QDomDocument>
#include <QJsonArray>
#include <QJsonObject>

#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlReversibility.h"
#include "ControlStructuralSupport.h"
#include "ControlTrackCreation.h"
#include "ControlTrackRole.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "InstrumentTrack.h"
#include "NeutralTrack.h"
#include "PluginFactory.h"
#include "Song.h"
#include "SongEditor.h"
#include "TrackView.h"

namespace lmms
{

using namespace control;

namespace
{

const QString RoleNeutral = QStringLiteral("neutral");
const QString RoleInstrument = QStringLiteral("instrument");

/*! A kept instrument role is bounded at the structural snapshot bound, the same one track.remove's
 *  inverse uses: a larger track is refused (nothing changes) rather than kept truncated. The row's
 *  own document then also carries that role as escaped text - at most 5 characters per character
 *  (`&` -> `&amp;`) - so the before/after documents of a switch are bounded at six times it. */
constexpr int MaxKeptRoleChars = StructuralSnapshotLimit;
constexpr int MaxRoleSwitchChars = 6 * StructuralSnapshotLimit;

//! The attributes and child elements that describe the ROW rather than its role: what a role
//! switch carries from the old object to the new one. Every other attribute (mixlink, monitor,
//! the role's own settings) stays with the role.
bool isRowAttribute(const QString& name)
{
	static const QStringList rowAttributes{QStringLiteral("id"), QStringLiteral("name"),
		QStringLiteral("trackheight"), QStringLiteral("color"), QStringLiteral("folder"),
		QStringLiteral("visible"), QStringLiteral("autolanes")};
	// muted / solo / mutedBeforeSolo, and their model-id companions (AutomatableModel::saveSettings).
	return rowAttributes.contains(name) || name.startsWith(QStringLiteral("muted"))
		|| name.startsWith(QStringLiteral("solo"));
}

bool isRowChild(const QString& name)
{
	return name == QLatin1String("muted") || name == QLatin1String("solo");
}

//! Makes @a to describe the same row as @a from: its row attributes and row children replace @a to's.
void carryRow(const QDomElement& from, QDomElement& to)
{
	QStringList stale;
	const QDomNamedNodeMap toAttributes = to.attributes();
	for (int i = 0; i < toAttributes.count(); ++i)
	{
		const QString name = toAttributes.item(i).nodeName();
		if (isRowAttribute(name)) { stale << name; }
	}
	for (const QString& name : stale) { to.removeAttribute(name); }
	const QDomNamedNodeMap fromAttributes = from.attributes();
	for (int i = 0; i < fromAttributes.count(); ++i)
	{
		const QDomNode attribute = fromAttributes.item(i);
		if (isRowAttribute(attribute.nodeName())) { to.setAttribute(attribute.nodeName(), attribute.nodeValue()); }
	}
	QDomElement child = to.firstChildElement();
	while (!child.isNull())
	{
		const QDomElement next = child.nextSiblingElement();
		if (isRowChild(child.tagName())) { to.removeChild(child); }
		child = next;
	}
	for (QDomElement each = from.firstChildElement(); !each.isNull(); each = each.nextSiblingElement())
	{
		if (isRowChild(each.tagName())) { to.appendChild(to.ownerDocument().importNode(each, true)); }
	}
}

//! The `<track>` element of a captured document (control::captureTrackXml's shape).
QDomElement trackElementOf(QDomDocument& document, const QString& xml)
{
	if (!document.setContent(xml)) { return QDomElement(); }
	return document.documentElement().firstChildElement(QStringLiteral("track"));
}

//! The row's captured document with its role element replaced by an empty `<instrumenttrack>`: a new
//! instrument track that is the same row. Empty when @a neutralXml does not parse.
QString freshInstrumentRow(const QString& neutralXml)
{
	QDomDocument document;
	QDomElement track = trackElementOf(document, neutralXml);
	if (track.isNull()) { return QString(); }
	track.setAttribute(QStringLiteral("type"), static_cast<int>(Track::Type::Instrument));
	track.removeChild(track.firstChildElement(QStringLiteral("neutraltrack")));
	track.appendChild(document.createElement(QStringLiteral("instrumenttrack")));
	return document.toString(1);
}

//! The kept instrument track, made to describe the row as it is now (renamed, muted, moved into a
//! folder while it was neutral). Empty when either document does not parse.
QString keptInstrumentRow(const QString& neutralXml, const QString& keptXml)
{
	QDomDocument neutralDocument;
	const QDomElement neutral = trackElementOf(neutralDocument, neutralXml);
	QDomDocument document;
	QDomElement track = trackElementOf(document, keptXml);
	if (neutral.isNull() || track.isNull()) { return QString(); }
	carryRow(neutral, track);
	return document.toString(1);
}

//! The row's captured instrument document turned into an empty neutral row with the same row fields.
QString neutralRow(const QString& instrumentXml)
{
	QDomDocument document;
	const QDomElement instrument = trackElementOf(document, instrumentXml);
	if (instrument.isNull()) { return QString(); }
	QDomElement neutral = document.createElement(QStringLiteral("track"));
	carryRow(instrument, neutral);
	neutral.setAttribute(QStringLiteral("type"), static_cast<int>(Track::Type::Neutral));
	neutral.appendChild(document.createElement(QStringLiteral("neutraltrack")));
	document.documentElement().replaceChild(neutral, instrument);
	return document.toString(1);
}

/*! Draws the row @a id at @a index too. The model reorder (moveTrackToIndex) does not move a view, and the
 *  new object's view is built on the next event-loop turn and appended (TrackContainerView's trackAdded
 *  slot is queued), so this is queued behind it on the same editor: it then finds the row's view and moves
 *  it with TrackContainerView::moveTrackView, the product's own drag. The model is already at @a index, so
 *  that call's TrackContainer::moveTrack journals nothing. Headless: there is nothing to draw. */
void placeRowView(int id, int index)
{
	gui::SongEditor* editor = gui::getGUI() == nullptr || gui::getGUI()->songEditor() == nullptr
		? nullptr : gui::getGUI()->songEditor()->m_editor;
	if (editor == nullptr) { return; }
	QMetaObject::invokeMethod(editor, [editor, id, index] {
		for (gui::TrackView* view : editor->trackViews())
		{
			if (view->getTrack() != nullptr && view->getTrack()->id() == id) { editor->moveTrackView(view, index); return; }
		}
	}, Qt::QueuedConnection);
}

/*! Replaces @a live by the track @a xml describes, at @a index: the product's own removal (view
 *  first), then the project loader's Track::create(element), then the reorder the remove-undo uses.
 *  The document is the row's own, so the new object has the row's id. */
Track* replaceRow(Track* live, int index, const QString& xml)
{
	removeTrack(live);
	Track* replacement = restoreTrackFromXml(xml, Engine::getSong());
	if (replacement != nullptr)
	{
		moveTrackToIndex(replacement, index);
		placeRowView(replacement->id(), index);
	}
	return replacement;
}

//! The undo/redo half of a role switch: whatever object is the row now becomes the one @a xml holds.
void replaceRowWithId(int id, int index, const QString& xml)
{
	Track* live = trackWithId(id);
	if (live == nullptr)
	{
		recordActionRestoreFailure(QStringLiteral("track.set_role: the row trk-%1 is gone; nothing was replaced").arg(id));
		return;
	}
	if (replaceRow(live, index, xml) == nullptr)
	{
		recordActionRestoreFailure(QStringLiteral("track.set_role: the row trk-%1 could not be rebuilt").arg(id));
	}
}

bool isLoadableInstrument(const QString& plugin)
{
	const PluginFactory::PluginInfo info = getPluginFactory()->pluginInfo(plugin.toUtf8().constData());
	return !info.isNull() && info.descriptor != nullptr && info.descriptor->type == Plugin::Type::Instrument;
}

//! What a switch needs: the row, where it is, its document now, and the role asked for.
struct RoleRequest
{
	Track* track = nullptr;
	int index = -1;
	QString role;
	QString instrument;
	QString beforeXml;
};

ControlResult refuse(ControlErrorKind kind, const QString& message)
{
	return ControlResult::failure(kind, message);
}

//! Reads and checks the arguments; a refusal changes nothing and records no undo step.
bool readRequest(const QJsonObject& args, RoleRequest* request, ControlResult* error)
{
	request->track = resolveTrack(args.value(QStringLiteral("track")).toString(), error);
	if (request->track == nullptr) { return false; }
	request->role = args.value(QStringLiteral("role")).toString();
	request->instrument = args.value(QStringLiteral("instrument")).toString();
	const Track::Type type = request->track->type();
	if (type != Track::Type::Neutral && type != Track::Type::Instrument)
	{
		*error = refuse(ControlErrorKind::Refused, QStringLiteral("only a neutral row or an instrument track has a "
			"role to switch; this track is a %1 track").arg(trackTypeNameOf(type)));
		return false;
	}
	if (request->role != RoleNeutral && request->role != RoleInstrument)
	{
		*error = refuse(ControlErrorKind::InvalidArgs, QStringLiteral("'role' is 'neutral' or 'instrument'"));
		return false;
	}
	if ((request->role == RoleNeutral) == (type == Track::Type::Neutral))
	{
		*error = refuse(ControlErrorKind::Refused, QStringLiteral("the row already has the %1 role "
			"(plugin.load changes the instrument of an instrument row)").arg(request->role));
		return false;
	}
	request->index = trackIndexIn(Engine::getSong(), request->track);
	if (request->index < 0)
	{
		*error = refuse(ControlErrorKind::NotFound, QStringLiteral("the track is not in the song container"));
		return false;
	}
	return true;
}

//! The checks of a neutral -> instrument switch that depend on the kept role.
bool checkInstrumentChoice(const RoleRequest& request, ControlResult* error)
{
	const auto* neutral = static_cast<const NeutralTrack*>(request.track);
	const QString kept = neutral->keptInstrumentPlugin();
	if (!neutral->keptInstrumentRole().isEmpty())
	{
		if (request.instrument.isEmpty() || request.instrument == kept) { return true; }
		*error = refuse(ControlErrorKind::InvalidArgs, QStringLiteral("this row keeps its instrument role (%1); "
			"omit 'instrument' to give it back, then change it with plugin.load").arg(kept));
		return false;
	}
	if (request.instrument.isEmpty())
	{
		*error = refuse(ControlErrorKind::InvalidArgs, QStringLiteral("name the 'instrument' plug-in this row should play"));
		return false;
	}
	if (!isLoadableInstrument(request.instrument))
	{
		*error = refuse(ControlErrorKind::InvalidArgs,
			QStringLiteral("'%1' is not an instrument plug-in of this build").arg(request.instrument));
		return false;
	}
	return true;
}

//! neutral -> instrument. The row's own name is kept: loading a plug-in names the track after it.
Track* switchToInstrument(const RoleRequest& request)
{
	auto* neutral = static_cast<NeutralTrack*>(request.track);
	const QString kept = neutral->keptInstrumentRole();
	const QString rowName = neutral->name();
	const QString target = kept.isEmpty() ? freshInstrumentRow(request.beforeXml)
		: keptInstrumentRow(request.beforeXml, kept);
	if (target.isEmpty()) { return nullptr; }
	Track* replacement = replaceRow(request.track, request.index, target);
	auto* instrumentTrack = dynamic_cast<InstrumentTrack*>(replacement);
	if (instrumentTrack != nullptr && kept.isEmpty())
	{
		instrumentTrack->loadInstrument(request.instrument);
		instrumentTrack->setName(rowName);
	}
	return replacement;
}

//! instrument -> neutral: the neutral row keeps the whole instrument track it was.
Track* switchToNeutral(const RoleRequest& request)
{
	const QString target = neutralRow(request.beforeXml);
	if (target.isEmpty()) { return nullptr; }
	Track* replacement = replaceRow(request.track, request.index, target);
	if (auto* neutral = dynamic_cast<NeutralTrack*>(replacement)) { neutral->setKeptInstrumentRole(request.beforeXml); }
	return replacement;
}

QString mechanismText()
{
	return QStringLiteral("action checkpoint (structural step): the row's own XML is captured before and after "
		"the switch, and the one recorded step replaces the row by the other document at the same index "
		"through control::removeTrack and Track::create(element) - the remove-undo path. The row's id, name, "
		"mute/solo, colour, height and folder are in both documents, and the instrument role (plug-in, "
		"settings, clips) is the kept role of the neutral document, so neither direction loses it");
}

ControlResult setRole(const QJsonObject& args)
{
	RoleRequest request;
	ControlResult error;
	if (!readRequest(args, &request, &error)) { return error; }
	const bool toInstrument = request.role == RoleInstrument;
	if (toInstrument && !checkInstrumentChoice(request, &error)) { return error; }
	// The before-document is the kept role of an instrument -> neutral switch, so it is bounded at the
	// kept-role bound there; a neutral row's document may carry a kept role, hence the larger bound.
	if (!captureTrackXml(request.track, &request.beforeXml, toInstrument ? MaxRoleSwitchChars : MaxKeptRoleChars))
	{
		return refuse(ControlErrorKind::Refused, QStringLiteral("this track's serialized state is larger than %1 "
			"characters, so its role cannot be kept; nothing was changed").arg(MaxKeptRoleChars));
	}
	const int id = request.track->id();
	const QJsonObject before = trackEditState(request.track, request.index);
	Track* replacement = toInstrument ? switchToInstrument(request) : switchToNeutral(request);
	if (replacement == nullptr)
	{
		return refuse(ControlErrorKind::Refused, QStringLiteral("the row's document could not be rebuilt"));
	}
	QString afterXml;
	const bool captured = captureTrackXml(replacement, &afterXml, MaxRoleSwitchChars);
	const bool recorded = captured && addStructuralUndoStep(
		[id, index = request.index, xml = request.beforeXml]() { replaceRowWithId(id, index, xml); },
		[id, index = request.index, xml = afterXml]() { replaceRowWithId(id, index, xml); },
		static_cast<qint64>(request.beforeXml.size() + afterXml.size()) * static_cast<qint64>(sizeof(QChar)));

	QJsonObject result = trackEditState(replacement, request.index);
	result.insert(QStringLiteral("track"), trackIdOf(replacement));
	insertTrackRoleFields(&result, replacement);
	result.insert(QStringLiteral("__transaction"), transactionPayload(before,
		recorded ? QStringLiteral("replace the row by its captured document from before the switch")
			: QStringLiteral("none: no undo step could be recorded"),
		QJsonObject(), recorded, recorded ? mechanismText()
			: QStringLiteral("snapshot only: the journal is off or the switched row is too large to capture")));
	return ControlResult::success(result);
}

} // namespace

namespace control
{

void insertTrackRoleFields(QJsonObject* entry, const Track* track)
{
	if (track == nullptr) { return; }
	if (track->type() == Track::Type::Instrument) { entry->insert(QStringLiteral("role"), RoleInstrument); }
	const auto* neutral = dynamic_cast<const NeutralTrack*>(track);
	if (neutral == nullptr) { return; }
	entry->insert(QStringLiteral("role"), RoleNeutral);
	QJsonArray kept;
	if (!neutral->keptInstrumentRole().isEmpty())
	{
		kept.append(QJsonObject{{QStringLiteral("role"), RoleInstrument},
			{QStringLiteral("instrument"), neutral->keptInstrumentPlugin()}});
	}
	entry->insert(QStringLiteral("kept_roles"), kept);
}

} // namespace control

void registerTrackRoleCommands(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("track.set_role");
	cmd.group = QStringLiteral("track");
	cmd.verb = QStringLiteral("set_role");
	cmd.description = QStringLiteral("Give a neutral row an instrument (role 'instrument', naming the "
		"`instrument` plug-in), or switch an instrument track back to a neutral row (role 'neutral'). The row "
		"keeps its trk-<n> id, its position, name, mute/solo, colour and folder, and nothing else in the "
		"project changes. Switching to neutral KEEPS the instrument role - plug-in, settings and clips - with "
		"the row (track.get_state `kept_roles`, saved with the project); switching that row to 'instrument' "
		"again gives it back (omit `instrument`). One undo step.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("track"), stringProperty()},
		{QStringLiteral("role"), enumProperty({RoleNeutral, RoleInstrument})},
		{QStringLiteral("instrument"), stringProperty()},
	}, {QStringLiteral("track"), QStringLiteral("role")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("track"), stringProperty()},
		{QStringLiteral("id"), stringProperty()},
		{QStringLiteral("index"), integerProperty()},
		{QStringLiteral("name"), stringProperty()},
		{QStringLiteral("type"), stringProperty()},
		{QStringLiteral("role"), stringProperty()},
		{QStringLiteral("kept_roles"), arrayProperty()},
		{QStringLiteral("clip_count"), numberProperty()},
		{QStringLiteral("muted"), booleanProperty()},
		{QStringLiteral("soloed"), booleanProperty()},
		{QStringLiteral("folder"), stringProperty()},
		{QStringLiteral("visible"), booleanProperty()},
	});
	cmd.mutating = true;
	cmd.handler = setRole;
	registry.registerCommand(cmd);
}

} // namespace lmms
