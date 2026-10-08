/*
 * ReversibilityUndoDeviceTest.cpp - the DEVICE half of the SPEC A16 undo proofs:
 *                                   plugin.bypass, plugin.unload and the
 *                                   device-state restore, each applied, undone
 *                                   and read back against the pre-command state.
 *
 * These cases were moved here unchanged from ReversibilityUndoTest.cpp to keep
 * that file under the file-length ratchet. They share its fixture (the same
 * initTestCase/cleanupTestCase) and its contract: every case here SKIPs on a
 * build that exposes no loadable effect, exactly as before the move.
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

#include <QtTest>

#include <memory>

#include <QDomDocument>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QVector>

#include "ControlDeviceSupport.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ControlStructuralSupport.h"
#include "ControlVocabulary.h"
#include "Effect.h"
#include "EffectControls.h"
#include "EffectChain.h"
#include "Engine.h"
#include "Plugin.h"
#include "ProjectJournal.h"
#include "ReversibilityTestSupport.h"

using namespace lmms;
using namespace revtest;

namespace
{

Plugin::Descriptor oversizedSnapshotTestDescriptor{
	"oversized-snapshot-test", "Oversized snapshot test", "", "", 1,
	Plugin::Type::Effect, nullptr, nullptr, nullptr};

class OversizedSnapshotTestControls final : public EffectControls
{
public:
	explicit OversizedSnapshotTestControls(Effect* effect) : EffectControls(effect) {}

	QString nodeName() const override { return QStringLiteral("OversizedSnapshotTestControls"); }
	gui::EffectControlDialog* createView() override { return nullptr; }
	int controlCount() override { return 0; }

protected:
	void saveSettings(QDomDocument&, QDomElement&) override {}
	void loadSettings(const QDomElement&) override {}
};

class OversizedSnapshotTestEffect final : public Effect
{
public:
	explicit OversizedSnapshotTestEffect(Model* parent) :
		Effect(&oversizedSnapshotTestDescriptor, parent, nullptr), m_controls(this) {}

	EffectControls* controls() override { return &m_controls; }

	void saveSettings(QDomDocument& doc, QDomElement& element) override
	{
		Effect::saveSettings(doc, element);
		element.setAttribute(QStringLiteral("payload"), QString(65537, QLatin1Char('x')));
	}

protected:
	ProcessStatus processImpl(SampleFrame*, f_cnt_t) override { return ProcessStatus::Continue; }

private:
	OversizedSnapshotTestControls m_controls;
};

/*! A real Effect, built here rather than LOADED from the device catalogue.
 *
 *  plugin.bypass addresses an Effect by its `fx-<n>` id and calls
 *  setEnabled()/isEnabled() on it (ControlCommandsPlugin.cpp's
 *  registerPluginBypass), so the catalogue is not involved - only the chain and
 *  the id. This build exposes no loadable built-in effect (the device modules
 *  are found through LMMS_PLUGIN_DIR, which these tests must not export), so
 *  firstLoadableEffect() is empty here and every slot built on it SKIPs. The
 *  enabled flag is the whole subject of the case below, and this effect owns one
 *  exactly as a loaded device does.
 */
class RevtestBypassEffect : public Effect
{
public:
	RevtestBypassEffect(Model* parent) : Effect{&s_descriptor, parent, nullptr} {}

	EffectControls* controls() override { return nullptr; }

protected:
	ProcessStatus processImpl(SampleFrame* buf, const f_cnt_t frames) override
	{
		(void)buf;
		(void)frames;
		return ProcessStatus::Continue;
	}

private:
	static const Plugin::Descriptor s_descriptor;
};

const Plugin::Descriptor RevtestBypassEffect::s_descriptor
{
	"revtestbypasseffect",
	"ReversibilityUndoTest bypass flag effect",
	"A pass-through effect whose only state is the enabled flag plugin.bypass drives",
	"LMMS",
	0x0100,
	Plugin::Type::Effect,
	nullptr,
	nullptr,
	nullptr
};

} // namespace

