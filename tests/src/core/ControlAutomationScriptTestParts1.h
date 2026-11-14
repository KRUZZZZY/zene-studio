/*
 * ControlAutomationScriptTest.cpp - unit tests for the automation.* and
 *                                   script.* command groups
 *                                   (SPEC-zene-studio.md A11-A16).
 *
 * The end-to-end behaviour (rendered audio, control.undo, the wire) lives in
 * tests/control-socket-integration.py; this file pins the registry surface those
 * flows rely on: the ids, the schemas, the typed refusal each error path must
 * produce, and the fact that script.run drives the embedded ScriptEngine rather
 * than a child process.
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

// Complete fixture method definitions; included once after the fixture declaration.

void ControlAutomationScriptTest::initTestCase() {
	// The built-in device modules live in the build tree, and the plugin
	// factory finds them through LMMS_PLUGIN_DIR - the preamble
	// ReversibilityUndoTest and ControlChainPresetTest use, and the reason
	// tests/CMakeLists.txt puts this target on the LMMS_TEST_PLUGIN_DIR list.
#ifdef LMMS_TEST_PLUGIN_DIR
	qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
	Engine::init(true);
	ControlRegistry::setReady(true);
	// THE PARAMETER THE MODE COMMANDS DRIVE, built through the surface. A
	// bare Engine::init song has NO track at all (the default project is
	// loaded by main(), which a test binary never runs) and
	// automation.get_state reports the parameters of the devices that exist
	// - which is why the two slots below saw an empty `tracks` array while
	// the file's comment said "a fresh song has at least one track with
	// parameters". The modules are needed AND the track has to be made.
	m_devicedTrack = revtest::addInstrumentTrack();
}
void ControlAutomationScriptTest::cleanupTestCase() {
	ControlRegistry::setReady(false);
	Engine::destroy();
}
void ControlAutomationScriptTest::automationCommandsAreRegistered() {
	ControlRegistry* registry = ControlRegistry::instance();
	const QVector<QPair<QString, bool>> required = {
		{qstr("automation.get_state"), false},
		{qstr("automation.add_point"), true},
		{qstr("automation.remove_point"), true},
		{qstr("automation.clear"), true},
		{qstr("automation.mode_set"), true},
		{qstr("automation.record_mode_set"), true},
	};
	for (const auto& entry : required)
	{
		const ControlCommand* command = registry->command(entry.first);
		QVERIFY2(command != nullptr, qPrintable(entry.first + " is not registered"));
		QCOMPARE(command->id, entry.first);
		QCOMPARE(command->group + QStringLiteral(".") + command->verb, entry.first);
		QCOMPARE(command->mutating, entry.second);
		QVERIFY(!command->description.isEmpty());
		QVERIFY2(!command->argsSchema.isEmpty(), qPrintable(entry.first + " has no args schema"));
		QVERIFY2(!command->resultSchema.isEmpty(), qPrintable(entry.first + " has no result schema"));
		QVERIFY(command->requiresEngine);
	}
}
void ControlAutomationScriptTest::scriptCommandsAreRegistered() {
	ControlRegistry* registry = ControlRegistry::instance();
	const ControlCommand* run = registry->command(QStringLiteral("script.run"));
	QVERIFY(run != nullptr);
	QCOMPARE(run->group, QStringLiteral("script"));
	QCOMPARE(run->verb, QStringLiteral("run"));
	QVERIFY(run->mutating);
	QVERIFY(run->requiresEngine);
	QVERIFY(!run->argsSchema.isEmpty());
	QVERIFY(!run->resultSchema.isEmpty());

	const ControlCommand* list = registry->command(QStringLiteral("script.list"));
	QVERIFY(list != nullptr);
	QCOMPARE(list->group, QStringLiteral("script"));
	QCOMPARE(list->verb, QStringLiteral("list"));
	QVERIFY(!list->mutating);
	// Listing the shipped files needs no song and no mixer.
	QVERIFY(!list->requiresEngine);
}
void ControlAutomationScriptTest::automationGetStateOnAFreshSongIsHonest() {
	ControlRegistry* registry = ControlRegistry::instance();
	const ControlResult state = registry->invoke(QStringLiteral("automation.get_state"));
	QVERIFY2(state.ok, qPrintable(state.errorMessage));
	// Whatever the fresh song holds, every track it has is reported and
	// nothing is reported as automated.
	const int trackCount = state.result.value(QStringLiteral("track_count")).toInt();
	QCOMPARE(state.result.value(QStringLiteral("count")).toInt(), trackCount);
	QCOMPARE(state.result.value(QStringLiteral("automated_parameter_count")).toInt(), 0);
	QCOMPARE(static_cast<int>(state.result.value(QStringLiteral("tracks")).toArray().size()), trackCount);

	const ControlResult missing = registry->invoke(QStringLiteral("automation.get_state"),
		QJsonObject{{QStringLiteral("track"), QStringLiteral("trk-9999")}});
	QVERIFY(!missing.ok);
	QCOMPARE(missing.errorKind, ControlErrorKind::NotFound);
}
void ControlAutomationScriptTest::automationTypedErrors() {
	ControlRegistry* registry = ControlRegistry::instance();

	struct Case
	{
		QString id;
		QJsonObject args;
		ControlErrorKind kind;
	};
	const QVector<Case> cases = {
		// no such track
		{qstr("automation.add_point"), {{qstr("track"), qstr("trk-9999")},
			{qstr("parameter"), qstr("inst/0")}, {qstr("ticks"), 0}, {qstr("value"), 1}},
			ControlErrorKind::NotFound},
		{qstr("automation.remove_point"), {{qstr("track"), qstr("trk-9999")},
			{qstr("parameter"), qstr("inst/0")}, {qstr("ticks"), 0}},
			ControlErrorKind::NotFound},
		{qstr("automation.clear"), {{qstr("track"), qstr("trk-9999")},
			{qstr("parameter"), qstr("inst/0")}}, ControlErrorKind::NotFound},
		// the args schema bites before the handler runs
		{qstr("automation.add_point"), {{qstr("track"), qstr("trk-9999")},
			{qstr("parameter"), qstr("inst/0")}, {qstr("ticks"), 0}},
			ControlErrorKind::InvalidArgs},
		{qstr("automation.add_point"), {{qstr("track"), qstr("trk-9999")},
			{qstr("parameter"), qstr("inst/0")}, {qstr("ticks"), -4}, {qstr("value"), 1}},
			ControlErrorKind::InvalidArgs},
		{qstr("automation.remove_point"), {{qstr("track"), qstr("trk-9999")},
			{qstr("parameter"), qstr("inst/0")}}, ControlErrorKind::InvalidArgs},
		// mode_set's closed enum is real, so a bogus mode never reaches the handler
		{qstr("automation.mode_set"), {{qstr("track"), qstr("trk-9999")},
			{qstr("parameter"), qstr("inst/0")}, {qstr("mode"), qstr("scribble")}},
			ControlErrorKind::InvalidArgs},
		// ... and a valid mode on a track that does not exist resolves the target first
		{qstr("automation.mode_set"), {{qstr("track"), qstr("trk-9999")},
			{qstr("parameter"), qstr("inst/0")}, {qstr("mode"), qstr("write")}},
			ControlErrorKind::NotFound},
		// record_mode_set's closed enum is real, so a bogus mode never reaches the handler
		{qstr("automation.record_mode_set"), {{qstr("track"), qstr("trk-9999")},
			{qstr("parameter"), qstr("inst/0")}, {qstr("mode"), qstr("scribble")}},
			ControlErrorKind::InvalidArgs},
		// record_mode_set on a non-existent track resolves the target first
		{qstr("automation.record_mode_set"), {{qstr("track"), qstr("trk-9999")},
			{qstr("parameter"), qstr("inst/0")}, {qstr("mode"), qstr("on")}},
			ControlErrorKind::NotFound},
		// an unknown command is not_found too
		{qstr("automation.no_such_verb"), {}, ControlErrorKind::NotFound},
	};
	for (const Case& testCase : cases)
	{
		const ControlResult result = registry->invoke(testCase.id, testCase.args);
		QVERIFY2(!result.ok, qPrintable(testCase.id + " unexpectedly succeeded"));
		QVERIFY2(result.errorKind == testCase.kind,
			qPrintable(QStringLiteral("%1: expected %2, got %3 (%4)")
				.arg(testCase.id, controlErrorKindName(testCase.kind),
					controlErrorKindName(result.errorKind), result.errorMessage)));
		QVERIFY(!result.errorMessage.isEmpty());
	}
}
void ControlAutomationScriptTest::transformRefusesAnInvertedRangeWithoutCreatingAutomation() {
	ControlRegistry* registry = ControlRegistry::instance();
	const QString trackId = m_devicedTrack;
	if (trackId.isEmpty() && !testHostCanLoadPluginModules())
	{
		QSKIP("the song exposes no automatable device parameter because this test host cannot load plugin modules");
	}
	QVERIFY(!trackId.isEmpty());
	const ControlResult state = registry->invoke(QStringLiteral("automation.get_state"),
		QJsonObject{{QStringLiteral("track"), trackId}});
	QVERIFY2(state.ok, qPrintable(state.errorMessage));
	QString parameterId;
	for (const QJsonValue& entry : state.result.value(QStringLiteral("tracks")).toArray())
	{
		const QJsonObject track = entry.toObject();
		if (track.value(QStringLiteral("id")).toString() == trackId)
		{
			const QJsonArray parameters = track.value(QStringLiteral("parameters")).toArray();
			if (!parameters.isEmpty()) { parameterId = parameters.first().toObject().value(QStringLiteral("id")).toString(); }
			break;
		}
	}
	if (parameterId.isEmpty() && !testHostCanLoadPluginModules())
	{
		QSKIP("the song exposes no automatable device parameter because this test host cannot load plugin modules");
	}
	QVERIFY(!parameterId.isEmpty());

	ControlTarget target;
	control::AutomationParameter parameter;
	ControlResult error;
	const QJsonObject args{{QStringLiteral("track"), trackId}, {QStringLiteral("parameter"), parameterId}};
	QVERIFY2(control::resolveAutomationParameter(args, &target, &parameter, &error), qPrintable(error.errorMessage));
	QVERIFY(control::existingAutomationClip(parameter.model) == nullptr);
	const int tracksBefore = static_cast<int>(Engine::getSong()->tracks().size());
	const ControlResult refused = registry->invoke(QStringLiteral("automation.transform"),
		{{QStringLiteral("track"), trackId}, {QStringLiteral("parameter"), parameterId},
			{QStringLiteral("start"), 100}, {QStringLiteral("end"), 20}});
	const int tracksAfter = static_cast<int>(Engine::getSong()->tracks().size());
	const bool automationWasCreated = control::existingAutomationClip(parameter.model) != nullptr;
	if (tracksAfter > tracksBefore)
	{
		const ControlResult undone = registry->invoke(QStringLiteral("control.undo"));
		QVERIFY2(undone.ok, qPrintable(undone.errorMessage));
	}
	QVERIFY(!refused.ok);
	QCOMPARE(refused.errorKind, ControlErrorKind::InvalidArgs);
	QCOMPARE(tracksAfter, tracksBefore);
	QVERIFY(!automationWasCreated);
}
void ControlAutomationScriptTest::automationModeSetSucceedsOnABareParameter() {
	ControlRegistry* registry = ControlRegistry::instance();
	// A fresh song has at least one track with parameters; find one.
	const ControlResult state = registry->invoke(QStringLiteral("automation.get_state"));
	QVERIFY2(state.ok, qPrintable(state.errorMessage));
	const QJsonArray tracks = state.result.value(QStringLiteral("tracks")).toArray();
	QVERIFY(!tracks.isEmpty());
	QString trackId;
	QString paramId;
	for (const QJsonValue& t : tracks)
	{
		const QJsonArray params = t.toObject().value(QStringLiteral("parameters")).toArray();
		if (!params.isEmpty())
		{
			trackId = t.toObject().value(QStringLiteral("id")).toString();
			paramId = params.first().toObject().value(QStringLiteral("id")).toString();
			break;
		}
	}
	if (trackId.isEmpty() && !testHostCanLoadPluginModules())
	{
		QSKIP("the song exposes no automatable device parameter: plugin modules link the zene "
			"executable, so on Windows their import descriptor names zene.exe and a test host "
			"cannot satisfy it; the product loads them inside zene.exe where that resolves by "
			"construction (CI msvc-x64: QLibrary::load -> ERROR_MOD_NOT_FOUND, 126)");
	}
	QVERIFY2(!trackId.isEmpty(), "no track with parameters in the fresh song");
	QVERIFY2(!paramId.isEmpty(), "no parameter found");

	for (const QString& mode : {qstr("off"), qstr("read"), qstr("touch"), qstr("latch"), qstr("write")})
	{
		const ControlResult result = registry->invoke(QStringLiteral("automation.mode_set"),
			QJsonObject{{QStringLiteral("track"), trackId},
				{QStringLiteral("parameter"), paramId},
				{QStringLiteral("mode"), mode}});
		QVERIFY2(result.ok, qPrintable(result.errorMessage));
		QCOMPARE(result.result.value(QStringLiteral("track")).toString(), trackId);
		QCOMPARE(result.result.value(QStringLiteral("parameter")).toString(), paramId);
		QCOMPARE(result.result.value(QStringLiteral("mode")).toString(), mode);
		// A mode change is OBSERVABLE, not only accepted: the read-back
		// reports exactly the string that was set, through the one
		// spelling function both directions use (ControlAutomationSupport).
		QCOMPARE(modeOf(registry, trackId, paramId), mode);
		QVERIFY2(!result.result.value(QStringLiteral("mode_before")).toString().isEmpty(),
			qPrintable(mode + ": the set reported no mode_before"));
		QCOMPARE(result.result.value(QStringLiteral("changed")).toBool(),
			result.result.value(QStringLiteral("mode_before")).toString() != mode);
	}

	// Leave the parameter as the test found it (read), so the mode this
	// test ended on is not a hidden precondition of the next one.
	const ControlResult back = registry->invoke(QStringLiteral("automation.mode_set"),
		QJsonObject{{QStringLiteral("track"), trackId},
			{QStringLiteral("parameter"), paramId},
			{QStringLiteral("mode"), qstr("read")}});
	QVERIFY2(back.ok, qPrintable(back.errorMessage));
}
void ControlAutomationScriptTest::automationRecordModeSetRequiresAClip() {
	ControlRegistry* registry = ControlRegistry::instance();
	const ControlResult state = registry->invoke(QStringLiteral("automation.get_state"));
	QVERIFY2(state.ok, qPrintable(state.errorMessage));
	const QJsonArray tracks = state.result.value(QStringLiteral("tracks")).toArray();
	QVERIFY(!tracks.isEmpty());
	QString trackId;
	QString paramId;
	for (const QJsonValue& t : tracks)
	{
		const QJsonArray params = t.toObject().value(QStringLiteral("parameters")).toArray();
		if (!params.isEmpty())
		{
			trackId = t.toObject().value(QStringLiteral("id")).toString();
			paramId = params.first().toObject().value(QStringLiteral("id")).toString();
			break;
		}
	}
	if (trackId.isEmpty() && !testHostCanLoadPluginModules())
	{
		QSKIP("the song exposes no automatable device parameter: plugin modules link the zene "
			"executable, so on Windows their import descriptor names zene.exe and a test host "
			"cannot satisfy it; the product loads them inside zene.exe where that resolves by "
			"construction (CI msvc-x64: QLibrary::load -> ERROR_MOD_NOT_FOUND, 126)");
	}
	QVERIFY2(!trackId.isEmpty(), "no track with parameters in the fresh song");
	QVERIFY2(!paramId.isEmpty(), "no parameter found");

	const ControlResult result = registry->invoke(QStringLiteral("automation.record_mode_set"),
		QJsonObject{{QStringLiteral("track"), trackId},
			{QStringLiteral("parameter"), paramId},
			{QStringLiteral("mode"), qstr("on")}});
	QVERIFY2(!result.ok, "record_mode_set succeeded without a clip");
	QCOMPARE(result.errorKind, ControlErrorKind::NotFound);
	QVERIFY2(result.errorMessage.contains(QStringLiteral("no automation clip")),
		qPrintable(result.errorMessage));

	// And its success path (R6.3: never reached before, so its reply was never
	// held to its schema): a point creates the clip, then the flag sets.
	const QJsonObject target{{QStringLiteral("track"), trackId}, {QStringLiteral("parameter"), paramId}};
	QJsonObject point = target;
	point.insert(QStringLiteral("ticks"), 0);
	point.insert(QStringLiteral("value"), 0.5);
	const ControlResult added = registry->invoke(QStringLiteral("automation.add_point"), point);
	QVERIFY2(added.ok, qPrintable(added.errorMessage));
	QJsonObject on = target;
	on.insert(QStringLiteral("mode"), qstr("on"));
	const ControlResult recording = registry->invoke(QStringLiteral("automation.record_mode_set"), on);
	QVERIFY2(recording.ok, qPrintable(recording.errorMessage));
	QCOMPARE(recording.result.value(QStringLiteral("mode")).toString(), qstr("on"));
	QVERIFY(registry->invoke(QStringLiteral("automation.clear"), target).ok);
}
void ControlAutomationScriptTest::movingBeforeTheClipOriginReportsATickError() {
	Engine::getSong()->clearProject();  // discard the empty clip left by the earlier record-mode test
	m_devicedTrack = revtest::addInstrumentTrack();
	if (m_devicedTrack.isEmpty() && !testHostCanLoadPluginModules())
	{
		QSKIP("the song exposes no automatable device parameter: plugin modules link the zene "
			"executable, so on Windows their import descriptor names zene.exe and a test host "
			"cannot satisfy it; the product loads them inside zene.exe where that resolves by "
			"construction (CI msvc-x64: QLibrary::load -> ERROR_MOD_NOT_FOUND, 126)");
	}
	QVERIFY2(!m_devicedTrack.isEmpty(), "no instrument track with parameters in the fresh song");
	ControlRegistry* registry = ControlRegistry::instance();
	const ControlResult state = registry->invoke(QStringLiteral("automation.get_state"),
		QJsonObject{{QStringLiteral("track"), m_devicedTrack}});
	QVERIFY2(state.ok, qPrintable(state.errorMessage));
	const QJsonArray tracks = state.result.value(QStringLiteral("tracks")).toArray();
	QVERIFY(!tracks.isEmpty());
	const QJsonArray parameters = tracks.first().toObject().value(QStringLiteral("parameters")).toArray();
	QVERIFY2(!parameters.isEmpty(), "no automatable parameter on the instrument track");
	const QString parameterId = parameters.first().toObject().value(QStringLiteral("id")).toString();

	ControlTarget target;
	control::AutomationParameter parameter;
	ControlResult error;
	const QJsonObject addressed{{QStringLiteral("track"), m_devicedTrack},
		{QStringLiteral("parameter"), parameterId}};
	QVERIFY2(control::resolveAutomationParameter(addressed, &target, &parameter, &error), qPrintable(error.errorMessage));
	const double validValue = (parameter.model->minValue<double>() + parameter.model->maxValue<double>()) / 2.0;

	auto* lane = dynamic_cast<AutomationTrack*>(Track::create(Track::Type::Automation, Engine::getSong()));
	auto* clip = dynamic_cast<AutomationClip*>(lane->createClip(TimePos(384)));
	clip->addObject(parameter.model);
	clip->changeLength(TimePos(4 * 192));
	clip->putValue(TimePos(16), 0.f, false, true);  // song tick 400
	const ControlResult moved = registry->invoke(QStringLiteral("automation.move_point"),
		QJsonObject{{QStringLiteral("track"), m_devicedTrack}, {QStringLiteral("parameter"), parameterId},
			{QStringLiteral("ticks"), 400}, {QStringLiteral("new_ticks"), 368}, {QStringLiteral("new_value"), validValue}});
	QVERIFY(!moved.ok);
	QCOMPARE(moved.errorMessage, QStringLiteral("destination tick 368 is before automation clip start at 384"));
	QCOMPARE(moved.errorKind, ControlErrorKind::Refused);
	QVERIFY(clip->getTimeMap().contains(TimePos(16)));
}
void ControlAutomationScriptTest::scriptListShowsTheShippedScripts() {
	ControlRegistry* registry = ControlRegistry::instance();
	const ControlResult result = registry->invoke(QStringLiteral("script.list"));
	QVERIFY2(result.ok, qPrintable(result.errorMessage));

	const QString dir = result.result.value(QStringLiteral("dir")).toString();
	QVERIFY2(!dir.isEmpty(), "script.list resolved no scripts directory");
	QVERIFY(QFileInfo(dir).isDir());

	QStringList names;
	for (const QJsonValue& value : result.result.value(QStringLiteral("scripts")).toArray())
	{
		const QJsonObject entry = value.toObject();
		names.append(entry.value(QStringLiteral("name")).toString());
		QVERIFY(!entry.value(QStringLiteral("sha256")).toString().isEmpty());
		QVERIFY(entry.value(QStringLiteral("bytes")).toInt() > 0);
		QVERIFY(QFileInfo(entry.value(QStringLiteral("path")).toString()).exists());
	}
	for (const QString& shipped : {qstr("hello.lua"), qstr("create-pattern.lua"),
			qstr("generative-bass.lua"), qstr("midi-router.lua")})
	{
		QVERIFY2(names.contains(shipped),
			qPrintable(QStringLiteral("%1 is not listed (saw %2)").arg(shipped, names.join(QLatin1Char(',')))));
	}
	QCOMPARE(result.result.value(QStringLiteral("count")).toInt(), static_cast<int>(names.size()));
}
void ControlAutomationScriptTest::anUnreadableScriptIsListedWithAnEmptyHash() {
#ifdef Q_OS_WIN
	QSKIP("POSIX file permissions: a Windows test host cannot make a readable file unreadable this way");
#else
	ControlRegistry* registry = ControlRegistry::instance();
	QTemporaryDir root;
	QVERIFY(root.isValid());
	QVERIFY(QDir(root.path()).mkpath(QStringLiteral("scripts")));
	const QString scripts = root.filePath(QStringLiteral("scripts"));

	const QByteArray body("-- @version 0.1\nprint('hello')\n");
	for (const QString& name : {QStringLiteral("readable.lua"), QStringLiteral("unreadable.lua")})
	{
		QFile file(scripts + QLatin1Char('/') + name);
		QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.errorString()));
		QCOMPARE(file.write(body), body.size());
		file.close();
	}
	QVERIFY(QFile::setPermissions(scripts + QStringLiteral("/unreadable.lua"), QFileDevice::Permissions()));

	// script.list resolves 'data:/scripts', so pointing the 'data' search path
	// at this temporary tree is what puts these two files on the listing. The
	// path is put back before anything else runs.
	const QStringList saved = QDir::searchPaths(QStringLiteral("data"));
	QDir::setSearchPaths(QStringLiteral("data"), QStringList{root.path()} + saved);
	const ControlResult result = registry->invoke(QStringLiteral("script.list"));
	QDir::setSearchPaths(QStringLiteral("data"), saved);

	QVERIFY2(result.ok, qPrintable(result.errorMessage));
	QCOMPARE(result.result.value(QStringLiteral("dir")).toString(), scripts);
	QJsonObject unreadable;
	for (const QJsonValue& value : result.result.value(QStringLiteral("scripts")).toArray())
	{
		const QJsonObject entry = value.toObject();
		if (entry.value(QStringLiteral("name")).toString() == qstr("unreadable.lua"))
		{
			unreadable = entry;
		}
		else
		{
			// The readable one beside it keeps a real hash, so the empty one
			// below is about the unreadable file and not about this listing.
			QVERIFY(!entry.value(QStringLiteral("sha256")).toString().isEmpty());
		}
	}
	QVERIFY2(!unreadable.isEmpty(), "the unreadable script was not listed at all");
	QCOMPARE(unreadable.value(QStringLiteral("bytes")).toInt(), static_cast<int>(body.size()));
	QVERIFY2(unreadable.value(QStringLiteral("sha256")).toString().isEmpty(),
		qPrintable(QStringLiteral("an unreadable script reported the hash '%1' (the hash of empty bytes)")
			.arg(unreadable.value(QStringLiteral("sha256")).toString())));
#endif
}
