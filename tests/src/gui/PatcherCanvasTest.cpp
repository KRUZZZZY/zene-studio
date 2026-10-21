/*
 * PatcherCanvasTest.cpp - Q050-087: the device-chain patcher canvas
 *
 * Drives the real canvas with real mouse events and key presses over an instrument track's chain of three graph-capable
 * effects (legacy effects: no audio-ports model, so the chain really routes through its patcher graph), and checks every
 * edit against what the agent surface reads - patcher.get_state - and against what the agent surface can write -
 * patcher.set_wiring: connect, disconnect, Shift-click a cable for a second destination, add a device, reorder, one
 * undo step each. A chain of audio-ports devices (the shipped Amplifier) is shown read-only with the reason.
 *
 * This file is part of Zene Studio, derived from LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include <QtTest>

#include <QApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QTemporaryDir>

#include <memory>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "DevicePicker.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "Mixer.h"
#include "AudioBusHandle.h"
#include "InstrumentTrack.h"
#include "Track.h"
#include "PatcherCanvas.h"
#include "ShellCommandMenus.h"
#include "Plugin.h"
#include "SampleFrame.h"
#include "Song.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

//! A legacy per-channel gain effect: no audio-ports model, so the chain routes through its graph.
class ScaleEffect : public Effect
{
public:
	ScaleEffect(Model* parent, float gain) : Effect{&s_descriptor, parent, nullptr}, m_gain(gain) {}
	EffectControls* controls() override { return nullptr; }

protected:
	ProcessStatus processImpl(SampleFrame* buf, const f_cnt_t frames) override
	{
		for (f_cnt_t f = 0; f < frames; ++f) { buf[f][0] *= m_gain; buf[f][1] *= m_gain; }
		return ProcessStatus::Continue;
	}

private:
	float m_gain;
	static const Plugin::Descriptor s_descriptor;
};

const Plugin::Descriptor ScaleEffect::s_descriptor{"patchercanvastest", "Canvas scale", "Gain for PatcherCanvasTest", "LMMS",
	0x0100, Plugin::Type::Effect, nullptr, nullptr, nullptr};

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

} // namespace

class PatcherCanvasTest : public QObject
{
	Q_OBJECT

private:
	QTemporaryDir m_home;
	QString m_channel;
	EffectChain* m_chain = nullptr;
	std::unique_ptr<PatcherCanvasWindow> m_window;

	PatcherCanvas* canvas() const { return m_window->canvas(); }

	QJsonObject state() const { return run(QStringLiteral("patcher.get_state"), {{QStringLiteral("target"), m_channel}}).result; }

	//! The cables the agent reads, as the canvas's own edge list would write them.
	//! The entry called \a text in the channel menu's Rack submenu.
	static QAction* rackEntry(const QMenu& menu, const QString& text)
	{
		for (QAction* action : menu.actions())
		{
			if (action->menu() == nullptr || action->text() != QStringLiteral("Rack")) { continue; }
			for (QAction* entry : action->menu()->actions()) { if (entry->text() == text) { return entry; } }
		}
		return nullptr;
	}

	//! How many visible canvas windows \a target has.
	static int openCanvasWindows(const QString& target)
	{
		int count = 0;
		for (QWidget* w : QApplication::topLevelWidgets())
		{
			if (w->objectName() == QStringLiteral("patcherCanvas-") + target && w->isVisible()) { ++count; }
		}
		return count;
	}

	QJsonArray agentEdges() const { return state().value(QStringLiteral("effective_wiring")).toObject().value(QStringLiteral("edges")).toArray(); }

	bool sameAsAgent() const { return canvas()->graph().edgesJson() == agentEdges(); }

	void drag(const QPointF& from, const QPointF& to, Qt::KeyboardModifiers mods = Qt::NoModifier)
	{
		QTest::mousePress(canvas(), Qt::LeftButton, mods, from.toPoint());
		QTest::mouseMove(canvas(), to.toPoint());
		QTest::mouseRelease(canvas(), Qt::LeftButton, mods, to.toPoint());
	}

	static QString amplifierId()
	{
		const QJsonArray devices = run(QStringLiteral("plugin.list"), {{QStringLiteral("kind"), QStringLiteral("effect")}, {QStringLiteral("loadable_only"), true}})
			.result.value(QStringLiteral("devices")).toArray();
		for (const QJsonValue& v : devices)
		{
			if (v.toObject().value(QStringLiteral("name")).toString() == QLatin1String("amplifier")) { return v.toObject().value(QStringLiteral("id")).toString(); }
		}
		return {};
	}

	//! A fresh chain of \a count scale effects on a new instrument track, and a canvas window over it.
	void freshChain(int count)
	{
		// An instrument track's own chain: the mixer view draws every effect of a channel and the fixture effect has no
		// controls to draw, while nothing draws a track's chain until its window is opened.
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		m_chain = track->audioBusHandle()->effects();
		for (int i = 0; i < count; ++i) { m_chain->appendEffect(new ScaleEffect(m_chain, 1.f + i)); }
		m_channel = control::trackIdOf(track);
		m_window = std::make_unique<PatcherCanvasWindow>(m_channel);
		m_window->show();
		QApplication::processEvents();
	}

private slots:
	void initTestCase()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
		QVERIFY(m_home.isValid());
		guitest::startGui(m_home);
		QVERIFY(getGUI() != nullptr);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		m_window.reset();
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! The mixer channel menu's Rack submenu offers Chain Canvas..., an action over the same patcher commands the canvas
	//! dispatches; triggering it opens the canvas of that channel (once - a second trigger raises the same window).
	void theChannelMenuOpensTheCanvas()
	{
		const int index = Engine::mixer()->createChannel();
		const QString id = control::channelIdOf(Engine::mixer()->mixerChannel(index));
		QMenu menu;
		ShellCommandMenus::populateChannel(&menu, id);
		QAction* open = rackEntry(menu, QStringLiteral("Chain Canvas..."));
		QVERIFY2(open != nullptr, "the Rack submenu has a Chain Canvas... entry");
		QCOMPARE(open->property("controlCommand").toString(), QStringLiteral("patcher.set_wiring"));
		QVERIFY(ControlRegistry::instance()->hasCommand(QStringLiteral("patcher.set_wiring")));
		QVERIFY(ControlRegistry::instance()->hasCommand(QStringLiteral("patcher.get_state")));

		QCOMPARE(openCanvasWindows(id), 0);
		open->trigger();
		QCOMPARE(openCanvasWindows(id), 1);
		open->trigger();
		QCOMPARE(openCanvasWindows(id), 1);  // the same window, raised
		for (QWidget* w : QApplication::topLevelWidgets()) { if (w->objectName() == QStringLiteral("patcherCanvas-") + id) { w->close(); } }
	}

	//! The canvas shows the input, one node per device in chain order, and the cables the agent reads.
	void theCanvasShowsTheChainAsTheAgentReadsIt()
	{
		freshChain(3);
		const PatcherGraph& graph = canvas()->graph();
		QCOMPARE(graph.nodes.size(), 4);
		QCOMPARE(graph.nodes.at(0).ref, QStringLiteral("input"));
		QCOMPARE(graph.nodes.at(3).ref, QStringLiteral("effect:2"));
		QCOMPARE(graph.edges.size(), 3);  // the derived linear wiring
		QVERIFY(sameAsAgent());
		QCOMPARE(graph.output, QStringLiteral("effect:2"));
		QVERIFY(graph.editable);
		for (int i = 0; i < graph.nodes.size(); ++i) { QVERIFY(canvas()->nodeRect(i).isValid()); }
	}

	//! Drag from an output port to another node's input port: a cable is added, the agent reads the same graph, one undo step.
	void draggingPortToPortConnects()
	{
		freshChain(3);
		drag(canvas()->outputPort(0), canvas()->inputPort(2));  // input -> effect:1 (besides input -> effect:0)
		QCOMPARE(canvas()->graph().edges.size(), 4);
		QVERIFY(canvas()->graph().indexOfEdge(PatcherEdge{QStringLiteral("input"), 0, QStringLiteral("effect:1"), 0}) >= 0);
		QVERIFY(sameAsAgent());
		QCOMPARE(state().value(QStringLiteral("wiring")).toString(), QStringLiteral("authored"));

		QVERIFY(run(QStringLiteral("control.undo")).ok);
		canvas()->refresh();
		QCOMPARE(canvas()->graph().edges.size(), 3);
		QCOMPARE(state().value(QStringLiteral("wiring")).toString(), QStringLiteral("derived"));
	}

	//! Click a cable and press Delete: it is removed, the agent reads the same graph. (A chain must still reach its output, so
	//! the cable removed here is one that has a second route round it.)
	void selectingACableAndPressingDeleteDisconnects()
	{
		freshChain(3);
		drag(canvas()->outputPort(0), canvas()->inputPort(2));  // input -> effect:1: a second route into it
		QCOMPARE(canvas()->graph().edges.size(), 4);
		const PatcherEdge doomed{QStringLiteral("effect:0"), 0, QStringLiteral("effect:1"), 0};
		const int cable = canvas()->graph().indexOfEdge(doomed);
		QVERIFY(cable >= 0);
		QTest::mouseClick(canvas(), Qt::LeftButton, Qt::NoModifier, canvas()->cableMiddle(cable).toPoint());
		QCOMPARE(canvas()->selectedEdge(), cable);
		QTest::keyClick(canvas(), Qt::Key_Delete);
		QCOMPARE(canvas()->graph().edges.size(), 3);
		QCOMPARE(canvas()->graph().indexOfEdge(doomed), -1);
		QVERIFY(sameAsAgent());
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // one undo step brings the cable back
		canvas()->refresh();
		QVERIFY(canvas()->graph().indexOfEdge(doomed) >= 0);
		QVERIFY(sameAsAgent());
	}

	//! The engine refuses a wiring that cuts the chain off from its output; the canvas shows the cable still there.
	void aCutThatStrandsTheOutputIsRefusedAndNothingChanges()
	{
		freshChain(3);
		const int cable = canvas()->graph().indexOfEdge(PatcherEdge{QStringLiteral("effect:0"), 0, QStringLiteral("effect:1"), 0});
		QTest::mouseClick(canvas(), Qt::LeftButton, Qt::NoModifier, canvas()->cableMiddle(cable).toPoint());
		QTest::keyClick(canvas(), Qt::Key_Delete);
		QCOMPARE(canvas()->graph().edges.size(), 3);
		QVERIFY(sameAsAgent());
		QCOMPARE(state().value(QStringLiteral("wiring")).toString(), QStringLiteral("derived"));
	}

	//! Shift-click a cable and drag to another node: that cable's source gets a second destination, the first stays; an agent
	//! command that sets the same wiring reads back the same graph.
	void shiftClickingACableAddsASecondDestination()
	{
		freshChain(3);
		const PatcherEdge original{QStringLiteral("input"), 0, QStringLiteral("effect:0"), 0};
		const int cable = canvas()->graph().indexOfEdge(original);
		QVERIFY(cable >= 0);
		drag(canvas()->cableMiddle(cable), canvas()->inputPort(3), Qt::ShiftModifier);  // second destination: effect:2

		const PatcherEdge added{QStringLiteral("input"), 0, QStringLiteral("effect:2"), 0};
		QVERIFY2(canvas()->graph().indexOfEdge(original) >= 0, "the first cable stayed");
		QVERIFY2(canvas()->graph().indexOfEdge(added) >= 0, "the second destination was added");
		QCOMPARE(canvas()->graph().edges.size(), 4);
		QVERIFY(sameAsAgent());
		const QJsonArray canvasEdges = canvas()->graph().edgesJson();

		// The agent writing the same wiring gets the same graph back.
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(state().value(QStringLiteral("wiring")).toString(), QStringLiteral("derived"));
		const ControlResult set = run(QStringLiteral("patcher.set_wiring"), {{QStringLiteral("target"), m_channel},
			{QStringLiteral("edges"), canvasEdges}, {QStringLiteral("output"), QStringLiteral("effect:2")}});
		QVERIFY2(set.ok, qPrintable(set.errorMessage));
		QCOMPARE(agentEdges(), canvasEdges);
		canvas()->refresh();
		QVERIFY(sameAsAgent());
	}

	//! A plain click on a cable only selects it: no cable is added.
	void clickingACableWithoutShiftOnlySelectsIt()
	{
		freshChain(3);
		const int cable = canvas()->graph().indexOfEdge(PatcherEdge{QStringLiteral("input"), 0, QStringLiteral("effect:0"), 0});
		drag(canvas()->cableMiddle(cable), canvas()->inputPort(3));
		QCOMPARE(canvas()->graph().edges.size(), 3);
		QCOMPARE(canvas()->selectedEdge(), cable);
	}

	//! Move Right / Move Left reorder the chain through plugin.move - the chain's real effect order changes, one undo step.
	void reorderingMovesTheDeviceInTheChain()
	{
		freshChain(3);
		Effect* first = m_chain->effects()[0];
		QTest::mouseClick(canvas(), Qt::LeftButton, Qt::NoModifier, canvas()->nodeRect(1).center().toPoint());
		QCOMPARE(canvas()->selectedNode(), 1);
		auto* right = m_window->findChild<QPushButton*>(QStringLiteral("patcherMoveRight"));
		QVERIFY(right != nullptr);
		right->click();
		QCOMPARE(m_chain->effects()[1], first);
		QCOMPARE(canvas()->selectedNode(), 2);  // the selection follows the device
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(m_chain->effects()[0], first);
		canvas()->refresh();
		QVERIFY(sameAsAgent());
	}

	//! Dragging a node sideways reorders the chain too.
	void draggingANodeSidewaysReordersTheChain()
	{
		freshChain(3);
		Effect* last = m_chain->effects()[2];
		drag(canvas()->nodeRect(3).center(), canvas()->nodeRect(1).center());
		QCOMPARE(m_chain->effects()[0], last);
	}

	//! The Add device button opens the device picker and plugin.load puts the device at the end of the chain: a new node.
	void addingADeviceAppendsANode()
	{
		freshChain(2);
		const QString device = amplifierId();
		if (device.isEmpty()) { QSKIP("this build has no amplifier module"); }
		DevicePicker::setChooser([device](DevicePicker::Kind, const QString&) { return device; });
		auto* add = m_window->findChild<QPushButton*>(QStringLiteral("patcherAddDevice"));
		QVERIFY(add != nullptr);
		add->click();
		DevicePicker::setChooser({});

		QCOMPARE(m_chain->effects().size(), std::size_t{3});
		QCOMPARE(canvas()->graph().nodes.size(), 4);
		QCOMPARE(canvas()->graph().nodes.at(3).ref, QStringLiteral("effect:2"));
		QVERIFY(sameAsAgent());

		// An audio-ports device keeps the chain off the graph: the cables are shown, the canvas says it cannot edit them.
		QVERIFY(!canvas()->graph().editable);
		auto* note = m_window->findChild<QLabel*>(QStringLiteral("patcherNote"));
		QVERIFY(note != nullptr && note->text().contains(canvas()->graph().reason.left(30)));
		const int before = canvas()->graph().edges.size();
		drag(canvas()->outputPort(0), canvas()->inputPort(3));
		QCOMPARE(canvas()->graph().edges.size(), before);  // refused: nothing connected

		QVERIFY(run(QStringLiteral("control.undo")).ok);  // one undo step takes the device back
		canvas()->refresh();
		QCOMPARE(canvas()->graph().nodes.size(), 3);
	}

	//! Remove takes the selected device out of the chain (plugin.unload), one undo step.
	void removingTheSelectedDeviceUnloadsIt()
	{
		const QString amplifier = amplifierId();
		if (amplifier.isEmpty()) { QSKIP("this build has no amplifier module"); }
		freshChain(0);
		for (int i = 0; i < 3; ++i)
		{
			QVERIFY(run(QStringLiteral("plugin.load"), {{QStringLiteral("target"), m_channel}, {QStringLiteral("device"), amplifier}}).ok);
		}
		canvas()->refresh();
		QCOMPARE(canvas()->graph().nodes.size(), 4);
		Effect* survivor = m_chain->effects()[2];
		QTest::mouseClick(canvas(), Qt::LeftButton, Qt::NoModifier, canvas()->nodeRect(2).center().toPoint());
		auto* remove = m_window->findChild<QPushButton*>(QStringLiteral("patcherRemove"));
		QVERIFY(remove != nullptr);
		remove->click();
		QCOMPARE(m_chain->effects().size(), std::size_t{2});
		QCOMPARE(m_chain->effects()[1], survivor);
		QCOMPARE(canvas()->graph().nodes.size(), 3);
		QVERIFY(sameAsAgent());
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(m_chain->effects().size(), std::size_t{3});
	}

	//! Reset Wiring goes back to the derived, linear wiring.
	void resetWiringRestoresTheDerivedWiring()
	{
		freshChain(3);
		drag(canvas()->outputPort(0), canvas()->inputPort(3));
		QCOMPARE(state().value(QStringLiteral("wiring")).toString(), QStringLiteral("authored"));
		auto* reset = m_window->findChild<QPushButton*>(QStringLiteral("patcherResetWiring"));
		QVERIFY(reset != nullptr);
		reset->click();
		QCOMPARE(state().value(QStringLiteral("wiring")).toString(), QStringLiteral("derived"));
		QCOMPARE(canvas()->graph().edges.size(), 3);
	}
};

QTEST_MAIN(PatcherCanvasTest)
#include "PatcherCanvasTest.moc"
