/*
 * SampleEditorDocsTest.cpp - the Sample Editor's documented range capabilities, against the implementation
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
/*! Q050-288: docs/KNOWN-LIMITATIONS.md and the manual must name the Sample Editor's range
 *  operations as the tree implements them - the eight buttons, their sample.edit_range values,
 *  the whole-source Process commands kept distinct, and the real bounds (no limiter, linear
 *  fades, no noise reduction among the range tools). The behaviour half drives every button
 *  through the registered command and proves one undo step each, so the prose cannot describe a
 *  control that is not wired. */

#include <QtTest>

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QPushButton>

#include <cmath>
#include <vector>

#include "ControlRegistry.h"
#include "Engine.h"
#include "MenuTestSupport.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleEditorDialog.h"
#include "SampleTrack.h"
#include "ShellCommandMenus.h"
#include "Song.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

constexpr int Rate = 44100;
constexpr int Frames = 4000;

//! The range operations: the button's text, its sample.edit_range 'op', and the gain it asks for.
struct Action
{
	const char* label;
	const char* op;
	double gain;
};

const Action kActions[] = {
	{"-3 dB", "gain", -3.0}, {"+3 dB", "gain", 3.0}, {"Silence", "silence", 0.0},
	{"Delete", "delete", 0.0}, {"Reverse", "reverse", 0.0}, {"Fade In", "fade_in", 0.0},
	{"Fade Out", "fade_out", 0.0}, {"Crop", "crop", 0.0}};

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

int undoDepth()
{
	return run(QStringLiteral("control.undo_depth")).result.value(QStringLiteral("depth")).toInt();
}

//! A doc from the source tree this binary was built out of; empty when unreachable (callers SKIP).
QString readDoc(const QString& path)
{
	const QStringList candidates = {QCoreApplication::applicationDirPath() + QStringLiteral("/../../") + path,
		QCoreApplication::applicationDirPath() + QStringLiteral("/../../../") + path};
	for (const QString& candidate : candidates)
	{
		QFile file(candidate);
		if (file.open(QIODevice::ReadOnly | QIODevice::Text)) { return QString::fromUtf8(file.readAll()); }
	}
	return QString();
}

QPushButton* button(QWidget* dialog, const QString& text)
{
	for (auto* candidate : dialog->findChildren<QPushButton*>())
	{
		if (candidate->text() == text) { return candidate; }
	}
	return nullptr;
}

std::vector<SampleFrame> framesOf(SampleClip* clip)
{
	const Sample& source = clip->sample();
	return std::vector<SampleFrame>(source.data(), source.data() + source.sampleSize());
}

//! Bit-exact frame equality (SampleFrame has no operator== of its own).
bool sameFrames(const std::vector<SampleFrame>& a, const std::vector<SampleFrame>& b)
{
	if (a.size() != b.size()) { return false; }
	for (std::size_t i = 0; i < a.size(); ++i)
	{
		if (a[i][0] != b[i][0] || a[i][1] != b[i][1]) { return false; }
	}
	return true;
}

} // namespace

class SampleEditorDocsTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
		m_track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, Engine::getSong()));
		QVERIFY(m_track != nullptr);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
		m_track = nullptr;
	}

	void init()
	{
		m_clip = dynamic_cast<SampleClip*>(m_track->createClip(TimePos(0)));
		setRamp();
		m_dialog = new SampleEditorDialog(m_clip);
		m_dialog->setAttribute(Qt::WA_DeleteOnClose, false);
		m_dialog->show();
		QVERIFY(QTest::qWaitForWindowExposed(m_dialog));
	}

	void cleanup()
	{
		delete m_dialog;
		m_dialog = nullptr;
		delete m_clip;
		m_clip = nullptr;
	}

	//! The docs name every button and every registered 'op', and state the bounds the code has.
	void theDocsNameTheRangeOperationsAndTheirBounds()
	{
		const QString limitations = readDoc(QStringLiteral("docs/KNOWN-LIMITATIONS.md"));
		const QString manual = readDoc(QStringLiteral("data/manual/index.html"));
		if (limitations.isEmpty() || manual.isEmpty()) { QSKIP("the docs are not reachable from this build directory"); }

		const ControlCommand* command = ControlRegistry::instance()->command(QStringLiteral("sample.edit_range"));
		QVERIFY2(command != nullptr, "sample.edit_range is not registered");
		QStringList ops;
		for (const QJsonValue& value : command->argsSchema.value(QStringLiteral("properties")).toObject()
			.value(QStringLiteral("op")).toObject().value(QStringLiteral("enum")).toArray())
		{
			ops.append(value.toString());
		}
		for (const Action& action : kActions)
		{
			QVERIFY2(button(m_dialog, QString::fromLatin1(action.label)) != nullptr, action.label);
			QVERIFY2(ops.contains(QString::fromLatin1(action.op)), action.op);
			QVERIFY2(limitations.contains(QString::fromLatin1(action.label)), action.label);
			QVERIFY2(limitations.contains(QStringLiteral("`%1`").arg(QLatin1String(action.op))), action.op);
			QVERIFY2(manual.contains(QString::fromLatin1(action.label)), action.label);
		}
		// The seven range values plus Q050-106's whole-source `mono` are exhaustive; there is no
		// range denoise operation.
		QCOMPARE(ops.size(), 8);
		QVERIFY(ops.contains(QStringLiteral("mono")));
		int rangeButtons = 0;
		for (auto* candidate : m_dialog->findChildren<QPushButton*>())
		{
			if (candidate->property("controlCommand").toString() == QStringLiteral("sample.edit_range")) { ++rangeButtons; }
		}
		QCOMPARE(rangeButtons, 9); // The eight range buttons and To Mono; no extra edit button in the GUI.
		QVERIFY2(button(m_dialog, QStringLiteral("To Mono")) != nullptr, "To Mono");
		QVERIFY2(limitations.contains(QStringLiteral("To Mono")) && limitations.contains(QStringLiteral("`op: \"mono\"`")),
			"the whole-source To Mono is not documented");
		QVERIFY2(limitations.contains(QStringLiteral("sample.split_stereo")), "Split Stereo... is not documented");
		// The section is the editor's, and the real bounds are spelled out rather than implied.
		QVERIFY(limitations.contains(QStringLiteral("Sample Editor")));
		QVERIFY2(limitations.contains(QStringLiteral("no limiter")), "the no-limiter bound is not stated");
		QVERIFY2(limitations.contains(QStringLiteral("linear")), "the linear-fade bound is not stated");
		QVERIFY2(limitations.contains(QStringLiteral("no noise reduction")), "the absent noise reduction is not stated");
		QVERIFY2(!limitations.contains(QStringLiteral("socket-only, no interface")), "the stale socket-only claim is back");
		// The whole-source bakers stay distinct from the range operations, in both directions.
		for (const QString& whole : {QStringLiteral("sample.normalize"), QStringLiteral("sample.amplify"),
				QStringLiteral("sample.reverse"), QStringLiteral("sample.fade")})
		{
			QVERIFY2(limitations.contains(whole), qPrintable(whole));
		}
		QVERIFY2(limitations.contains(QStringLiteral("Crop the Sample")), "the crop distinction is missing");
		QVERIFY2(limitations.contains(QStringLiteral("sample.crop")), "sample.crop is not named");
	}

	/*! The AGENT-facing descriptions name the GUI surface too. The whole-source commands used to
	 *  tell an agent "Socket-only in this release" and "no interface reaches them", which is the
	 *  same stale claim the limitations page carried; sample.fade additionally claimed the tree
	 *  had no range/selection model. Every registered description is read from the live registry,
	 *  so reverting those strings fails here. */
	void everyWholeSourceDescriptionNamesItsRealGuiEntryPoint()
	{
		const struct { const char* id; const char* entry; const char* also; } expected[] = {
			{"sample.generate", "Insert Test Signal...", "sample.edit_range"},
			{"sample.amplify", "Process menu", "sample.edit_range"},
			{"sample.normalize", "Process menu", "Normalize..."},
			{"sample.reverse", "Process menu", "Reverse"},
			{"sample.fade", "Process menu", "sample.edit_range"},
		};
		for (const auto& want : expected)
		{
			const ControlCommand* command = ControlRegistry::instance()->command(QString::fromLatin1(want.id));
			QVERIFY2(command != nullptr, want.id);
			const QString description = command->description;
			QVERIFY2(!description.contains(QStringLiteral("Socket-only")),
				qPrintable(QStringLiteral("%1 still says Socket-only").arg(QLatin1String(want.id))));
			QVERIFY2(description.contains(QString::fromLatin1(want.entry)),
				qPrintable(QStringLiteral("%1 does not name its GUI entry point '%2'").arg(QLatin1String(want.id), QLatin1String(want.entry))));
			QVERIFY2(description.contains(QString::fromLatin1(want.also)),
				qPrintable(QStringLiteral("%1 does not point at '%2'").arg(QLatin1String(want.id), QLatin1String(want.also))));
		}
		// sample.fade is a whole-source bake; the command must not claim the product has no range model.
		const ControlCommand* fade = ControlRegistry::instance()->command(QStringLiteral("sample.fade"));
		QVERIFY2(fade != nullptr, "sample.fade is not registered");
		QVERIFY2(!fade->description.contains(QStringLiteral("no range/selection model")),
			"sample.fade still says the release has no range/selection model");
		QVERIFY2(fade->description.contains(QStringLiteral("fade_in")) && fade->description.contains(QStringLiteral("fade_out")),
			"sample.fade does not point at the range fade operations");
	}

	//! Every button runs sample.edit_range through the registry, and one undo restores the frames.
	void everyButtonIsOneUndoStepThroughTheRegisteredCommand()
	{
		const int from = 1000;
		const int to = 2500;
		for (const Action& action : kActions)
		{
			QPushButton* control = button(m_dialog, QString::fromLatin1(action.label));
			QVERIFY2(control != nullptr, action.label);
			QCOMPARE(control->property("controlCommand").toString(), QStringLiteral("sample.edit_range"));
			const std::vector<SampleFrame> before = framesOf(m_clip);
			const int depth = undoDepth();
			m_dialog->setRange(from, to);
			control->click();
			QCOMPARE(undoDepth(), depth + 1);
			verifyApplied(action, before, from, to);
			QVERIFY2(run(QStringLiteral("control.undo")).ok, action.label);
			QVERIFY2(sameFrames(framesOf(m_clip), before), "one undo did not restore the frames that were there");
			QCOMPARE(undoDepth(), depth);
		}
	}

	//! The clip menu opens the editor, and the Process menu's whole-source bakes are their own ids.
	void theClipMenuOpensTheEditorAndKeepsTheBakesSeparate()
	{
		QMenu menu;
		ShellCommandMenus::populateClip(&menu, m_clip);
		QAction* editor = lmms::test::menuAction(&menu, {QStringLiteral("Sample Editor...")});
		QVERIFY(editor != nullptr);
		QCOMPARE(editor->property("controlCommand").toString(), QStringLiteral("sample.edit_range"));
		const struct { const char* path; const char* id; } bakes[] = {
			{"Normalize...", "sample.normalize"}, {"Amplify...", "sample.amplify"},
			{"Reverse", "sample.reverse"}};
		for (const auto& bake : bakes)
		{
			QAction* entry = lmms::test::menuAction(&menu, {QStringLiteral("Process"), QString::fromLatin1(bake.path)});
			QVERIFY2(entry != nullptr, bake.path);
			QCOMPARE(entry->property("controlCommand").toString(), QString::fromLatin1(bake.id));
		}
		QAction* crop = lmms::test::menuAction(&menu, {QStringLiteral("Crop the Sample")});
		QVERIFY(crop != nullptr);
		QCOMPARE(crop->property("controlCommand").toString(), QStringLiteral("sample.crop"));
	}

