/*
 * LibraryCommandMenus.cpp - the project and library entries the 0.5.0 L11/L12 work adds to the shell's menus
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

#include "LibraryCommandMenus.h"

#include <QActionGroup>
#include <QFileInfo>
#include <QJsonArray>
#include <QMenu>
#include <QPointer>
#include <QTimer>

#include "Clip.h"
#include "CommandLists.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "EditLocks.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "MainWindow.h"
#include "ProjectInfo.h"
#include "ReferenceProject.h"
#include "ReferenceProjectPanel.h"
#include "Song.h"
#include "Track.h"
#include "TrackSpacers.h"

namespace lmms::gui::LibraryCommandMenus
{

namespace
{

void syncDocumentTypeView();

QString tr(const char* text)
{
	return QObject::tr(text);
}

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! An entry running @a id with @a fixed: at once, or through its form (pre-filled by @a defaults(), read when triggered).
QAction* entry(QMenu* menu, const char* label, const char* id, const QJsonObject& fixed, bool ask,
	std::function<QJsonObject()> defaults = {}, bool showResult = false)
{
	const QString title = tr(label).remove(QStringLiteral("..."));
	return addCommandAction(menu, tr(label), QLatin1String(id), [id, fixed, ask, defaults, showResult, title] {
		return CommandCall{QLatin1String(id), fixed, defaults ? defaults() : QJsonObject(), title, ask, showResult};
	});
}

//! An entry opening the list surface @a make builds.
void list(QMenu* menu, const char* label, std::function<ListSpec()> make)
{
	QAction* action = menu->addAction(tr(label));
	action->setProperty("controlCommand", make().listId);
	QObject::connect(action, &QAction::triggered, menu, [menu, make = std::move(make)] {
		QWidget* host = menu;
		while (qobject_cast<QMenu*>(host) != nullptr) { host = host->parentWidget(); }
		showCommandList(host, make());
	});
}

//! What a reporting command answers with no arguments, minus @a dropped: a form's starting values.
std::function<QJsonObject()> current(const char* id, const QString& dropped = {})
{
	return [id, dropped] {
		QJsonObject values = run(QLatin1String(id)).result;
		values.remove(dropped);
		return values;
	};
}

void documentTypeMenu(QMenu* menu)
{
	QMenu* types = menu->addMenu(tr("Document Type"));
	// A document has ONE type, so the items are exclusive: choosing one unticks the one it replaces,
	// and choosing the type that is already chosen leaves it ticked instead of clearing it.
	auto* exclusive = new QActionGroup(types);
	exclusive->setExclusive(true);
	const QString now = ProjectInfo::instance().documentType();
	for (const QString& type : ProjectInfo::documentTypes())
	{
		QAction* action = types->addAction(type);
		action->setCheckable(true);
		action->setChecked(type == now);
		exclusive->addAction(action);
		action->setProperty("controlCommand", QStringLiteral("project.settings"));
		QObject::connect(action, &QAction::triggered, menu, [type] {
			if (run(QStringLiteral("project.settings"), {{QStringLiteral("document_type"), type}}).ok) { syncDocumentTypeView(); }
		});
	}
}

//! The widget a menu's dialogs belong to: the first ancestor that is not a menu.
QWidget* hostOf(QMenu* menu)
{
	QWidget* host = menu;
	while (qobject_cast<QMenu*>(host) != nullptr) { host = host->parentWidget(); }
	return host;
}

//! Whether the session may be replaced: the main window asks to save it; with no window (a test) yes.
bool mayLeaveSession()
{
	return getGUI() == nullptr || getGUI()->mainWindow() == nullptr || getGUI()->mainWindow()->mayChangeProject(true);
}

//! An entry that runs @a id with @a args after the session may be left (project.new_document, project.switch).
void leavingEntry(QMenu* menu, const QString& label, const QString& id, const QJsonObject& args)
{
	QAction* action = menu->addAction(label);
	action->setProperty("controlCommand", id);
	QObject::connect(action, &QAction::triggered, menu, [menu, id, args] {
		if (mayLeaveSession()) { runCommand(hostOf(menu), CommandCall{id, args, {}, QString(), false}); }
	});
}

//! WF-048: a new document of each type, from the type's template.
void newDocumentMenu(QMenu* menu)
{
	QMenu* documents = menu->addMenu(tr("New Document"));
	for (const QString& type : ProjectInfo::documentTypes())
	{
		leavingEntry(documents, documentTypeLabel(type), QStringLiteral("project.new_document"), {{QStringLiteral("type"), type}});
	}
}

//! An entry opening the reference panel through @a open (which shows it, after a file dialog or at once).
void referenceEntry(QMenu* menu, const char* label, const char* id, bool (*open)(QWidget*))
{
	QAction* action = menu->addAction(tr(label));
	action->setProperty("controlCommand", QLatin1String(id));
	QObject::connect(action, &QAction::triggered, menu, [menu, open] { open(hostOf(menu)); });
}

//! WF-042: other projects - the reference beside the session, switching between recent projects, pasting tracks.
void otherProjectsMenu(QMenu* menu)
{
	QMenu* other = menu->addMenu(tr("Other Projects"));
	referenceEntry(other, "Open as Reference...", "reference.open", ReferenceProjectPanel::openWithDialog);
	referenceEntry(other, "Reference Project...", "reference.get_state", [](QWidget* host) { return ReferenceProjectPanel::showPanel(host) != nullptr; });
	other->addSeparator();
	const QJsonObject keep{{QStringLiteral("save_current"), false}};
	leavingEntry(other, tr("Switch to Last Project"), QStringLiteral("project.switch"), keep);
	QJsonObject keepReference = keep;
	keepReference.insert(QStringLiteral("keep_as_reference"), true);
	leavingEntry(other, tr("Switch to Last Project, Keeping This One as Reference"), QStringLiteral("project.switch"), keepReference);
	QMenu* switchTo = other->addMenu(tr("Switch To"));
	for (const QJsonValue& value : run(QStringLiteral("project.recent")).result.value(QStringLiteral("projects")).toArray())
	{
		const QJsonObject project = value.toObject();
		QJsonObject args = keep;
		args.insert(QStringLiteral("path"), project.value(QStringLiteral("path")));
		leavingEntry(switchTo, project.value(QStringLiteral("name")).toString(), QStringLiteral("project.switch"), args);
	}
	switchTo->setEnabled(!switchTo->isEmpty());
	other->addSeparator();
	entry(other, "Paste Tracks From Another Project", "project.paste_tracks", {}, false);  // WF-042, between instances
}

void snapshotMenu(QMenu* menu)
{
	QMenu* snapshots = menu->addMenu(tr("Mixer Snapshots"));  // MX-170
	entry(snapshots, "Store Snapshot...", "mixer.snapshot_store", {}, true);
	QMenu* recall = snapshots->addMenu(tr("Recall"));
	for (const QJsonValue& value : run(QStringLiteral("mixer.snapshot_list")).result.value(QStringLiteral("snapshots")).toArray())
	{
		const QString name = value.toObject().value(QStringLiteral("name")).toString();
		addCommandAction(recall, name, QStringLiteral("mixer.snapshot_recall"), [name] {
			return CommandCall{QStringLiteral("mixer.snapshot_recall"), {{QStringLiteral("name"), name}}, {}, QString(), false};
		});
	}
	recall->setEnabled(!recall->isEmpty());
	list(snapshots, "Snapshots...", [] { return CommandLists::mixerSnapshots(); });
}

//! WF-051: syncing with other apps and machines - the Ableton Link session and MIDI clock.
void syncMenu(QMenu* menu)
{
	QMenu* sync = menu->addMenu(tr("Sync"));
	addCommandChoices(sync, tr("Ableton Link"), QStringLiteral("link.set_enabled"), QStringLiteral("enabled"),
		[] { return CommandCall{QStringLiteral("link.set_enabled"), {}, {}, QString(), false}; });
	addCommandChoices(sync, tr("Share Start and Stop"), QStringLiteral("link.set_start_stop_sync"), QStringLiteral("enabled"),
		[] { return CommandCall{QStringLiteral("link.set_start_stop_sync"), {}, {}, QString(), false}; });
	entry(sync, "Session Tempo...", "link.set_session_tempo", {}, true);
	entry(sync, "Quantum...", "link.set_quantum", {}, true);
	entry(sync, "Session Status", "link.get_state", {}, false, {}, true);
	sync->addSeparator();
	// Q050-024: the PCM stream between instances - not Link sync (docs/LINK-AUDIO.md).
	addCommandChoices(sync, tr("Link Audio Stream"), QStringLiteral("link.audio_set_enabled"), QStringLiteral("enabled"),
		[] { return CommandCall{QStringLiteral("link.audio_set_enabled"), {}, {}, QString(), false}; });
	entry(sync, "Link Audio Peer...", "link.audio_set_enabled", {}, true);
	entry(sync, "Link Audio Status", "link.audio_get_state", {}, false, {}, true);
	sync->addSeparator();
	sync->addSeparator();
	entry(sync, "Send MIDI Clock...", "clock.master_set", {}, true);
	entry(sync, "Follow MIDI Clock...", "clock.slave_set", {}, true);
	entry(sync, "Clock Status", "clock.get_state", {}, false, {}, true);
}

} // namespace

void populateProject(QMenu* menu)
{
	menu->addSeparator();
	entry(menu, "Project Settings...", "project.settings", {}, true, current("project.settings", QStringLiteral("file")));  // WF-043
	entry(menu, "Project Info...", "project.metadata", {}, true, [] {  // WF-047
		return run(QStringLiteral("project.metadata")).result.value(QStringLiteral("metadata")).toObject();
	});
	documentTypeMenu(menu);  // WF-048
	newDocumentMenu(menu);  // WF-048: the type's template
	snapshotMenu(menu);
	QMenu* locks = menu->addMenu(tr("Locks"));  // WF-044
	list(locks, "Locked Tracks...", [] { return CommandLists::lockedTracks(); });
	list(locks, "Locked Clips...", [] { return CommandLists::lockedClips(); });
	list(menu, "Render Queue...", [] { return CommandLists::renderQueue(); });  // WF-040
	list(menu, "Background Tasks...", [] { return CommandLists::backgroundTasks(); });  // WF-046
	otherProjectsMenu(menu);  // WF-042
	QMenu* family = menu->addMenu(tr("Alternatives and Subprojects"));
	entry(family, "Keep as an Alternative...", "project.alternative_save", {}, true);  // WF-032
	list(family, "Alternatives...", [] { return CommandLists::alternatives(); });
	entry(family, "Add Subproject...", "project.subproject_add", {}, true);  // WF-045
	list(family, "Subprojects...", [] { return CommandLists::subprojects(); });
	syncMenu(menu);  // WF-051
}

void populateTrack(QMenu* menu, Track* track)
{
	if (track == nullptr) { return; }
	const QString id = control::trackIdOf(track);
	const bool locked = EditLocks::instance().trackLocked(track->id());
	entry(menu, locked ? "Unlock Track" : "Lock Track", "track.set_locked", {{QStringLiteral("track"), id}, {QStringLiteral("locked"), !locked}}, false);
	entry(menu, "Flatten Track (Replace With Its Audio)", "track.flatten", {{QStringLiteral("track"), id}}, false);  // ED-207
	entry(menu, "Copy to Another Project", "project.copy_tracks", {{QStringLiteral("tracks"), QJsonArray{id}}}, false);  // WF-042
	const TrackSpacers::Spacer spacer = TrackSpacers::instance().spacer(track->id());  // WF-107
	entry(menu, spacer.height > 0 ? "Edit Spacer Below..." : "Add Spacer Below...", "track.set_spacer", {{QStringLiteral("track"), id}}, true,
		[spacer] { return QJsonObject{{QStringLiteral("height"), spacer.height > 0 ? spacer.height : 24}, {QStringLiteral("label"), spacer.label}}; });
	if (spacer.height > 0)
	{
		entry(menu, "Remove Spacer Below", "track.set_spacer", {{QStringLiteral("track"), id}, {QStringLiteral("height"), 0}}, false);
	}
}

void populateClip(QMenu* menu, Clip* clip)
{
	if (clip == nullptr) { return; }
	const QString id = control::clipIdOf(clip);
	const bool locked = EditLocks::instance().clipLocked(clip->id());
	entry(menu, locked ? "Unlock Clip" : "Lock Clip", "clip.set_locked", {{QStringLiteral("clip"), id}, {QStringLiteral("locked"), !locked}}, false);
	const QString name = clip->name();
	entry(menu, "Save to Library...", "browser.save_clip", {{QStringLiteral("clip"), id}}, true,  // WF-022
		[name] { return QJsonObject{{QStringLiteral("name"), name}}; }, true);
}

void applyDocumentType(const QString& type)
{
	run(QStringLiteral("window.set_view"), {{QStringLiteral("view"), type == QLatin1String("mastering") ? QStringLiteral("mix") : QStringLiteral("arrange")}});
	if (type != QLatin1String("live")) { return; }
	for (const QJsonValue& value : run(QStringLiteral("window.get_state")).result.value(QStringLiteral("editors")).toArray())
	{
		const QJsonObject editor = value.toObject();
		if (editor.value(QStringLiteral("editor")).toString() == QLatin1String("clip_launcher") && !editor.value(QStringLiteral("visible")).toBool())
		{
			run(QStringLiteral("window.toggle"), {{QStringLiteral("editor"), QStringLiteral("clip_launcher")}});
		}
	}
}

QString documentTypeLabel(const QString& type)
{
	if (type == QLatin1String("mastering")) { return tr("Mastering"); }
	if (type == QLatin1String("live")) { return tr("Live"); }
	return tr("Production");
}

QString taggedTitle(const QString& title, const QString& type)
{
	QString base = title;
	for (const QString& each : ProjectInfo::documentTypes())
	{
		const QString tag = QStringLiteral("[%1] ").arg(documentTypeLabel(each));
		if (base.startsWith(tag)) { base.remove(0, tag.size()); }
	}
	// Production is the default document: only the other types are named in the title.
	if (type == ProjectInfo::documentTypes().first()) { return base; }
	return QStringLiteral("[%1] %2").arg(documentTypeLabel(type), base);
}

namespace
{

//! The main window whose title shows the document type, once it is known; null again when it goes.
QPointer<MainWindow>& titledWindow()
{
	static QPointer<MainWindow> window;
	return window;
}

//! WF-048: the window title leads with the document type, whatever MainWindow::resetWindowTitle wrote.
void tagTitle(MainWindow* window)
{
	const QString tagged = taggedTitle(window->windowTitle(), ProjectInfo::instance().documentType());
	if (window->windowTitle() != tagged) { window->setWindowTitle(tagged); }
}

void syncDocumentTypeView()
{
	applyDocumentType(ProjectInfo::instance().documentType());
}

//! Once the main window exists: keep its title tagged from now on.
void ensureTitleTagging()
{
	static bool connected = false;
	MainWindow* window = connected || getGUI() == nullptr ? nullptr : getGUI()->mainWindow();
	if (window == nullptr) { return; }
	connected = true;
	titledWindow() = window;
	QObject::connect(window, &QWidget::windowTitleChanged, window, [window] { tagTitle(window); });
	tagTitle(window);
}

void documentChanged(ProjectInfo::Change)
{
	syncDocumentTypeView();
	ensureTitleTagging();
	if (!titledWindow().isNull()) { titledWindow()->resetWindowTitle(); }
}

} // namespace

void install()
{
	static bool installed = false;
	Song* song = Engine::getSong();
	if (installed || song == nullptr) { return; }
	installed = true;
	QObject::connect(song, &Song::projectLoaded, song, [] {
		syncDocumentTypeView();
	});
	ProjectInfo::setChangeHook(documentChanged);  // WF-048: the type in the title, a new document's view
	ReferenceProject::setChangeHook(ReferenceProjectPanel::refreshIfOpen);  // WF-042
	QTimer::singleShot(0, song, [] { ensureTitleTagging(); });  // the main window is built after this runs
}

} // namespace lmms::gui::LibraryCommandMenus