class ReversibilityUndoDeviceTest : public QObject
{
	Q_OBJECT
private slots:

	void initTestCase()
	{
		// The built-in device modules live in the build tree, and the plugin
		// factory finds them through LMMS_PLUGIN_DIR - the same setup
		// ControlDeviceCatalogueTest uses, because the device commands these
		// tests exercise (plugin.load/param_set/bypass, automation.add_point)
		// need a real device to drive.
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! The device commands: bypass and param_set are model checkpoints, and a
	//! state load is an action step that restores the captured XML.
	void deviceCommandsUndoToTheirPreCommandState()
	{
		const QString device = firstLoadableEffect();
		if (device.isEmpty()) { QSKIP("this build exposes no loadable built-in effect"); }
		const QString track = addInstrumentTrack();
		if (track.isEmpty()) { QSKIP("this build exposes no loadable built-in instrument"); }

		const ControlResult loaded = run(QStringLiteral("plugin.load"),
			{{QStringLiteral("target"), track}, {QStringLiteral("device"), device}});
		QVERIFY2(loaded.ok, qPrintable(loaded.errorMessage));
		QCOMPARE(loaded.result.value(QStringLiteral("kind")).toString(), QStringLiteral("effect"));
		const QString fx = loaded.result.value(QStringLiteral("id")).toString();
		const int devicesAfterLoad = deviceCount(track);

		QVERIFY(run(QStringLiteral("plugin.bypass"),
			{{QStringLiteral("target"), track}, {QStringLiteral("plugin"), fx},
				{QStringLiteral("bypass"), true}}).ok);
		QVERIFY(!deviceEnabled(track, fx));
		REV_UNDO_OR_FAIL();
		QVERIFY(deviceEnabled(track, fx));

		// The param write is only meaningful when the device exposes one.
		const QJsonArray parameters = deviceParameters(track, fx);
		if (parameters.isEmpty()) { QSKIP("the loaded effect exposes no parameter"); }
		const QJsonObject parameter = parameters.first().toObject();
		const double previous = parameter.value(QStringLiteral("value")).toDouble();
		const double wanted = parameter.value(QStringLiteral("min")).toDouble() != previous
			? parameter.value(QStringLiteral("min")).toDouble()
			: parameter.value(QStringLiteral("max")).toDouble();
		QVERIFY(run(QStringLiteral("plugin.param_set"),
			{{QStringLiteral("target"), track}, {QStringLiteral("plugin"), fx},
				{QStringLiteral("index"), parameter.value(QStringLiteral("index")).toInt()},
				{QStringLiteral("value"), wanted}}).ok);
		QCOMPARE(deviceParameterValue(track, fx, parameter.value(QStringLiteral("index")).toInt()), wanted);
		REV_UNDO_OR_FAIL();
		QCOMPARE(deviceParameterValue(track, fx, parameter.value(QStringLiteral("index")).toInt()), previous);

		// The append itself is one step: undo unloads the device it created.
		QCOMPARE(deviceCount(track), devicesAfterLoad);
		const QJsonObject loadTx = stateOf(QStringLiteral("plugin.load"));
		QCOMPARE(loadTx.value(QStringLiteral("reversible")).toBool(), true);

		// ... and the state load restores the settings it replaced.
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString file = dir.path() + QStringLiteral("/state.xml");
		QVERIFY(run(QStringLiteral("plugin.state_save"),
			{{QStringLiteral("target"), track}, {QStringLiteral("plugin"), fx},
				{QStringLiteral("path"), file}}).ok);
		QVERIFY(QFileInfo::exists(file));
		QVERIFY(run(QStringLiteral("plugin.param_set"),
			{{QStringLiteral("target"), track}, {QStringLiteral("plugin"), fx},
				{QStringLiteral("index"), parameter.value(QStringLiteral("index")).toInt()},
				{QStringLiteral("value"), wanted}}).ok);
		QCOMPARE(deviceParameterValue(track, fx, parameter.value(QStringLiteral("index")).toInt()), wanted);
		QVERIFY(run(QStringLiteral("plugin.state_load"),
			{{QStringLiteral("target"), track}, {QStringLiteral("plugin"), fx},
				{QStringLiteral("path"), file}}).ok);
		QCOMPARE(deviceParameterValue(track, fx, parameter.value(QStringLiteral("index")).toInt()), previous);
		REV_UNDO_OR_FAIL();
		QCOMPARE(deviceParameterValue(track, fx, parameter.value(QStringLiteral("index")).toInt()), wanted);
	}


	/*! Luna cluster 4865: plugin.bypass's recorded INVERSE must be replayable, and
	 *  replaying it must restore the state the command found.
	 *
	 *  The two flags have OPPOSITE meanings and the inverse had them the wrong way
	 *  round: the handler applies `setEnabled(!bypass)`, but the inverse stored
	 *  `previouslyEnabled` AS the `bypass` argument. So an effect that was enabled
	 *  and got bypassed recorded an inverse that bypasses it again, and one that
	 *  was disabled recorded an inverse that leaves it enabled - the inverse
	 *  re-applied the change instead of undoing it. control.undo never noticed,
	 *  because this command's record is journal-class (an Effect enabled-model
	 *  checkpoint) and undoes through the ProjectJournal, not through these
	 *  args: the payload was only ever wrong for a caller that FOLLOWS it, which
	 *  is exactly what `__transaction.inverse` promises a caller it can.
	 */
	void theBypassInverseReplaysToTheStateBeforeTheCommand()
	{
		const QString track = addTrack(QStringLiteral("instrument"));
		QVERIFY2(!track.isEmpty(), "track.add refused: this case cannot be measured");
		ControlTarget target;
		ControlResult error;
		QVERIFY2(resolveControlTarget(track, &target, &error),
			qPrintable(error.errorMessage));
		Effect* effect = new RevtestBypassEffect(target.chain);
		target.chain->appendEffect(effect);
		const QString fx = control::effectId(effect->id());

		// (a) An ENABLED effect, bypassed: the inverse must re-enable it.
		QVERIFY(effect->isEnabled());
		const ControlResult bypassed = run(QStringLiteral("plugin.bypass"),
			{{QStringLiteral("target"), track}, {QStringLiteral("plugin"), fx},
				{QStringLiteral("bypass"), true}});
		QVERIFY2(bypassed.ok, qPrintable(bypassed.errorMessage));
		QVERIFY(!effect->isEnabled());

		const QJsonObject inverse = stateOf(QStringLiteral("plugin.bypass"))
			.value(QStringLiteral("inverse")).toObject();
		QCOMPARE(inverse.value(QStringLiteral("op")).toString(), QStringLiteral("plugin.bypass"));
		const QJsonObject inverseArgs = inverse.value(QStringLiteral("args")).toObject();
		QVERIFY2(!inverseArgs.isEmpty(), "plugin.bypass recorded an inverse with no args");
		QVERIFY2(inverseArgs.value(QStringLiteral("bypass")).toBool() == false,
			"the inverse of bypassing an enabled effect asks to bypass it AGAIN "
			"(bypass=true re-applies the change instead of undoing it)");
		QVERIFY2(run(inverse.value(QStringLiteral("op")).toString(), inverseArgs).ok,
			"the recorded inverse is not replayable");
		QVERIFY2(effect->isEnabled(),
			"replaying the recorded inverse did not put the effect back in the state "
			"plugin.bypass found it in");

		// (b) The other direction: un-bypassing a DISABLED effect records an inverse
		// that bypasses it again, and the replay puts it back to disabled. The
		// effect is disabled first on purpose - (a) left it enabled, and running
		// bypass=false from there is a no-op that records nothing about the disabled
		// state this half is about.
		QVERIFY2(run(QStringLiteral("plugin.bypass"),
			{{QStringLiteral("target"), track}, {QStringLiteral("plugin"), fx},
				{QStringLiteral("bypass"), true}}).ok, "probe (b)");
		QVERIFY(!effect->isEnabled());
		QVERIFY(run(QStringLiteral("plugin.bypass"),
			{{QStringLiteral("target"), track}, {QStringLiteral("plugin"), fx},
				{QStringLiteral("bypass"), false}}).ok);
		QVERIFY(effect->isEnabled());
		const QJsonObject undoArgs = stateOf(QStringLiteral("plugin.bypass"))
			.value(QStringLiteral("inverse")).toObject().value(QStringLiteral("args")).toObject();
		QVERIFY2(undoArgs.value(QStringLiteral("bypass")).toBool() == true,
			"the inverse of un-bypassing a disabled effect asks to un-bypass it AGAIN "
			"(bypass=false leaves the effect enabled instead of restoring it disabled)");
		QVERIFY(run(QStringLiteral("plugin.bypass"), undoArgs).ok);
		QVERIFY2(!effect->isEnabled(),
			"replaying the second inverse did not restore the disabled state");

		target.chain->removeEffect(effect);
		effect->deleteLater();
	}


	/*! BUG-CTL-1 (SPEC A16): the undo of plugin.unload must NOT report success when
	 *  the recreated device could not be given its captured settings. Before the fix
	 *  recreateEffectFromState() discarded controlRestoreEffectState()'s result, so
	 *  the undo answered `undone: true` over a device sitting at its DEFAULTS.
	 *
	 *  The refusal is driven through the PRODUCTION recreate path with a document whose
	 *  body element is removed: one controlEffectStateXml() captured from a live device
	 *  is always restorable by that same engine, so plugin.unload cannot reach it. The
	 *  step is pushed with the API journalEffectRemoval() uses.
	 */
	void unloadUndoRefusesWhenTheDeviceCannotBeRestored()
	{
		const QString device = firstLoadableEffect();
		if (device.isEmpty()) { QSKIP("this build exposes no loadable built-in effect"); }
		const QString track = addInstrumentTrack();
		if (track.isEmpty()) { QSKIP("this build exposes no loadable built-in instrument"); }
		ControlTarget target; ControlResult error;
		QVERIFY2(resolveControlTarget(track, &target, &error), qPrintable(error.errorMessage));

		const ControlResult loaded = run(QStringLiteral("plugin.load"),
			{{QStringLiteral("target"), track}, {QStringLiteral("device"), device}});
		QVERIFY2(loaded.ok, qPrintable(loaded.errorMessage));
		const QString fx = loaded.result.value(QStringLiteral("id")).toString();

		// The happy path is untouched: the undo reports success and the device is back.
		QVERIFY(run(QStringLiteral("plugin.unload"),
			{{QStringLiteral("target"), track}, {QStringLiteral("plugin"), fx}}).ok);
		QCOMPARE(deviceCount(track), 0);
		REV_UNDO_OR_FAIL();
		QCOMPARE(deviceCount(track), 1);

		// The defect: a document the DEVICE refuses, naming a device this build can build.
		const QString restored = deviceIdAt(track, 0);
		Effect* live = resolveControlEffect(target, restored, &error);
		QVERIFY2(live != nullptr, qPrintable(error.errorMessage));
		QDomDocument document;
		QVERIFY(document.setContent(controlEffectStateXml(live)));
		const QDomElement body = document.documentElement().firstChildElement(live->nodeName());
		QVERIFY2(!body.isNull(), "the captured device document carries no body to remove");
		document.documentElement().removeChild(body);
		const QString refused = document.toString();

		// plugin.unload again, the REAL command (so the transaction control.undo consults
		// is the one it recorded), then the step it recorded with the refused document.
		QVERIFY(run(QStringLiteral("plugin.unload"),
			{{QStringLiteral("target"), track}, {QStringLiteral("plugin"), restored}}).ok);
		QCOMPARE(deviceCount(track), 0);
		auto holder = std::make_shared<Effect*>(nullptr);
		control::addStructuralUndoStep(
			[chain = target.chain, refused, holder]() {
				*holder = control::recreateEffectFromState(chain, refused, 0);
			},
			[chain = target.chain, holder]() {
				if (*holder != nullptr) { chain->removeEffect(*holder); (*holder)->deleteLater(); *holder = nullptr; }
			},
			refused.size());

		// (1) the undo must NOT report success - `undone: true` here was the lie ...
		const ControlResult undo = run(QStringLiteral("control.undo"));
		QVERIFY2(!undo.ok, "control.undo reported SUCCESS although the device was not restored "
			"(BUG-CTL-1): the recreated instance sat at its defaults");
		QVERIFY(undo.errorKind == ControlErrorKind::InvalidArgs
			|| undo.errorKind == ControlErrorKind::Refused);
		QVERIFY2(undo.errorMessage.contains(QStringLiteral("NOT restored")), qPrintable(undo.errorMessage));
		QCOMPARE(deviceCount(track), 0);  // (2) no half-restored device left behind
		// (3) ... and repeated undo/redo stays honest. No depth delta is asserted:
		// undo_depth also counts the chain's own enabled-flag checkpoints (appendEffect/
		// removeEffect), a pre-existing EffectChain behaviour the happy path has too.
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QCOMPARE(deviceCount(track), 0);
		QVERIFY2(!run(QStringLiteral("control.undo")).ok, "a second undo of the failed restore "
			"reported SUCCESS (BUG-CTL-1)");
		QCOMPARE(deviceCount(track), 0);
		QVERIFY(run(QStringLiteral("track.list")).ok);
	}


	//! An over-limit unload must not advertise truncated XML as a restorable snapshot.
	void unloadOverLimitSnapshotIsNotPresentedAsRestorableXml()
	{
		const QString track = addTrack();
		QVERIFY(!track.isEmpty());
		ControlTarget target; ControlResult error;
		QVERIFY2(resolveControlTarget(track, &target, &error), qPrintable(error.errorMessage));

		auto* effect = new OversizedSnapshotTestEffect(target.chain);
		target.chain->appendEffect(effect);
		const QString fx = QStringLiteral("fx-%1").arg(effect->id());
		const QString xml = controlEffectStateXml(effect);
		QVERIFY(xml.size() > 65536);
		QDomDocument completeDocument;
		QVERIFY(completeDocument.setContent(xml));

		const ControlResult unloaded = run(QStringLiteral("plugin.unload"),
			{{QStringLiteral("target"), track}, {QStringLiteral("plugin"), fx}});
		QVERIFY2(unloaded.ok, qPrintable(unloaded.errorMessage));

		const QJsonObject tx = stateOf(QStringLiteral("plugin.unload"));
		const QJsonObject before = tx.value(QStringLiteral("before")).toObject();
		QCOMPARE(before.value(QStringLiteral("state_truncated")).toBool(), true);
		QVERIFY2(before.value(QStringLiteral("state_xml")).toString().isEmpty(),
			"plugin.unload exposed truncated XML as a usable snapshot");
		QCOMPARE(tx.value(QStringLiteral("reversible")).toBool(), false);
		const QString note = tx.value(QStringLiteral("inverse")).toObject()
			.value(QStringLiteral("args")).toObject().value(QStringLiteral("note")).toString();
		QVERIFY(note.contains(QStringLiteral("no usable state snapshot")));

		const ControlCommand* unloadCommand = ControlRegistry::instance()->command(QStringLiteral("plugin.unload"));
		QVERIFY(unloadCommand != nullptr);
		QVERIFY(unloadCommand->description.contains(QStringLiteral("'state_xml' is empty")));
		QVERIFY(unloadCommand->description.contains(QStringLiteral("'state_truncated' is true")));
		QVERIFY(unloadCommand->description.contains(QStringLiteral("cannot be manually restored from that snapshot")));
	}
};

QTEST_GUILESS_MAIN(ReversibilityUndoDeviceTest)
#include "ReversibilityUndoDeviceTest.moc"