private:
	void setRamp()
	{
		std::vector<SampleFrame> frames;
		frames.reserve(Frames);
		for (int i = 0; i < Frames; ++i)
		{
			const float v = 0.001f * static_cast<float>(i + 1);
			frames.emplace_back(v, -v);
		}
		m_clip->setSampleBuffer(std::make_shared<SampleBuffer>(frames.data(), frames.size(), Rate));
	}

	void verifyApplied(const Action& action, const std::vector<SampleFrame>& before, int from, int to)
	{
		const std::vector<SampleFrame> after = framesOf(m_clip);
		const QString op = QLatin1String(action.op);
		if (op == QLatin1String("delete") || op == QLatin1String("crop"))
		{
			QCOMPARE(static_cast<int>(after.size()), op == QLatin1String("delete") ? Frames - (to - from) : to - from);
			return;
		}
		QCOMPARE(static_cast<int>(after.size()), Frames);
		if (op == QLatin1String("silence")) { QCOMPARE(after[from][0], 0.f); return; }
		if (op == QLatin1String("reverse")) { QCOMPARE(after[from][0], before[to - 1][0]); return; }
		if (op == QLatin1String("fade_in") || op == QLatin1String("fade_out"))
		{
			if (op == QLatin1String("fade_in"))
			{
				QCOMPARE(after[from][0], 0.f);
				QCOMPARE(after[to - 1][0], before[to - 1][0]);
			}
			else
			{
				QCOMPARE(after[from][0], before[from][0]);
				QCOMPARE(after[to - 1][0], 0.f);
			}
			const int middle = from + (to - from - 1) / 2;
			const float midpointRatio = after[middle][0] / before[middle][0];
			QVERIFY2(std::abs(midpointRatio - 0.5f) < 0.001f, "the fade is not linear across its range");
			return;
		}
		const float expected = before[from][0] * static_cast<float>(std::pow(10.0, action.gain / 20.0));
		QVERIFY2(std::abs(after[from][0] - expected) < 1e-6f, action.label);
		QCOMPARE(after[0][0], before[0][0]);  // outside the range the frames are untouched
	}

	SampleTrack* m_track = nullptr;
	SampleClip* m_clip = nullptr;
	SampleEditorDialog* m_dialog = nullptr;
};

QTEST_MAIN(SampleEditorDocsTest)
#include "SampleEditorDocsTest.moc"
