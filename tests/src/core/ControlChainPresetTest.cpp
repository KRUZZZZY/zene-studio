/*
 * ControlChainPresetTest.cpp - the chain.* command group's SURFACE (SPEC A11-A16,
 *                              the release contract section 3.1).
 *
 * The ENGINE half is include/ControlChainPresetSupport.h; this file holds the
 * surface to account, in process: the six registered ids with their schemas, the
 * six contract rows, the store's name rule and its location, the preset
 * document's identity round trip, and - the part that is not optional - the
 * measured effect of an apply. A command that answered ok and applied nothing
 * fails here: the second track's device list AND its parameter values are read
 * back through dsp.get_state and compared with the chain that was captured.
 *
 * The end-to-end proof that an AGENT can drive all of this over a real socket,
 * including a real project.save -> project.open round trip, is the registered
 * ctest ControlChainPresets (tests/control-chain-presets.py).
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

#include <QDir>
#include <QDomDocument>
#include <QDomElement>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include "ControlChainPresetSupport.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ConfigManager.h"
#include "Engine.h"
#include "ReversibilityTestSupport.h"

using namespace lmms;
using namespace revtest;

namespace
{

//! The preset every check of this file uses.
const QString kPreset = QStringLiteral("ControlChainPresetTest Chain");
//! ... and the name a rename takes it to.
const QString kRenamed = QStringLiteral("ControlChainPresetTest Renamed");

//! The six ids of the group, in the order the registration docs list them.
QStringList chainIds()
{
	return {
		QStringLiteral("chain.list"), QStringLiteral("chain.get_state"),
		QStringLiteral("chain.save"), QStringLiteral("chain.apply"),
		QStringLiteral("chain.rename"), QStringLiteral("chain.remove"),
	};
}

/*! A device state document of the shape plugin.state_save writes one.
 *
 *  Written by hand on purpose: the identity rules under test are the document's
 *  own attributes (plugin, hosted_file, hosted_id, hosted_uri), and a hand-built
 *  document is the only way to assert them without depending on which devices a
 *  build happens to ship.
 */
QByteArray deviceDocument(const QString& plugin, const QString& file, const QString& label,
	int gain)
{
	QDomDocument doc;
	QDomElement root = doc.createElement(QStringLiteral("zenepluginstate"));
	root.setAttribute(QStringLiteral("version"), 1);
	root.setAttribute(QStringLiteral("plugin"), plugin);
	if (!file.isEmpty()) { root.setAttribute(QStringLiteral("hosted_file"), file); }
	if (!label.isEmpty()) { root.setAttribute(QStringLiteral("hosted_id"), label); }
	doc.appendChild(root);
	QDomElement body = doc.createElement(QStringLiteral("effect"));
	body.setAttribute(QStringLiteral("on"), 1);
	body.setAttribute(QStringLiteral("gain"), gain);
	root.appendChild(body);
	return doc.toString().toUtf8();
}

//! A stored device of a document built above.
ControlChainPresetDevice deviceOf(const QByteArray& state)
{
	ControlChainPresetDevice device;
	device.state = state;
	ControlResult error;
	controlChainPresetIdentity(&device, &error);
	return device;
}

//! Every preset the store holds, by name.
QStringList storeNames()
{
	QStringList names;
	for (const QJsonValue& value : run(QStringLiteral("chain.list"))
		.result.value(QStringLiteral("presets")).toArray())
	{
		names.append(value.toObject().value(QStringLiteral("name")).toString());
	}
	return names;
}

//! The plugin name of every device of a preset, in the preset's own order.
QStringList presetPlugins(const QString& name)
{
	QStringList plugins;
	for (const QJsonValue& value : run(QStringLiteral("chain.get_state"),
			QJsonObject{{QStringLiteral("name"), name}})
		.result.value(QStringLiteral("devices")).toArray())
	{
		plugins.append(value.toObject().value(QStringLiteral("plugin")).toString());
	}
	return plugins;
}

