/*
 * DevicePanelTest.cpp - the shell selection, the Inspector and the Device panel over a real engine
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

/*! The 0.5.0 shell's selection and the two panels that follow it, over a real engine with the
 *  build's own plugin modules (LMMS_TEST_PLUGIN_DIR):
 *  - ShellSelection: a clip selects its track, a channel clears both, a deleted track reads as
 *    nothing selected;
 *  - the Inspector edits through the registered commands (track.rename, track.set_mute), so one
 *    control.undo takes an edit back;
 *  - the Device panel draws a track's instrument and devices and a channel's inserts with the
 *    modulator that drives one of them, its cards' header buttons dispatch plugin.bypass and
 *    plugin.unload, and a device removed from anywhere takes its card with it at once - before
 *    the device is deleted, so no control paints a deleted parameter.
 *  - 0.5.0 L10 (W3): the channel's level beside its heading, a parameter's alias on its card, the
 *    card's delta and fold buttons.
 *  - 0.5.0 theme 12: cards update in place (a device added keeps the other cards), each card says its state in
 *    words (bypassed, A/B) and has one action menu of registered commands; the device picker searches and words
 *    each row's state.
 *  - 0.5.0 theme 13: the Inspector keeps an unchanged section (and its focus) when the selection moves, says
 *    "Multiple values" for notes that differ and gives a typed value to all of them, reads velocities in %, and
 *    edits a channel's name in place (mixer.rename_channel).
 *  The device half requires the build's amplifier and triple oscillator modules. */

#include <QtTest>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

#include <QApplication>
#include <QContextMenuEvent>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QMenu>
#include <QTimer>
#include <QToolTip>
#include <QToolButton>

#include "CommandForm.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "DevicePanel.h"
#include "DevicePicker.h"
#include "Effect.h"
#include "EffectChain.h"
#include "EffectControls.h"
#include "Engine.h"
#include "InspectorPanel.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "MusicalTime.h"
#include "MenuTestSupport.h"
#include "Mixer.h"
#include "Model.h"
#include "RemoteControls.h"
#include "SampleTrack.h"
#include "ChannelLevelMeter.h"
#include "CommandLists.h"
#include "ShellCommandMenus.h"
#include "ShellSelection.h"
#include "TransportReadouts.h"
#include "Song.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = QJsonObject{})
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! Load the first effect this host can load onto @a target; its fx-<n>, or empty.
QString loadAnEffect(const QString& target)
{
	for (const QJsonValue& device : run(QStringLiteral("plugin.list"), {{QStringLiteral("kind"), QStringLiteral("effect")},
		{QStringLiteral("loadable_only"), true}}).result.value(QStringLiteral("devices")).toArray())
	{
		const ControlResult loaded = run(QStringLiteral("plugin.load"), {{QStringLiteral("target"), target},
			{QStringLiteral("device"), device.toObject().value(QStringLiteral("id")).toString()}});
		if (loaded.ok) { return loaded.result.value(QStringLiteral("id")).toString(); }
	}
	return QString();
}

QJsonArray cards(const DevicePanel& panel) { return panel.describe().value(QStringLiteral("cards")).toArray(); }

//! The @a T children that show: a rebuild hides the widgets it drops at once and deletes them later.
template <class T>
QList<T*> shownChildren(QWidget* parent)
{
	QList<T*> shown = parent->findChildren<T*>();
	shown.erase(std::remove_if(shown.begin(), shown.end(), [parent](const T* child) { return !child->isVisibleTo(parent); }), shown.end());
	return shown;
}

template <class T>
T* childNamed(QWidget* parent, const QString& accessibleName)
{
	for (T* child : parent->findChildren<T*>())
	{
		if (child->accessibleName() == accessibleName && child->isVisibleTo(parent)) { return child; }
	}
	return nullptr;
}

//! Every leaf entry of @a menu names a registered command (a renamed command fails here).
bool allDeclareRegisteredCommands(QMenu* menu, QString* stray)
{
	for (QAction* action : lmms::test::leafActions(menu))
	{
		const QString id = action->property("controlCommand").toString();
		if (!ControlRegistry::instance()->hasCommand(id)) { *stray = action->text() + QStringLiteral(" -> ") + id; return false; }
	}
	return true;
}

ReadoutField* selectedNoteLength(InspectorPanel& inspector)
{
	for (ReadoutField* candidate : inspector.findChildren<ReadoutField*>())
	{
		if (candidate->caption() == QStringLiteral("Length")
			&& candidate->parentWidget()->objectName() == QStringLiteral("inspectorNotes")
			&& candidate->isVisibleTo(&inspector)) { return candidate; }
	}
	return nullptr;
}

bool typeReadout(ReadoutField* field, const QString& text)
{
	if (field == nullptr) { return false; }
	QTest::mouseDClick(field, Qt::LeftButton);
	auto* editor = field->findChild<QLineEdit*>();
	if (editor == nullptr) { return false; }
	editor->selectAll();
	QTest::keyClicks(editor, text);
	emit editor->editingFinished();
	return true;
}

struct RestoreMusicalTimeFormat
{
	musicaltime::Format format = musicaltime::currentFormat();
	~RestoreMusicalTimeFormat() { musicaltime::setFormat(format); }
};

struct ClearInspectorTrack
{
	Track* track;
	~ClearInspectorTrack()
	{
		ShellSelection::instance()->setTrack(nullptr);
		delete track;
	}
};

//! The menu a right-click opened: the visible QMenu among the application's widgets (the offscreen
//! platform has no active popup widget, so the menu is found by what it shows).
QMenu* visibleMenu()
{
	for (QWidget* widget : QApplication::allWidgets())
	{
		if (auto* menu = qobject_cast<QMenu*>(widget); menu != nullptr && menu->isVisible()) { return menu; }
	}
	return nullptr;
}

//! A device whose controls have no view of their own: the card's fallback editor cannot be created.
class NoViewEffectControls final : public EffectControls
{
public:
	explicit NoViewEffectControls(Effect* effect) : EffectControls{effect} {}
	int controlCount() override { return 0; }
	void saveSettings(QDomDocument&, QDomElement&) override {}
	void loadSettings(const QDomElement&) override {}
	QString nodeName() const override { return QStringLiteral("NoViewEffectControls"); }
	gui::EffectControlDialog* createView() override { return nullptr; }
};

//! The test device: a plug-in with no editor of its own and no control view either - so the card's
//! "Open Editor" has nothing to open, neither the device's own window nor the built-in dialog.
class NoViewEffect final : public Effect
{
public:
	explicit NoViewEffect(Model* parent) : Effect{&s_descriptor, parent, nullptr}, m_controls{this} {}
	EffectControls* controls() override { return &m_controls; }

protected:
	ProcessStatus processImpl(SampleFrame*, const f_cnt_t) override { return ProcessStatus::Continue; }

private:
	NoViewEffectControls m_controls;
	static const Plugin::Descriptor s_descriptor;
};

const Plugin::Descriptor NoViewEffect::s_descriptor{
	"zene-no-view-test-effect", "No view test effect", "", "Zene", 0x0100,
	Plugin::Type::Effect, nullptr, nullptr, nullptr};

} // namespace

using lmms::test::menuAction;

class DevicePanelTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ShellSelection::instance()->setTrack(nullptr);
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void theSelectionKeepsATrackAClipOrAChannel()
	{
		ShellSelection* selection = ShellSelection::instance();
		QSignalSpy changed(selection, &ShellSelection::changed);
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		QVERIFY(track != nullptr);
		Clip* clip = track->createClip(TimePos(0));
		selection->setClip(clip);
		QCOMPARE(selection->track(), track);
		QCOMPARE(selection->clip(), clip);
		selection->setTrack(track);  // the clip is on it: it stays
		QCOMPARE(selection->clip(), clip);
		QCOMPARE(changed.count(), 1);
		selection->setChannel(QStringLiteral("ch-1"));
		QCOMPARE(selection->track(), static_cast<Track*>(nullptr));
		QCOMPARE(selection->clip(), static_cast<Clip*>(nullptr));
		QCOMPARE(selection->channel(), QStringLiteral("ch-1"));
		selection->setTrack(track);
		QVERIFY(selection->channel().isEmpty());
		delete track;
		QCOMPARE(selection->track(), static_cast<Track*>(nullptr));
	}

	void deletingSelectedTrackNotifiesListeners()
	{
		ShellSelection* selection = ShellSelection::instance();
		auto* oldTrack = Track::create(Track::Type::Instrument, Engine::getSong());
		auto* selectedTrack = Track::create(Track::Type::Instrument, Engine::getSong());
		Clip* selectedClip = selectedTrack->createClip(TimePos(0));
		selection->setTrack(oldTrack);
		selection->setClip(selectedClip);
		QSignalSpy changed(selection, &ShellSelection::changed);

		delete oldTrack;
		QCOMPARE(changed.count(), 0);
		QCOMPARE(selection->track(), selectedTrack);
		QCOMPARE(selection->clip(), selectedClip);

		delete selectedTrack;
		QCOMPARE(changed.count(), 1);
		QCOMPARE(selection->track(), static_cast<Track*>(nullptr));
		QVERIFY(selection->clip() == nullptr);
	}

	void deletingSelectedClipNotifiesListeners()
	{
		ShellSelection* selection = ShellSelection::instance();
		auto* track = Track::create(Track::Type::Instrument, Engine::getSong());
		Clip* oldClip = track->createClip(TimePos(0));
		Clip* selectedClip = track->createClip(TimePos(1));
		selection->setClip(oldClip);
		selection->setClip(selectedClip);
		QSignalSpy changed(selection, &ShellSelection::changed);

		delete oldClip;
		QCOMPARE(changed.count(), 0);
		QCOMPARE(selection->clip(), selectedClip);

		delete selectedClip;
		QCOMPARE(changed.count(), 1);
		QCOMPARE(selection->track(), track);
		QVERIFY(selection->clip() == nullptr);
		delete track;
		QCOMPARE(changed.count(), 2);
		QCOMPARE(selection->track(), static_cast<Track*>(nullptr));
	}

	void theInspectorEditsThroughTheCommands()
	{
		auto* track = Track::create(Track::Type::Instrument, Engine::getSong());
		track->setName(QStringLiteral("Lead"));
		const QString id = control::trackIdOf(track);
		InspectorPanel inspector;
		inspector.show();
		ShellSelection::instance()->setTrack(track);
		inspector.rebuild();
		QCoreApplication::processEvents();  // a layout shows a widget added to a shown parent on the next turn
		QCOMPARE(inspector.describe().value(QStringLiteral("track")).toString(), id);

		auto* name = childNamed<QLineEdit>(&inspector, QStringLiteral("Track name"));
		QVERIFY(name != nullptr);
		QCOMPARE(name->text(), QStringLiteral("Lead"));
		name->setText(QStringLiteral("Bass"));
		emit name->editingFinished();
		QCOMPARE(track->name(), QStringLiteral("Bass"));

		auto* mute = childNamed<QToolButton>(&inspector, QStringLiteral("Mute the track"));
		QVERIFY(mute != nullptr);
		mute->click();
		QVERIFY(track->isMuted());
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(!track->isMuted());

		// Solo is track.set_solo with its `solo` argument (the Inspector once sent `soloed`, which
		// the schema refuses: every click did nothing).
		auto* solo = childNamed<QToolButton>(&inspector, QStringLiteral("Solo the track"));
		QVERIFY(solo != nullptr);
		solo->click();
		QVERIFY(track->isSolo());
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(!track->isSolo());
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(track->name(), QStringLiteral("Lead"));
		ShellSelection::instance()->setTrack(nullptr);
		delete track;
	}

	/*! The Inspector's colour swatch follows the track's colour: it was built from the colour the track had
	 *  when the section was made and never learned another, so a track.set_color from the header, a script
	 *  or an undo left the Inspector showing the old colour with no selection change to redraw it. */
	void theTrackColourSwatchFollowsARecolour()
	{
		auto* track = Track::create(Track::Type::Instrument, Engine::getSong());
		track->setName(QStringLiteral("Swatch"));
		InspectorPanel inspector;
		inspector.show();
		ShellSelection::instance()->setTrack(track);
		inspector.rebuild();
		QCoreApplication::processEvents();

		auto* swatch = inspector.findChild<QWidget*>(QStringLiteral("inspectorSwatch"));
		QVERIFY(swatch != nullptr);
		QVERIFY2(swatch->property("color").value<QColor>().isValid(), "the swatch shows a colour to begin with");

		// The colour changed with the selection untouched: nothing here rebuilds the Inspector.
		QVERIFY(run(QStringLiteral("track.set_color"), {{QStringLiteral("track"), control::trackIdOf(track)},
			{QStringLiteral("color"), QStringLiteral("#ff0000")}}).ok);
		QCOMPARE(track->color().value_or(Qt::white), QColor(QStringLiteral("#ff0000")));
		QCOMPARE(swatch->property("color").value<QColor>(), QColor(QStringLiteral("#ff0000")));

		QVERIFY(run(QStringLiteral("track.set_color"), {{QStringLiteral("track"), control::trackIdOf(track)},
			{QStringLiteral("color"), QStringLiteral("#00ff00")}}).ok);
		QCOMPARE(swatch->property("color").value<QColor>(), QColor(QStringLiteral("#00ff00")));
		ShellSelection::instance()->setTrack(nullptr);
		delete track;
	}

	/*! 0.5.0 W1: the track, clip, channel and project menus (the Inspector's More buttons, the
	 *  track header's and the mixer strip's More submenus) offer the socket-only capabilities
	 *  where they belong, every entry a registered command. */
	void theSectionMenusOfferTheW1Commands()
	{
		auto* instrument = Track::create(Track::Type::Instrument, Engine::getSong());
		dynamic_cast<InstrumentTrack*>(instrument)->loadInstrument(QStringLiteral("tripleoscillator"));
		auto* sample = Track::create(Track::Type::Sample, Engine::getSong());
		Clip* midiClip = instrument->createClip(TimePos(0));
		Clip* sampleClip = sample->createClip(TimePos(0));
		const QString channel = run(QStringLiteral("mixer.add_channel")).result.value(QStringLiteral("channel")).toString();
		QString stray;

		QMenu track;
		ShellCommandMenus::populateTrack(&track, instrument);
		QVERIFY2(allDeclareRegisteredCommands(&track, &stray), qPrintable(stray));
		QVERIFY(menuAction(&track, {QStringLiteral("Record"), QStringLiteral("Arm")}) != nullptr);
		QVERIFY(menuAction(&track, {QStringLiteral("Retrospective Capture"), QStringLiteral("Last MIDI to a New Clip")}) != nullptr);
		QVERIFY(menuAction(&track, {QStringLiteral("Render"), QStringLiteral("Freeze")}) != nullptr);
		QVERIFY2(menuAction(&track, {QStringLiteral("Insert Test Signal...")}) == nullptr, "an instrument track offered a test signal clip");
		QMenu sampleTrack;
		ShellCommandMenus::populateTrack(&sampleTrack, sample);
		QVERIFY(menuAction(&sampleTrack, {QStringLiteral("Insert Test Signal...")}) != nullptr);

		QMenu midi;
		ShellCommandMenus::populateClip(&midi, midiClip);
		QVERIFY2(allDeclareRegisteredCommands(&midi, &stray), qPrintable(stray));
		QVERIFY(menuAction(&midi, {QStringLiteral("Scale and Chords"), QStringLiteral("Detect Chords")}) != nullptr);
		QVERIFY(menuAction(&midi, {QStringLiteral("Groove"), QStringLiteral("Random Seed...")}) != nullptr);
		QMenu audio;
		ShellCommandMenus::populateClip(&audio, sampleClip);
		QVERIFY2(allDeclareRegisteredCommands(&audio, &stray), qPrintable(stray));
		QVERIFY(menuAction(&audio, {QStringLiteral("Process"), QStringLiteral("Reverse")}) != nullptr);
		QVERIFY(menuAction(&audio, {QStringLiteral("Tempo and Warp"), QStringLiteral("Stretch Algorithm"), QStringLiteral("preserve_pitch")}) != nullptr);
		QVERIFY2(!menuAction(&audio, {QStringLiteral("Crossfade Into Next Clip...")})->isEnabled(), "the last clip offered a crossfade");

		QMenu strip;
		ShellCommandMenus::populateChannel(&strip, channel);
		QVERIFY2(allDeclareRegisteredCommands(&strip, &stray), qPrintable(stray));
		menuAction(&strip, {QStringLiteral("Loudness Meter"), QStringLiteral("On")})->trigger();
		QVERIFY(run(QStringLiteral("meter.get_state")).result.value(QStringLiteral("armed")).toBool());
		menuAction(&strip, {QStringLiteral("Loudness Meter"), QStringLiteral("Off")})->trigger();
		QVERIFY(!run(QStringLiteral("meter.get_state")).result.value(QStringLiteral("armed")).toBool());
		// L08: the mixer view's options and the solo mode are a click away on every channel.
		menuAction(&strip, {QStringLiteral("Mixer View"), QStringLiteral("Strip Width"), QStringLiteral("wide")})->trigger();
		QCOMPARE(run(QStringLiteral("window.mixer_view")).result.value(QStringLiteral("strip_width")).toString(), QStringLiteral("wide"));
		menuAction(&strip, {QStringLiteral("Mixer View"), QStringLiteral("Strip Width"), QStringLiteral("normal")})->trigger();
		QVERIFY(menuAction(&strip, {QStringLiteral("Solo Mode"), QStringLiteral("additive")}) != nullptr);
		QVERIFY(menuAction(&strip, {QStringLiteral("Strip"), QStringLiteral("Pan Mode"), QStringLiteral("dual")}) != nullptr);

		// Review W1-1 #1/#2: Take Lanes maps the rows' `lane`, and Plug-ins in Project lists an
		// instrument (a chain's `instrument`) as well as its effects.
		const QString trackId = control::trackIdOf(instrument);
		QVERIFY(run(QStringLiteral("comp.lane_add"), {{QStringLiteral("track"), trackId}}).ok);
		CommandListDialog lanes(CommandLists::takeLanes(trackId));
		QVERIFY(lanes.rowCount() >= 1);
		QVERIFY2(lanes.runAction(lanes.rowCount() - 1, 0).ok, qPrintable(lanes.statusText()));
		CommandListDialog plugins(CommandLists::pluginsInProject());
		bool listsInstrument = false;
		for (int row = 0; row < plugins.rowCount(); ++row)
		{
			listsInstrument = listsInstrument || plugins.row(row).value(QStringLiteral("kind")).toString() == QStringLiteral("instrument");
		}
		QVERIFY2(listsInstrument, "Plug-ins in Project left the instrument out");

		QMenu project;
		ShellCommandMenus::populateProject(&project);
		QVERIFY2(allDeclareRegisteredCommands(&project, &stray), qPrintable(stray));

		// The Inspector's clip section carries the same menu behind its More button.
		InspectorPanel inspector;
		ShellSelection::instance()->setClip(midiClip);
		inspector.rebuild();
		auto* more = childNamed<QToolButton>(&inspector, QStringLiteral("More clip actions"));
		QVERIFY(more != nullptr && more->menu() != nullptr);
		emit more->menu()->aboutToShow();
		QVERIFY(menuAction(more->menu(), {QStringLiteral("Scale and Chords"), QStringLiteral("Detect Chords")}) != nullptr);
		ShellSelection::instance()->setTrack(nullptr);
		delete instrument;
		delete sample;
	}

	/*! Q050-343: the sample generator's interface route is a CLICK, not just a registered id.
	 *  A sample track's More menu (the track header's and the Inspector's are the same
	 *  populateTrack) offers Insert Test Signal...; the action opens the SHARED sample.generate
	 *  command form with the track fixed and position 0, length 192 and kind tone pre-filled, and
	 *  running the form creates the clip in ONE undo step. An instrument track offers no such
	 *  entry. The form's about text is the registry description - the same help an agent reads -
	 *  so the route is checked where the menu actually puts it. */
	void theSampleGeneratorsMenuActionOpensItsFormAndMakesAClip()
	{
		auto* sample = Track::create(Track::Type::Sample, Engine::getSong());
		auto* instrument = Track::create(Track::Type::Instrument, Engine::getSong());
		dynamic_cast<InstrumentTrack*>(instrument)->loadInstrument(QStringLiteral("tripleoscillator"));
		const int before = sample->numOfClips();

		QMenu menu;
		ShellCommandMenus::populateTrack(&menu, sample);
		QAction* entry = menuAction(&menu, {QStringLiteral("Insert Test Signal...")});
		QVERIFY2(entry != nullptr, "a sample track's menu offered no Insert Test Signal...");
		QCOMPARE(entry->property("controlCommand").toString(), QStringLiteral("sample.generate"));

		entry->trigger();
		CommandForm* form = nullptr;
		for (QWidget* widget : QApplication::topLevelWidgets())
		{
			if (qobject_cast<CommandForm*>(widget) != nullptr && widget->isVisible()) { form = qobject_cast<CommandForm*>(widget); }
		}
		QVERIFY2(form != nullptr, "Insert Test Signal... opened no command form");
		// The form shows the registered description, and that description names the route that
		// opened it (the same text control.commands_list / MCP hand an agent).
		auto* about = form->findChild<QLabel*>(QStringLiteral("commandFormAbout"));
		QVERIFY(about != nullptr);
		QCOMPARE(about->text(), ControlRegistry::instance()->command(QStringLiteral("sample.generate"))->description);
		QVERIFY2(about->text().contains(QStringLiteral("Insert Test Signal...")),
			"the form's help did not name the route that opened it");
		// Where it was asked from is fixed, and the brief's defaults are what the fields start at.
		QJsonObject args;
		QString error;
		QVERIFY2(form->collect(&args, &error), qPrintable(error));
		QCOMPARE(args.value(QStringLiteral("track")).toString(), control::trackIdOf(sample));
		QCOMPARE(args.value(QStringLiteral("position")).toInt(), 0);
		QCOMPARE(args.value(QStringLiteral("length")).toInt(), 192);
		QCOMPARE(args.value(QStringLiteral("kind")).toString(), QStringLiteral("tone"));

		// The form is WA_DeleteOnClose, so the outcome is captured from its ran() signal instead
		// of read off a widget that is already on its way out.
		ControlResult made;
		int ran = 0;
		QObject::connect(form, &CommandForm::ran, form,
			[&made, &ran](const ControlResult& result) { made = result; ++ran; });
		form->accept();
		QCOMPARE(ran, 1);
		QVERIFY2(made.ok, qPrintable(made.errorMessage));
		const QString clip = made.result.value(QStringLiteral("clip")).toString();
		QVERIFY2(!clip.isEmpty(), "the form's run reported no clip");
		QCOMPARE(made.result.value(QStringLiteral("kind")).toString(), QStringLiteral("tone"));
		QCOMPARE(sample->numOfClips(), before + 1);
		// ONE undo removes the clip the click made; the sample track is as it was.
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(sample->numOfClips(), before);

		QMenu other;
		ShellCommandMenus::populateTrack(&other, instrument);
		QVERIFY2(menuAction(&other, {QStringLiteral("Insert Test Signal...")}) == nullptr,
			"an instrument track offered a test signal clip");

		delete instrument;
		delete sample;
	}

	/*! ED-015/016 (0.5.0 L04): the Inspector's clip and note fields edit through the registry -
	 *  typed the way a person types into them (double-click, text, Enter). Theme 7: they read in bars and
	 *  beats by default, in ticks once transport.time_format says so, and take either when typed. */
	void theInspectorsNumericFieldsEditTheClipAndItsNotes()
	{
		auto* track = Track::create(Track::Type::Instrument, Engine::getSong());
		dynamic_cast<InstrumentTrack*>(track)->loadInstrument(QStringLiteral("tripleoscillator"));
		auto* clip = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
		Note* note = clip->addNote(Note(TimePos(48), TimePos(0), 60, 100), false);
		note->setSelected(true);
		InspectorPanel inspector;
		inspector.show();
		ShellSelection::instance()->setClip(clip);
		inspector.rebuild();
		QCoreApplication::processEvents();
		const auto type = [&inspector](const QString& caption, const QString& text, int nth = 0) {
			QList<ReadoutField*> matches;
			for (ReadoutField* field : inspector.findChildren<ReadoutField*>())
			{
				if (field->accessibleName().contains(caption) && field->isVisibleTo(&inspector)) { matches.append(field); }
			}
			if (nth >= matches.size()) { return false; }
			QTest::mouseDClick(matches.at(nth), Qt::LeftButton);
			auto* editor = matches.at(nth)->findChild<QLineEdit*>();
			if (editor == nullptr) { return false; }
			editor->setText(text);
			emit editor->editingFinished();
			return true;
		};
		QVERIFY(type(QStringLiteral("Start"), QStringLiteral("2.1")));
		QCOMPARE(clip->startPosition().getTicks(), Engine::getSong()->ticksPerBar());
		QVERIFY(type(QStringLiteral("Length"), QStringLiteral("2")));
		QCOMPARE(clip->length().getTicks(), 2 * Engine::getSong()->ticksPerBar());
		QVERIFY(type(QStringLiteral("Offset"), QStringLiteral("0.1.0")));  // one beat (bars|beats shown: a plain "48" is bars)
		QCOMPARE(clip->startTimeOffset().getTicks(), 48);
		const auto shown = [&inspector](const QString& caption) {
			for (ReadoutField* field : inspector.findChildren<ReadoutField*>())
			{
				if (field->caption() == caption && field->isVisibleTo(&inspector)) { return field->valueText(); }
			}
			return QString();
		};
		inspector.rebuild();
		QCoreApplication::processEvents();
		QCOMPARE(shown(QStringLiteral("Start")), QStringLiteral("2.1.1"));  // the clip's, the first of the two Starts
		QCOMPARE(shown(QStringLiteral("Length")), QStringLiteral("2.0.0"));
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("transport.time_format"),
			{{QStringLiteral("format"), QStringLiteral("ticks")}}).ok);
		QCOMPARE(shown(QStringLiteral("Length")), QStringLiteral("384 ticks"));  // shown at once, in the new format
		QVERIFY(type(QStringLiteral("Offset"), QStringLiteral("24")));  // ticks shown: a plain number is ticks
		QCOMPARE(clip->startTimeOffset().getTicks(), 24);
		ControlRegistry::instance()->invoke(QStringLiteral("transport.time_format"), {{QStringLiteral("format"), QStringLiteral("bars_beats")}});
		QVERIFY(type(QStringLiteral("Key"), QStringLiteral("62")));
		QCOMPARE(clip->notes().front()->key(), 62);
		QVERIFY(type(QStringLiteral("Velocity"), QStringLiteral("64")));
		QCOMPARE(static_cast<int>(clip->notes().front()->getVolume()), 64);  // the 0..200 note volume scale
		ShellSelection::instance()->setTrack(nullptr);
		delete track;
	}

	//! Cluster 4168: a registered Inspector edit's refusal is shown verbatim in the field's error tooltip.
	void theInspectorShowsARegisteredNoteEditRefusal()
	{
		auto* track = Track::create(Track::Type::Instrument, Engine::getSong());
		auto* clip = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
		QVERIFY(clip != nullptr);
		Note* note = clip->addNote(Note(TimePos(96), TimePos(0), 60, 100), false);
		QVERIFY(note != nullptr);
		note->setSelected(true);
		InspectorPanel inspector;
		inspector.show();
		ShellSelection::instance()->setClip(clip);
		inspector.rebuild();
		QCoreApplication::processEvents();
		ReadoutField* key = nullptr;
		for (ReadoutField* candidate : inspector.findChildren<ReadoutField*>())
		{
			if (candidate->caption() == QStringLiteral("Key") && candidate->isVisibleTo(&inspector)) { key = candidate; }
		}
		QVERIFY(key != nullptr);

		ControlRegistry* registry = ControlRegistry::instance();
		const ControlCommand* registered = registry->command(QStringLiteral("note.transpose"));
		QVERIFY(registered != nullptr);
		ControlCommand* command = const_cast<ControlCommand*>(registered);
		const auto originalHandler = command->handler;
		const QString refusal = QStringLiteral("the note is locked by the test");
		bool dispatched = false;
		command->handler = [&dispatched, refusal](const QJsonObject&) {
			dispatched = true;
			return ControlResult::failure(ControlErrorKind::Refused, refusal);
		};

		QToolTip::hideText();
		QTest::mouseDClick(key, Qt::LeftButton);
		auto* editor = key->findChild<QLineEdit*>();
		const bool hasEditor = editor != nullptr;
		if (hasEditor)
		{
			editor->selectAll();
			QTest::keyClicks(editor, QStringLiteral("D4"));
			emit editor->editingFinished();
		}
		const QString shownError = QToolTip::text();
		command->handler = originalHandler;
		QVERIFY(hasEditor);
		QVERIFY(dispatched);
		QVERIFY2(shownError.contains(refusal), qPrintable(shownError));

		ShellSelection::instance()->setTrack(nullptr);
		delete track;
	}

	//! new-p10-171: note time origins are widened and range-checked before parsing or display.
	void theInspectorHandlesAnOutOfRangeNoteTimeOrigin()
	{
		RestoreMusicalTimeFormat restoreFormat;
		musicaltime::setFormat(musicaltime::Format::Time);
		{
			auto* track = Track::create(Track::Type::Instrument, Engine::getSong());
			ClearInspectorTrack cleanup{track};
			auto* clip = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
			QVERIFY(clip != nullptr);
			Note* note = clip->addNote(Note(TimePos(48), TimePos(200), 60, 100), false);
			QVERIFY(note != nullptr);
			note->setSelected(true);
			InspectorPanel inspector;
			inspector.show();
			ShellSelection::instance()->setClip(clip);
			inspector.rebuild();
			QCoreApplication::processEvents();
			ReadoutField* length = selectedNoteLength(inspector);
			QVERIFY(length != nullptr);
			QVERIFY(!length->valueText().contains(QStringLiteral("out of range")));
			QVERIFY(typeReadout(length, QStringLiteral("1s")));
			QVERIFY(note->length().getTicks() > 0);
			QVERIFY(note->length().getTicks() != 48);
		}

		const tick_t clipPosition = std::numeric_limits<tick_t>::max() - 100;
		auto* track = Track::create(Track::Type::Instrument, Engine::getSong());
		ClearInspectorTrack cleanup{track};
		auto* clip = dynamic_cast<MidiClip*>(track->createClip(TimePos(clipPosition)));
		QVERIFY(clip != nullptr);
		Note* note = clip->addNote(Note(TimePos(48), TimePos(200), 60, 100), false);
		QVERIFY(note != nullptr);
		note->setSelected(true);
		{
			InspectorPanel inspector;
			inspector.show();
			ShellSelection::instance()->setClip(clip);
			inspector.rebuild();
			QCoreApplication::processEvents();
			ReadoutField* length = selectedNoteLength(inspector);
			QVERIFY(length != nullptr);
			const QString shownLength = length->valueText();
			QVERIFY(typeReadout(length, QStringLiteral("96 ticks")));
			QCOMPARE(note->length().getTicks(), 96);

			ControlRegistry* registry = ControlRegistry::instance();
			const ControlCommand* registered = registry->command(QStringLiteral("note.edit"));
			QVERIFY(registered != nullptr);
			ControlCommand* command = const_cast<ControlCommand*>(registered);
			const auto originalHandler = command->handler;
			bool dispatched = false;
			command->handler = [&dispatched](const QJsonObject&) {
				dispatched = true;
				return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("origin was not checked"));
			};
			const bool typed = typeReadout(length, QStringLiteral("1s"));
			command->handler = originalHandler;
			QVERIFY(typed);
			const QString evidence = QStringLiteral("shown length: %1; note.edit dispatched: %2")
				.arg(shownLength).arg(dispatched);
			QVERIFY2(shownLength == QStringLiteral("out of range") && !dispatched, qPrintable(evidence));
		}
	}

	void theInspectorShowsAChannelsSendsAndStrip()
	{
		// MX-012 (0.5.0 L08): the channel's sends, with their removal, and its strip settings.
		Mixer* mixer = Engine::mixer();
		while (mixer->numChannels() <= 3) { mixer->createChannel(); }
		const QString from = control::channelIdOf(mixer->mixerChannel(2));
		const QString to = control::channelIdOf(mixer->mixerChannel(3));
		QVERIFY(run(QStringLiteral("mixer.send_to"), {{QStringLiteral("channel"), from}, {QStringLiteral("to"), to}}).ok);
		InspectorPanel inspector;
		inspector.show();
		ShellSelection::instance()->setChannel(from);
		inspector.rebuild();
		QCoreApplication::processEvents();
		QVERIFY(childNamed<QWidget>(&inspector, QStringLiteral("Trim (dB)")) != nullptr);
		QToolButton* remove = nullptr;
		for (auto* button : inspector.findChildren<QToolButton*>())
		{
			if (button->property("controlCommand").toString() == QLatin1String("mixer.route_remove")
				&& button->toolTip().contains(mixer->mixerChannel(3)->m_name)) { remove = button; }
		}
		QVERIFY2(remove != nullptr, "no removal for the send");
		remove->click();
		QVERIFY(run(QStringLiteral("mixer.send_pan"), {{QStringLiteral("channel"), from}, {QStringLiteral("to"), to},
			{QStringLiteral("pan"), 0.0}}).errorKind == ControlErrorKind::NotFound);  // the send is gone
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // and back, with the section rebuilt around it
		QCoreApplication::processEvents();
		ShellSelection::instance()->setTrack(nullptr);
	}

	//! MX-106: the knobs follow the selection - but a selection with nothing to follow (a channel
	//! with no effects, or nothing selected) has to stop them. Otherwise a turn kept writing to the
	//! device the user had already selected away from.
	void followingStopsWhenTheNewSelectionHasNothingToFollow()
	{
		QVERIFY(run(QStringLiteral("remote.connect"), {{QStringLiteral("first_cc"), 20}}).ok);
		Track* track = Track::create(Track::Type::Instrument, Engine::getSong());
		ShellSelection::instance()->setTrack(track);
		QCOMPARE(RemoteControls::instance()->state().value(QStringLiteral("target")).toString(),
			control::trackIdOf(track));
		// A channel with nothing on it: there is no device there to follow.
		Mixer* mixer = Engine::mixer();
		while (mixer->numChannels() <= 5) { mixer->createChannel(); }
		ShellSelection::instance()->setChannel(control::channelIdOf(mixer->mixerChannel(5)));
		QVERIFY2(RemoteControls::instance()->state().value(QStringLiteral("target")).toString().isEmpty(),
			"the knobs still drive the device the previous selection named");
		ShellSelection::instance()->setTrack(nullptr);
		QVERIFY(RemoteControls::instance()->state().value(QStringLiteral("target")).toString().isEmpty());
		delete track;
		QVERIFY(run(QStringLiteral("remote.disconnect")).ok);
	}

	void theDevicePanelDrawsTheChainAndTheModulatorBesideIt()
	{
		const int index = Engine::mixer()->createChannel();
		const QString channel = control::channelIdOf(Engine::mixer()->mixerChannel(index));
		const QString first = loadAnEffect(channel);
		QVERIFY2(!first.isEmpty(), "DevicePanelTest must load the amplifier module built for this test");
		const QString second = loadAnEffect(channel);
		// The parameter list an agent reads for the device (dsp.get_state), which the card draws from.
		const QJsonArray chains = run(QStringLiteral("dsp.get_state"), {{QStringLiteral("target"), channel}}).result
			.value(QStringLiteral("chains")).toArray();
		QVERIFY(!chains.isEmpty());
		const QJsonArray parameters = chains.first().toObject().value(QStringLiteral("devices")).toArray()
			.first().toObject().value(QStringLiteral("parameters")).toArray();
		QVERIFY(!parameters.isEmpty());
		const QString modulator = run(QStringLiteral("modulator.create"), {{QStringLiteral("name"), QStringLiteral("Wobble")},
			{QStringLiteral("rate"), 2.0}}).result.value(QStringLiteral("modulator")).toString();
		QVERIFY(run(QStringLiteral("modulator.target_set"), {{QStringLiteral("modulator"), modulator},
			{QStringLiteral("channel"), channel}, {QStringLiteral("chain"), 0}, {QStringLiteral("effect"), 0},
			{QStringLiteral("parameter"), parameters.first().toObject().value(QStringLiteral("name")).toString()},
			{QStringLiteral("depth"), 0.25}}).ok);

		DevicePanel panel;
		panel.resize(1200, 300);
		panel.show();
		ShellSelection::instance()->setChannel(channel);
		panel.rebuild();
		QCoreApplication::processEvents();
		QCOMPARE(panel.describe().value(QStringLiteral("channel")).toString(), channel);
		QCOMPARE(cards(panel).size(), second.isEmpty() ? 1 : 2);
		const QJsonObject card = cards(panel).first().toObject();
		QCOMPARE(card.value(QStringLiteral("plugin")).toString(), first);
		QVERIFY(card.value(QStringLiteral("parameters")).toInt() > 0);
		QVERIFY(card.value(QStringLiteral("parameters")).toInt() <= parameters.size());
		QVERIFY(card.value(QStringLiteral("shown")).toInt() <= DeviceCard::MaxParameters);
		QCOMPARE(panel.describe().value(QStringLiteral("modulators")).toArray(), QJsonArray{modulator});

		// The power button is plugin.bypass.
		auto* deviceCard = shownChildren<DeviceCard>(&panel).first();
		Effect* effect = deviceCard->spec().effect;
		auto* power = childNamed<QToolButton>(deviceCard, QStringLiteral("Switch %1 on or off").arg(deviceCard->spec().title));
		QVERIFY(power != nullptr);
		power->click();
		QVERIFY(!effect->isEnabled());

		// Removed over the socket: its card goes with it at once - before the device is deleted - and (theme 12) only
		// its card: the other device's card stays, the same widget, through the redraw.
		const QPointer<DeviceCard> other = second.isEmpty() ? nullptr : shownChildren<DeviceCard>(&panel).last();
		QVERIFY(run(QStringLiteral("plugin.unload"), {{QStringLiteral("target"), channel}, {QStringLiteral("plugin"), first}}).ok);
		QCOMPARE(cards(panel).size(), second.isEmpty() ? 0 : 1);
		QCoreApplication::processEvents();
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		QCOMPARE(cards(panel).size(), second.isEmpty() ? 0 : 1);
		if (!second.isEmpty()) { QVERIFY(!other.isNull() && other->isVisibleTo(&panel)); }
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCoreApplication::processEvents();
		QCOMPARE(cards(panel).size(), second.isEmpty() ? 1 : 2);
		ShellSelection::instance()->setTrack(nullptr);
	}

	//! 0.5.0 L10 (W3): a channel's level beside its heading (MX-101); a parameter's alias captions its
	//! control (MX-097); the delta button is plugin.delta (MX-080); folding hides the parameters (MX-113).
	void theLevelAliasDeltaAndFoldShowOnTheCard()
	{
		const int index = Engine::mixer()->createChannel();
		MixerChannel* mixerChannel = Engine::mixer()->mixerChannel(index);
		const QString channel = control::channelIdOf(mixerChannel);
		const QString effect = loadAnEffect(channel);
		if (effect.isEmpty()) { QSKIP("this host loads no effect module"); }
		QVERIFY(run(QStringLiteral("plugin.param_rename"), {{QStringLiteral("target"), channel}, {QStringLiteral("plugin"), effect},
			{QStringLiteral("name"), QStringLiteral("Wet/Dry mix")}, {QStringLiteral("alias"), QStringLiteral("Blend")}}).ok);
		DevicePanel panel;
		panel.resize(1200, 300);
		panel.show();
		ShellSelection::instance()->setChannel(channel);
		panel.rebuild();
		QCoreApplication::processEvents();
		const QList<ChannelLevelMeter*> meters = shownChildren<ChannelLevelMeter>(&panel);
		QCOMPARE(meters.size(), 1);
		QCOMPARE(meters.first()->channelId(), channel);
		mixerChannel->m_rmsLeft.set(0.5f);
		meters.first()->refresh();
		QVERIFY(std::abs(meters.first()->levels().first + 6.0206f) < 0.05f);
		DeviceCard* card = shownChildren<DeviceCard>(&panel).first();
		Effect* device = card->spec().effect;
		QVERIFY(childNamed<QWidget>(card, QStringLiteral("Blend")) != nullptr);
		auto* delta = childNamed<QToolButton>(card, QStringLiteral("Hear only what %1 changes").arg(card->spec().title));
		QVERIFY(delta != nullptr);
		delta->click();
		QVERIFY(device->deltaTap().enabled());
		delta->click();
		QVERIFY(!device->deltaTap().enabled());
		auto* fold = childNamed<QToolButton>(card, QStringLiteral("Fold or open %1").arg(card->spec().title));
		QVERIFY(fold != nullptr);
		fold->click();
		QVERIFY(device->deviceOptions().folded);
		QCoreApplication::processEvents();
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		DeviceCard* redrawn = shownChildren<DeviceCard>(&panel).first();
		QVERIFY(childNamed<QWidget>(redrawn, QStringLiteral("Blend")) == nullptr);  // folded: the header only
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(!device->deviceOptions().folded);
		ShellSelection::instance()->setTrack(nullptr);
	}

	void aTrackShowsItsInstrumentAndItsDevices()
	{
		const QString track = run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("instrument")}})
			.result.value(QStringLiteral("track")).toString();
		const QString effect = loadAnEffect(track);
		if (effect.isEmpty()) { QSKIP("this host loads no effect module"); }
		Track* resolved = nullptr;
		for (Track* t : Engine::getSong()->tracks()) { if (control::trackIdOf(t) == track) { resolved = t; } }
		QVERIFY(resolved != nullptr);
		DevicePanel panel;
		panel.show();
		ShellSelection::instance()->setTrack(resolved);
		panel.rebuild();
		const QJsonArray drawn = cards(panel);
		QVERIFY(!drawn.isEmpty());
		QCOMPARE(drawn.last().toObject().value(QStringLiteral("plugin")).toString(), effect);
		const bool hasInstrument = dynamic_cast<InstrumentTrack*>(resolved)->instrument() != nullptr;
		QCOMPARE(drawn.size(), hasInstrument ? 2 : 1);
		ShellSelection::instance()->setTrack(nullptr);
	}

	//! Theme 12: a device added redraws the panel in place - the cards already there are the same widgets - and a card
	//! says its state in words: "Bypassed" once bypassed, the A/B slot heard once one is stored.
	void theCardsStayInPlaceAndSayTheirState()
	{
		const int index = Engine::mixer()->createChannel();
		const QString channel = control::channelIdOf(Engine::mixer()->mixerChannel(index));
		const QString first = loadAnEffect(channel);
		if (first.isEmpty()) { QSKIP("this host loads no effect module"); }
		DevicePanel panel;
		panel.resize(1200, 300);
		panel.show();
		ShellSelection::instance()->setChannel(channel);
		panel.rebuild();
		QCoreApplication::processEvents();
		const QPointer<DeviceCard> card = shownChildren<DeviceCard>(&panel).first();
		QVERIFY(!loadAnEffect(channel).isEmpty());
		QCoreApplication::processEvents();
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		QCOMPARE(cards(panel).size(), 2);
		QVERIFY2(!card.isNull() && card->isVisibleTo(&panel), "the first card was made again for a device added after it");
		QVERIFY(run(QStringLiteral("plugin.bypass"), {{QStringLiteral("target"), channel}, {QStringLiteral("plugin"), first},
			{QStringLiteral("bypass"), true}}).ok);
		card->refreshActivity();
		auto* state = card->findChild<QLabel*>(QStringLiteral("deviceState"));
		QVERIFY(state != nullptr && state->text().contains(QStringLiteral("Bypassed")));
		QVERIFY(state->accessibleName().contains(QStringLiteral("Bypassed")));  // a screen reader hears it too
		QVERIFY(cards(panel).first().toObject().value(QStringLiteral("state")).toObject().value(QStringLiteral("bypassed")).toBool());
		auto* ab = card->findChild<QToolButton*>(QStringLiteral("deviceAb"));
		QVERIFY(ab != nullptr && !ab->isVisibleTo(card));  // no slot stored yet: A/B means nothing
		QVERIFY(run(QStringLiteral("plugin.ab"), {{QStringLiteral("target"), channel}, {QStringLiteral("plugin"), first},
			{QStringLiteral("action"), QStringLiteral("store")}, {QStringLiteral("slot"), QStringLiteral("b")}}).ok);
		card->refreshActivity();
		QVERIFY(ab->isVisibleTo(card));
		QCOMPARE(ab->text(), QStringLiteral("B"));  // the letter, not a colour
		QVERIFY(state->text().contains(QStringLiteral("Hearing B")));
		ShellSelection::instance()->setTrack(nullptr);
	}

	//! Theme 12: the card's Actions button and its right-click offer one menu; its first part - bypass, editor,
	//! presets, A/B, replace, move, remove - names registered commands, and Move Right is plugin.move, one undo step.
	void theCardsActionMenuOffersEachAction()
	{
		const int index = Engine::mixer()->createChannel();
		MixerChannel* mixerChannel = Engine::mixer()->mixerChannel(index);
		const QString channel = control::channelIdOf(mixerChannel);
		const QString first = loadAnEffect(channel);
		if (first.isEmpty()) { QSKIP("this host loads no effect module"); }
		const QString second = loadAnEffect(channel);
		DevicePanel panel;
		panel.resize(1200, 300);
		panel.show();
		ShellSelection::instance()->setChannel(channel);
		panel.rebuild();
		QCoreApplication::processEvents();
		DeviceCard* card = shownChildren<DeviceCard>(&panel).first();
		auto* actions = card->findChild<QToolButton*>(QStringLiteral("deviceActions"));
		QVERIFY(actions != nullptr && actions->menu() != nullptr);
		QVERIFY(!actions->accessibleName().isEmpty());
		emit actions->menu()->aboutToShow();
		QMenu* menu = actions->menu();
		for (const QString& text : {QStringLiteral("Bypass"), QStringLiteral("Open Editor"), QStringLiteral("Replace With..."),
				QStringLiteral("Move Left"), QStringLiteral("Move Right"), QStringLiteral("Remove")})
		{
			QAction* action = menuAction(menu, {text});
			QVERIFY2(action != nullptr, qPrintable(text));
			QVERIFY2(ControlRegistry::instance()->hasCommand(action->property("controlCommand").toString()), qPrintable(text));
		}
		QVERIFY(menuAction(menu, {QStringLiteral("Presets"), QStringLiteral("Load Preset...")}) != nullptr);
		QVERIFY(menuAction(menu, {QStringLiteral("A/B"), QStringLiteral("Toggle A/B")}) != nullptr);
		QVERIFY2(!menuAction(menu, {QStringLiteral("Move Left")})->isEnabled(), "the first device offered to move left");
		if (second.isEmpty()) { ShellSelection::instance()->setTrack(nullptr); return; }
		menuAction(menu, {QStringLiteral("Move Right")})->trigger();
		QCOMPARE(control::effectIdOf(mixerChannel->m_fxChain.effects().back()), first);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(control::effectIdOf(mixerChannel->m_fxChain.effects().front()), first);
		ShellSelection::instance()->setTrack(nullptr);
	}

	/*! BUG new-p9-104: the card's right-click menu is a stack object, and one of its own entries (Remove)
	 *  retires the card - so the menu must not be the card's child: a card deleted while the menu's event
	 *  loop runs would take the stack menu with it and the menu's destructor would run twice. */
	void theContextMenuIsNotTheCardsChild()
	{
		const int index = Engine::mixer()->createChannel();
		const QString channel = control::channelIdOf(Engine::mixer()->mixerChannel(index));
		if (loadAnEffect(channel).isEmpty()) { QSKIP("this host loads no effect module"); }
		DevicePanel panel;
		panel.resize(1200, 300);
		panel.show();
		ShellSelection::instance()->setChannel(channel);
		panel.rebuild();
		QCoreApplication::processEvents();
		DeviceCard* card = shownChildren<DeviceCard>(&panel).first();
		QVERIFY(card != nullptr);

		// The menu the right-click opens, and the widget it belongs to.
		bool opened = false;
		QWidget* host = nullptr;
		QTimer::singleShot(0, card, [&opened, &host] {
			if (QMenu* menu = visibleMenu())
			{
				opened = true;
				host = menu->parentWidget();
				menu->close();
			}
		});
		QContextMenuEvent event(QContextMenuEvent::Mouse, card->rect().center(), card->mapToGlobal(card->rect().center()));
		QCoreApplication::sendEvent(card, &event);
		QVERIFY2(opened, "the card's right-click did not open its action menu");
		QVERIFY2(host != card, "the action menu is a child of the card: a card retired by the menu's own Remove entry takes the stack menu with it");
		ShellSelection::instance()->setTrack(nullptr);
	}

	/*! BUG new-p9-104: removing the device from the card's own menu retires the card while the menu's event
	 *  loop still runs - the card must not take the stack menu with it (the menu would be destroyed twice). */
	void removingADeviceFromItsOwnMenuDoesNotDestroyTheMenu()
	{
		const int index = Engine::mixer()->createChannel();
		const QString channel = control::channelIdOf(Engine::mixer()->mixerChannel(index));
		if (loadAnEffect(channel).isEmpty()) { QSKIP("this host loads no effect module"); }
		DevicePanel panel;
		panel.resize(1200, 300);
		panel.show();
		ShellSelection::instance()->setChannel(channel);
		panel.rebuild();
		QCoreApplication::processEvents();
		DeviceCard* card = shownChildren<DeviceCard>(&panel).first();
		QVERIFY(card != nullptr);
		const QPointer<DeviceCard> retired = card;

		QWidget* host = nullptr;
		QTimer::singleShot(0, card, [&host] {
			QMenu* menu = visibleMenu();
			if (menu == nullptr) { return; }
			host = menu->parentWidget();
			if (QAction* remove = menuAction(menu, {QStringLiteral("Remove")})) { remove->trigger(); }
			menu->close();
		});
		QContextMenuEvent event(QContextMenuEvent::Mouse, card->rect().center(), card->mapToGlobal(card->rect().center()));
		QCoreApplication::sendEvent(card, &event);
		QCoreApplication::processEvents();
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		QVERIFY2(host != card, "the action menu is a child of the card: the card the menu removes takes the stack menu with it");
		QVERIFY2(retired.isNull(), "the card of the removed device is still there");
		ShellSelection::instance()->setTrack(nullptr);
	}

	/*! BUG new-p17-49: an "Open Editor" that opens nothing must say why - the failed plugin.editor_open
	 *  was discarded, and the built-in dialog is a fallback that can fail too, so the user was left with
	 *  no window and no word. The card reports the command's own reason. */
	void theCardSaysWhyTheEditorDidNotOpen()
	{
		const int index = Engine::mixer()->createChannel();
		MixerChannel* mixerChannel = Engine::mixer()->mixerChannel(index);
		const QString channel = control::channelIdOf(mixerChannel);
		auto* effect = new NoViewEffect(&mixerChannel->m_fxChain);
		mixerChannel->m_fxChain.appendEffect(effect);
		const QString plugin = control::effectIdOf(effect);
		DeviceCard::Spec spec;
		spec.target = channel;
		spec.plugin = plugin;
		spec.title = QStringLiteral("No view effect");
		spec.effect = effect;
		DeviceCard card(spec, {});
		card.show();
		QCoreApplication::processEvents();

		// The command's own refusal: the card has to report THIS one, not a word of its own.
		const ControlResult failed = run(QStringLiteral("plugin.editor_open"),
			{{QStringLiteral("target"), channel}, {QStringLiteral("plugin"), plugin}});
		QVERIFY2(!failed.ok, "the test device has no editor of its own");
		QVERIFY2(!failed.errorMessage.isEmpty(), "the command refused without saying why");

		card.openEditor();
		auto* state = card.findChild<QLabel*>(QStringLiteral("deviceState"));
		QVERIFY(state != nullptr);
		QVERIFY2(state->text().contains(failed.errorMessage), qPrintable(state->text()));
		QVERIFY2(state->accessibleName().contains(failed.errorMessage), qPrintable(state->accessibleName()));
		ShellSelection::instance()->setTrack(nullptr);
	}

	/*! BUG new-p15-48: the fold handler touched the card's body after the dispatch - and the dispatch runs
	 *  a command, which can retire the card, so the body would be a dangling pointer. A card that is gone
	 *  is not touched: the body keeps the state it had. */
	void foldingACardThatTheDispatchRetiresTouchesNothing()
	{
		const int index = Engine::mixer()->createChannel();
		MixerChannel* mixerChannel = Engine::mixer()->mixerChannel(index);
		const QString channel = control::channelIdOf(mixerChannel);
		const QString effect = loadAnEffect(channel);
		if (effect.isEmpty()) { QSKIP("this host loads no effect module"); }
		Effect* device = mixerChannel->m_fxChain.effects().front();
		DeviceCard::Spec spec;
		spec.target = channel;
		spec.plugin = effect;
		spec.title = device->displayName();
		spec.effect = device;
		auto* card = new DeviceCard(spec, {});
		card->show();
		QCoreApplication::processEvents();
		auto* fold = childNamed<QToolButton>(card, QStringLiteral("Fold or open %1").arg(spec.title));
		QVERIFY(fold != nullptr);
		// The card's body: the widget the fold hides. It is let out of the card, so it outlives it and
		// the test can watch what the handler does to it.
		QWidget* body = nullptr;
		for (QWidget* child : card->findChildren<QWidget*>())
		{
			if (child->parentWidget() == card && child->objectName().isEmpty()) { body = child; }
		}
		QVERIFY(body != nullptr);
		QVERIFY(body->isVisible());

		// A listener that retires the card while the fold command runs (a chain's own signal can do it).
		bool folding = false;
		const QPointer<DeviceCard> retired = card;
		QObject::connect(&mixerChannel->m_fxChain, &EffectChain::dataChanged, &mixerChannel->m_fxChain,
			[&folding, body, retired] {
				if (!folding || retired.isNull()) { return; }
				body->setParent(nullptr);
				body->show();
				delete retired.data();
			});
		folding = true;
		fold->click();
		folding = false;
		QVERIFY2(retired.isNull(), "the card was not retired while the fold was dispatched");
		QVERIFY2(body->isVisible(), "the fold touched a card that was already gone");
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // the fold the test made
	}

	//! Theme 12: the device picker lists the catalogue's effects with their type and format in words, and its search
	//! filters them live; a search that matches nothing shows nothing.
	void theDevicePickerSearchesAndWordsEachRow()
	{
		DevicePicker picker(DevicePicker::Kind::Effect, QStringLiteral("Add"));
		if (picker.shownRows() == 0) { QSKIP("this host catalogues no effect"); }
		const QString row = picker.shownRowText(0);
		QVERIFY2(row.contains(QStringLiteral("Effect")), qPrintable(row));
		const QString name = row.section(QStringLiteral(" | "), 0, 0);
		picker.setQuery(name);
		QVERIFY(picker.shownRows() >= 1);
		QVERIFY(picker.shownRowText(0).contains(name));
		picker.setQuery(QStringLiteral("zz-no-device-is-called-this"));
		QCOMPARE(picker.shownRows(), 0);
		QVERIFY(picker.selectedDevice().isEmpty());
		picker.setQuery(QString());
		QVERIFY(picker.selectedDevice().startsWith(QStringLiteral("dev-")) || picker.shownRows() > 0);
	}

	//! Theme 13: selecting another clip of the same track keeps the track section - the same widget, with the focus
	//! where it was; the notes' fields say "Multiple values" when the notes differ, and a key typed there goes to all.
	void theInspectorKeepsItsContextAndShowsMixedValues()
	{
		auto* track = Track::create(Track::Type::Instrument, Engine::getSong());
		dynamic_cast<InstrumentTrack*>(track)->loadInstrument(QStringLiteral("tripleoscillator"));
		auto* clip = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
		auto* other = dynamic_cast<MidiClip*>(track->createClip(TimePos(4 * 192)));
		Note* low = clip->addNote(Note(TimePos(0), TimePos(0), 60, 100), false);
		Note* high = clip->addNote(Note(TimePos(48), TimePos(0), 64, 80), false);
		low->setSelected(true);
		high->setSelected(true);
		InspectorPanel inspector;
		inspector.show();
		inspector.activateWindow();
		ShellSelection::instance()->setClip(clip);
		inspector.rebuild();
		QCoreApplication::processEvents();
		auto* name = childNamed<QLineEdit>(&inspector, QStringLiteral("Track name"));
		QVERIFY(name != nullptr);
		name->setFocus();
		const auto field = [&inspector](const QString& caption) -> ReadoutField* {
			for (ReadoutField* f : inspector.findChildren<ReadoutField*>())
			{
				if (f->caption() == caption && f->isVisibleTo(&inspector)) { return f; }
			}
			return nullptr;
		};
		QVERIFY(field(QStringLiteral("Key")) != nullptr);
		QVERIFY2(field(QStringLiteral("Key"))->valueText().contains(QStringLiteral("Multiple values")), qPrintable(field(QStringLiteral("Key"))->valueText()));
		QVERIFY(field(QStringLiteral("Velocity"))->valueText().contains(QStringLiteral("Multiple values")));
		QVERIFY(field(QStringLiteral("Selected notes"))->valueText().contains(QStringLiteral("2")));
		QTest::mouseDClick(field(QStringLiteral("Key")), Qt::LeftButton);
		auto* editor = field(QStringLiteral("Key"))->findChild<QLineEdit*>();
		QVERIFY(editor != nullptr);
		editor->setText(QStringLiteral("62"));
		emit editor->editingFinished();
		QCOMPARE(low->key(), 62);  // given to every selected note, not transposed by the first one's distance
		QCOMPARE(high->key(), 62);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		// The undo restored the clip's checkpoint, which makes its notes anew: the second note (velocity 80) is
		// found again.
		const auto restored = std::find_if(clip->notes().begin(), clip->notes().end(),
			[](const Note* note) { return static_cast<int>(note->getVolume()) == 80; });
		QVERIFY(restored != clip->notes().end());
		QCOMPARE((*restored)->key(), 64);
		ShellSelection::instance()->setClip(other);
		inspector.rebuild();
		QCoreApplication::processEvents();
		QCOMPARE(childNamed<QLineEdit>(&inspector, QStringLiteral("Track name")), name);  // the track section stayed
		QVERIFY(inspector.describe().value(QStringLiteral("sections")).toArray().contains(QStringLiteral("clip")));
		ShellSelection::instance()->setTrack(nullptr);
		delete track;
	}

	/*! ED-015: the clip section's Name and Notes labels were built from the clip as it was when the
	 *  section was made and nothing ever re-read them, so a rename or a note edited elsewhere left the
	 *  Inspector showing the old text - the clip key is the pointer, so no selection change redrew it. */
	void theClipsNameAndNoteCountFollowTheClip()
	{
		auto* track = Track::create(Track::Type::Instrument, Engine::getSong());
		auto* clip = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
		clip->setName(QStringLiteral("Hook"));
		clip->addNote(Note(TimePos(0), TimePos(0), 60, 100), false);
		InspectorPanel inspector;
		inspector.show();
		ShellSelection::instance()->setClip(clip);
		inspector.rebuild();
		QCoreApplication::processEvents();
		// The clip section's plain value labels, in the order the rows are added: Name, then Notes.
		const auto valueLabels = [&inspector] {
			auto* more = childNamed<QToolButton>(&inspector, QStringLiteral("More clip actions"));
			if (more == nullptr) { return QList<QLabel*>{}; }
			auto* section = more->parentWidget() != nullptr ? more->parentWidget()->parentWidget() : nullptr;
			QList<QLabel*> values;
			for (QLabel* label : section != nullptr ? section->findChildren<QLabel*>() : QList<QLabel*>{})
			{
				// The captions and the section's own title are named; what is left are the value labels.
				if (label->objectName() != QStringLiteral("deviceCaption") && label->objectName() != QStringLiteral("deviceSection")
					&& label->isVisibleTo(&inspector))
				{
					values.append(label);
				}
			}
			return values;
		};
		QCOMPARE(valueLabels().size(), 2);
		QCOMPARE(valueLabels().at(0)->text(), QStringLiteral("Hook"));
		QCOMPARE(valueLabels().at(1)->text(), QStringLiteral("1"));
		// Renamed and given a second note from outside the Inspector, as the arranger or the note editor does.
		clip->setName(QStringLiteral("Riff"));
		clip->addNote(Note(TimePos(48), TimePos(0), 64, 100), false);
		inspector.rebuild();
		QCoreApplication::processEvents();
		QCOMPARE(valueLabels().at(0)->text(), QStringLiteral("Riff"));
		QCOMPARE(valueLabels().at(1)->text(), QStringLiteral("2"));
		ShellSelection::instance()->setTrack(nullptr);
		delete track;
	}

	//! ED-016: a typed start outside the note range is refused before note.edit can see an overflowed position.
	void theInspectorRejectsAnOverflowingSelectedNoteStart()
	{
		ShellSelection::instance()->setTrack(nullptr);
		std::unique_ptr<Track> trackOwner(Track::create(Track::Type::Instrument, Engine::getSong()));
		auto* track = dynamic_cast<InstrumentTrack*>(trackOwner.get());
		QVERIFY(track != nullptr);
		auto* clip = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
		QVERIFY(clip != nullptr);
		Note* first = clip->addNote(Note(TimePos(48), TimePos(100), 60, 100), false);
		Note* second = clip->addNote(Note(TimePos(48), TimePos(200), 64, 100), false);
		QVERIFY(first != nullptr);
		QVERIFY(second != nullptr);
		first->setSelected(true);
		second->setSelected(true);

		InspectorPanel inspector;
		inspector.show();
		ShellSelection::instance()->setClip(clip);
		inspector.rebuild();
		QCoreApplication::processEvents();
		ReadoutField* start = nullptr;
		for (ReadoutField* candidate : inspector.findChildren<ReadoutField*>())
		{
			if (candidate->caption() == QStringLiteral("Start") && candidate->isVisibleTo(&inspector)) { start = candidate; }
		}
		QVERIFY(start != nullptr);
		QTest::mouseDClick(start, Qt::LeftButton);
		auto* editor = start->findChild<QLineEdit*>();
		QVERIFY(editor != nullptr);

		ControlRegistry* registry = ControlRegistry::instance();
		const ControlCommand* registered = registry->command(QStringLiteral("note.edit"));
		QVERIFY(registered != nullptr);
		// The command's handler copy lets this UI test observe a dispatch that the real handler rejects anyway.
		ControlCommand* mutableCommand = const_cast<ControlCommand*>(registered);
		const auto originalHandler = mutableCommand->handler;
		bool dispatched = false;
		mutableCommand->handler = [&dispatched](const QJsonObject&) {
			dispatched = true;
			return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("observed by test"));
		};
		editor->setText(QStringLiteral("2147483647 ticks"));
		emit editor->editingFinished();
		mutableCommand->handler = originalHandler;
		QVERIFY2(!dispatched, "an out-of-range selected-note start reached note.edit");
		ShellSelection::instance()->setTrack(nullptr);
	}

	//! Theme 13: a channel's name is a field in the Inspector, and Enter renames it through mixer.rename_channel.
	void theChannelsNameIsEditedInPlace()
	{
		const int index = Engine::mixer()->createChannel();
		MixerChannel* mixerChannel = Engine::mixer()->mixerChannel(index);
		const QString channel = control::channelIdOf(mixerChannel);
		const QString before = mixerChannel->m_name;
		InspectorPanel inspector;
		inspector.show();
		ShellSelection::instance()->setChannel(channel);
		inspector.rebuild();
		QCoreApplication::processEvents();
		auto* name = childNamed<QLineEdit>(&inspector, QStringLiteral("Channel name"));
		QVERIFY(name != nullptr);
		QCOMPARE(name->text(), before);
		name->setText(QStringLiteral("Vocals"));
		emit name->editingFinished();
		QCOMPARE(mixerChannel->m_name, QStringLiteral("Vocals"));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(mixerChannel->m_name, before);
		ShellSelection::instance()->setTrack(nullptr);
	}
};

QTEST_MAIN(DevicePanelTest)
#include "DevicePanelTest.moc"