//! The plugin name of every device of a live target, in the chain's own order.
QStringList livePlugins(const QString& target)
{
	QStringList plugins;
	for (const QJsonValue& value : deviceChain(target))
	{
		plugins.append(value.toObject().value(QStringLiteral("plugin")).toString());
	}
	return plugins;
}

/*! Every parameter value of every device of a live target, as ONE string:
 *  "fx-0:<plugin>:<value>,<value>|fx-1:...". A string rather than a nested list
 *  because a failure has to NAME the value that differs, and because it compares
 *  the devices in the chain's own order.
 */
QString liveValues(const QString& target)
{
	QStringList devices;
	for (const QJsonValue& value : deviceChain(target))
	{
		const QJsonObject device = value.toObject();
		QStringList values;
		for (const QJsonValue& parameter : device.value(QStringLiteral("parameters")).toArray())
		{
			values.append(QString::number(
				parameter.toObject().value(QStringLiteral("value")).toDouble(), 'g', 12));
		}
		devices.append(device.value(QStringLiteral("plugin")).toString()
			+ QLatin1Char(':') + values.join(QLatin1Char(',')));
	}
	return devices.join(QLatin1Char('|'));
}

//! Captures \a target's chain as \a name.
ControlResult savePreset(const QString& target, const QString& name)
{
	return run(QStringLiteral("chain.save"), QJsonObject{{QStringLiteral("target"), target},
		{QStringLiteral("name"), name}, {QStringLiteral("overwrite"), true}});
}

//! A preset of every loadable effect of this build, up to \a wanted of them.
int loadEffects(const QString& target, int wanted, QStringList* ids)
{
	const QStringList devices = loadableDevices(QStringLiteral("effect"));
	int loaded = 0;
	for (const QString& device : devices)
	{
		if (loaded >= wanted) { break; }
		const ControlResult result = run(QStringLiteral("plugin.load"),
			QJsonObject{{QStringLiteral("target"), target}, {QStringLiteral("device"), device}});
		if (!result.ok) { continue; }
		ids->append(result.result.value(QStringLiteral("id")).toString());
		++loaded;
	}
	return loaded;
}

} // namespace

class ControlChainPresetTest : public QObject
{
	Q_OBJECT
private slots:

	//! A private working directory, so the store this test writes is a temp one:
	//! a test must never touch the developer's own preset tree.
	void initTestCase()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
		Engine::init(true);
		ControlRegistry::setReady(true);
		QVERIFY(m_world.isValid());
		ConfigManager::inst()->setWorkingDir(m_world.path());
	}

	void cleanupTestCase()
	{
		// Leave no preset behind, whatever an earlier check did.
		run(QStringLiteral("chain.remove"), QJsonObject{{QStringLiteral("name"), kPreset}});
		run(QStringLiteral("chain.remove"), QJsonObject{{QStringLiteral("name"), kRenamed}});
		run(QStringLiteral("chain.remove"),
			QJsonObject{{QStringLiteral("name"), QStringLiteral("ControlChainPresetTest Renamed2")}});
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! Every command of the group declares the contract's parts.
	void requiredCommandsAreRegistered()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const QStringList mutating = {
			QStringLiteral("chain.save"), QStringLiteral("chain.apply"),
			QStringLiteral("chain.rename"), QStringLiteral("chain.remove")};
		for (const QString& id : chainIds())
		{
			QVERIFY2(registry->hasCommand(id), qPrintable(QStringLiteral("missing %1").arg(id)));
			const ControlCommand* cmd = registry->command(id);
			QVERIFY(cmd != nullptr);
			QCOMPARE(cmd->group, QStringLiteral("chain"));
			QVERIFY(!cmd->verb.isEmpty());
			QVERIFY(!cmd->description.isEmpty());
			QVERIFY(!cmd->argsSchema.isEmpty());
			QVERIFY(!cmd->resultSchema.isEmpty());
			QVERIFY2(cmd->requiresDecl.isEmpty(), qPrintable(id + " declares a requires excuse"));
			QCOMPARE(cmd->mutating, mutating.contains(id));
		}
	}

	//! The contract table classifies the group: four recorded-action
	//! true_inverse rows and two read-only rows, each with its reason.
	void contractRowsClassifyTheGroup()
	{
		const control::ReversibilityTable& table = control::ReversibilityTable::instance();
		for (const QString& id : chainIds())
		{
			const control::ReversibilityEntry* row = table.lookup(id);
			QVERIFY2(row != nullptr, qPrintable(id + " has no contract row"));
			QVERIFY(!row->reason.isEmpty());
			QVERIFY(!row->mechanism.isEmpty());
			const QString expected = id.startsWith(QLatin1String("chain.list"))
					|| id == QLatin1String("chain.get_state")
				? QStringLiteral("not_mutating") : QStringLiteral("true_inverse");
			QCOMPARE(control::reversibilityClassName(row->cls), expected);
			QCOMPARE(row->reversible, expected == QLatin1String("true_inverse"));
		}
		// The apply's inverse names the mechanism it really uses, not a promise:
		// the chain's own <fxchain> XML written back through the loader's path.
		QVERIFY(table.lookup(QStringLiteral("chain.apply"))->mechanism.contains(
			QStringLiteral("<fxchain>")));
		QVERIFY(table.lookup(QStringLiteral("chain.apply"))->mechanism.contains(
			QStringLiteral("EffectChain::loadSettings")));
		// ... and the removal's says the bytes go back, which is what control.undo
		// then does.
		QVERIFY(table.lookup(QStringLiteral("chain.remove"))->mechanism.contains(
			QStringLiteral("byte for byte")));
	}

	//! The name rule: a preset name is a bare file name, and it never escapes.
	void theNameRuleRefusesANameThatWouldEscapeTheStore()
	{
		ControlResult error;
		QString path;
		QVERIFY(!controlChainPresetPath(QStringLiteral("../escape"), &path, &error));
		QVERIFY(!error.errorMessage.isEmpty());
		QVERIFY(!controlChainPresetPath(QStringLiteral("a/b"), &path, &error));
		QVERIFY(!controlChainPresetPath(QString(), &path, &error));
		QVERIFY(!controlChainPresetPath(QStringLiteral(".hidden"), &path, &error));
		QVERIFY(controlChainPresetPath(QStringLiteral("Fine Name"), &path, &error));
		QCOMPARE(path, controlChainPresetDir() + QStringLiteral("Fine Name.zcp"));
		QVERIFY(controlChainPresetPath(QStringLiteral("Fine Name.zcp"), &path, &error));
		QCOMPARE(path, controlChainPresetDir() + QStringLiteral("Fine Name.zcp"));
	}

	//! The store is the user preset tree, OUTSIDE the project - the property that
	//! makes a preset usable in another project at all.
	void theStoreIsTheUserPresetTreeAndNotTheProject()
	{
		QVERIFY(QDir::isAbsolutePath(controlChainPresetDir()));
		QCOMPARE(controlChainPresetDir(),
			ConfigManager::inst()->userPresetsDir() + QStringLiteral("chainpresets/"));
		// The project's own directory (working/projects) is a different tree: a
		// preset is never written under it.
		QVERIFY(!controlChainPresetDir().startsWith(ConfigManager::inst()->userProjectsDir()));
	}

	//! A document keeps every device, in order, with the identity its own state
	//! document carries - and the state bytes come back unchanged.
	void aPresetKeepsEveryDeviceInOrderWithItsOwnIdentity()
	{
		const QByteArray builtin = deviceDocument(QStringLiteral("amplifier"), QString(), QString(),
			7);
		const QByteArray ladspa = deviceDocument(QStringLiteral("ladspaeffect"),
			QStringLiteral("amp"), QStringLiteral("amp_mono"), 3);
		QList<ControlChainPresetDevice> devices{deviceOf(builtin), deviceOf(ladspa)};
		QCOMPARE(devices.at(0).format, QStringLiteral("builtin"));
		QCOMPARE(devices.at(0).plugin, QStringLiteral("amplifier"));
		QCOMPARE(devices.at(1).format, QStringLiteral("ladspa"));
		QCOMPARE(devices.at(1).file, QStringLiteral("amp"));
		QCOMPARE(devices.at(1).label, QStringLiteral("amp_mono"));

		const QString path = controlChainPresetDir() + QStringLiteral("document-test.zcp");
		QDir().mkpath(controlChainPresetDir());
		ControlResult error;
		QVERIFY(controlWriteFileBytes(path,
			controlChainPresetDocument(QStringLiteral("document-test"), devices), true, &error));

		ControlChainPreset read;
		QVERIFY2(controlReadChainPreset(path, &read, &error), qPrintable(error.errorMessage));
		QCOMPARE(read.name, QStringLiteral("document-test"));
		QCOMPARE(read.devices.size(), 2);
		// Order and identity, and the state documents byte for byte.
		QCOMPARE(read.devices.at(0).plugin, QStringLiteral("amplifier"));
		QCOMPARE(read.devices.at(1).label, QStringLiteral("amp_mono"));
		QCOMPARE(read.devices.at(0).state, builtin);
		QCOMPARE(read.devices.at(1).state, ladspa);
		QVERIFY(QFile::remove(path));
	}

	//! A document that is not a device state is refused, typed, and names why.
	void aStateDocumentThatIsNotZenePluginStateIsRefused()
	{
		ControlChainPresetDevice device;
		device.state = QByteArray("<notadevice plugin=\"x\"/>");
		ControlResult error;
		QVERIFY(!controlChainPresetIdentity(&device, &error));
		QCOMPARE(error.errorKind, ControlErrorKind::InvalidArgs);
		QVERIFY(error.errorMessage.contains(QStringLiteral("zenepluginstate")));

		// An identity no build has is a typed not_found, not a silent apply.
		ControlDeviceEntry entry;
		int index = -1;
		const ControlChainPresetDevice missing = deviceOf(deviceDocument(
			QStringLiteral("ladspaeffect"), QStringLiteral("no-such-file"),
			QStringLiteral("no_such_label"), 0));
		QVERIFY(!controlChainPresetEntry(missing, &entry, &index, &error));
		QCOMPARE(error.errorKind, ControlErrorKind::NotFound);
		// ... and the refusal names the device in the FORMAT's own terms (a bare
		// "ladspaeffect" names every LADSPA device, so the label and file are named).
		QVERIFY2(error.errorMessage.contains(QStringLiteral("no_such_label (no-such-file)")),
			qPrintable(error.errorMessage));
	}

	/*! THE MEASURED EFFECT. A chain captured from one track, applied to a second,
	 *  must give that second track the same ordered device list and the same
	 *  parameter values - read back through dsp.get_state, not asserted from the
	 *  command's own answer.
	 */
	void aCapturedChainAppliesToASecondTrackWithTheSameDevicesAndValues()
	{
		const QString source = addInstrumentTrack();
		const QString target = addInstrumentTrack();
		QVERIFY2(!source.isEmpty() && !target.isEmpty(), "this build has no instrument track");
		QStringList ids;
		QCOMPARE(loadEffects(source, 2, &ids), 2);
		QCOMPARE(deviceCount(target), 0);

		// A parameter away from its default, so "the same values" means something,
		// on the first loaded device that HAS one.
		QString parameterised;
		for (const QString& fx : ids)
		{
			if (!deviceParameters(source, fx).isEmpty()) { parameterised = fx; break; }
		}
		QVERIFY2(!parameterised.isEmpty(), "no loaded effect exposes a parameter");
		const QJsonObject parameter = deviceParameters(source, parameterised).at(0).toObject();
		const double wanted = (parameter.value(QStringLiteral("min")).toDouble()
			+ parameter.value(QStringLiteral("max")).toDouble()) / 3.0;
		QVERIFY(run(QStringLiteral("plugin.param_set"),
			QJsonObject{{QStringLiteral("target"), source},
				{QStringLiteral("plugin"), parameterised},
				{QStringLiteral("index"), 0}, {QStringLiteral("value"), wanted}}).ok);
		const QString parameterisedId = QStringLiteral("fx-%1").arg(ids.indexOf(parameterised));

		const QStringList sourcePlugins = livePlugins(source);
		const QString sourceValues = liveValues(source);
		QCOMPARE(sourcePlugins.size(), 2);

		const ControlResult saved = run(QStringLiteral("chain.save"),
			QJsonObject{{QStringLiteral("target"), source}, {QStringLiteral("name"), kPreset},
				{QStringLiteral("overwrite"), true}});
		QVERIFY2(saved.ok, qPrintable(saved.errorMessage));
		QCOMPARE(saved.result.value(QStringLiteral("device_count")).toInt(), 2);
		QVERIFY(saved.result.value(QStringLiteral("bytes")).toInt() > 0);
		QCOMPARE(presetPlugins(kPreset), sourcePlugins);
		QCOMPARE(storeNames().contains(kPreset), true);

		const ControlResult applied = run(QStringLiteral("chain.apply"),
			QJsonObject{{QStringLiteral("name"), kPreset}, {QStringLiteral("target"), target}});
		QVERIFY2(applied.ok, qPrintable(applied.errorMessage));
		QCOMPARE(applied.result.value(QStringLiteral("replaced")).toInt(), 0);

		// THE REAL EFFECT: the same devices in the same ORDER, and the same
		// values, read off the target itself.
		QCOMPARE(livePlugins(target), sourcePlugins);
		QCOMPARE(liveValues(target), sourceValues);
		// A device parameter is a float, so the value that comes back is the float
		// the engine stored: compare within float precision, not bit for bit.
		QVERIFY(qAbs(deviceParameterValue(target, parameterisedId, 0) - wanted) < 1e-6);

		// The A16 record classes it as the table says, and the undo really
		// reverses it: the target is empty again.
		QCOMPARE(stateOf(QStringLiteral("chain.apply")).value(QStringLiteral("class")).toString(),
			QStringLiteral("true_inverse"));
		REV_UNDO_OR_FAIL();
		QCOMPARE(deviceCount(target), 0);
	}

	//! Rename and remove are reversible too, and the store is left as it was.
	void theStoreEditsAreReversible()
	{
		const QString source = addInstrumentTrack();
		QStringList ids;
		QVERIFY2(loadEffects(source, 1, &ids) == 1, "this build has no loadable effect");
		QVERIFY2(savePreset(source, kPreset).ok, "the preset could not be captured");
		// A capture of an EMPTY chain is refused: there is nothing to store.
		const ControlResult empty = run(QStringLiteral("chain.save"),
			QJsonObject{{QStringLiteral("target"), addInstrumentTrack()},
				{QStringLiteral("name"), kRenamed}, {QStringLiteral("overwrite"), true}});
		QVERIFY(!empty.ok);
		QCOMPARE(empty.errorKind, ControlErrorKind::InvalidArgs);

		QVERIFY(savePreset(source, kRenamed).ok);
		const QStringList before = storeNames();
		QVERIFY(!run(QStringLiteral("chain.rename"),
			QJsonObject{{QStringLiteral("name"), kPreset}, {QStringLiteral("to"), kRenamed}}).ok);
		QCOMPARE(storeNames(), before);

		QVERIFY(run(QStringLiteral("chain.rename"),
			QJsonObject{{QStringLiteral("name"), kPreset},
				{QStringLiteral("to"), QStringLiteral("ControlChainPresetTest Renamed2")}}).ok);
		QVERIFY(storeNames().contains(QStringLiteral("ControlChainPresetTest Renamed2")));
		REV_UNDO_OR_FAIL();
		QVERIFY(storeNames().contains(kPreset));

		QVERIFY(run(QStringLiteral("chain.remove"),
			QJsonObject{{QStringLiteral("name"), kPreset}}).ok);
		QVERIFY(!storeNames().contains(kPreset));
		REV_UNDO_OR_FAIL();
		QVERIFY(storeNames().contains(kPreset));
		run(QStringLiteral("chain.remove"), QJsonObject{{QStringLiteral("name"), kPreset}});
	}

private:
	QTemporaryDir m_world;
};

QTEST_GUILESS_MAIN(ControlChainPresetTest)

#include "ControlChainPresetTest.moc"
