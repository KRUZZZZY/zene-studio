/*
 * SlicerTSliceControlsTest.cpp - per-slice SlicerT controls (0.5.0 L10, Q050-263)
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

/*! THE PROOF for Q050-263. A real SlicerT (the build's module) holds a three-slice sample whose
 *  slices are identical (a 200 Hz plus an 8 kHz tone, right channel half the left), so the rendered
 *  slices differ only by what the per-slice controls do to them:
 *   - the view's per-slice panel is driven with real key events; its edits go through the registered
 *     plugin.slice_set, and a MIDI slice sequence (keys base+1 .. base+3) is rendered: slice 0 is
 *     6 dB down, slice 1 is panned hard right, slice 2 is low-passed, and none of them leaks into
 *     another slice or into the base key's whole-sample note;
 *   - the random amount varies repeated triggers of a slice within its bound and is exactly neutral
 *     at 0;
 *   - plugin.slice_get reads every value back, a project save/reopen keeps them (and the render),
 *     one control.undo takes back one edit, and state saved without the attributes loads neutral;
 *   - bad calls are refused without writing anything. */

#include <QtTest>

#include <QDomDocument>
#include <QDomElement>
#include <QDoubleSpinBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QLineEdit>
#include <QSpinBox>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <span>
#include <vector>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "Note.h"
#include "NotePlayHandle.h"
#include "PluginView.h"
#include "SampleBuffer.h"
#include "SampleFrame.h"
#include "Song.h"
#include "Track.h"

using namespace lmms;

namespace
{

constexpr int SliceFrames = 4096;
constexpr int SliceCount = 3;

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! The slices are the same 200 Hz + 8 kHz signal; the right channel is half the left.
std::vector<SampleFrame> sourceFrames(int rate)
{
	std::vector<SampleFrame> frames(static_cast<std::size_t>(SliceFrames * SliceCount));
	for (std::size_t n = 0; n < frames.size(); ++n)
	{
		const double t = static_cast<double>(n % SliceFrames) / rate;
		const auto v = static_cast<float>(0.4 * std::sin(2.0 * std::numbers::pi * 200.0 * t)
			+ 0.4 * std::sin(2.0 * std::numbers::pi * 8000.0 * t));
		frames[n] = SampleFrame(v, 0.5f * v);
	}
	return frames;
}

QDomElement slicerSettings(QDomDocument& doc, int rate)
{
	QDomElement settings = doc.createElement(QStringLiteral("slicert"));
	const SampleBuffer buffer(sourceFrames(rate), rate);
	settings.setAttribute(QStringLiteral("sampledata"), buffer.toBase64());
	settings.setAttribute(QStringLiteral("totalSlices"), SliceCount + 1);
	for (int i = 0; i <= SliceCount; ++i)
	{
		settings.setAttribute(QStringLiteral("slice_%1").arg(i), static_cast<double>(i) / SliceCount);
	}
	return settings;
}

double rms(const std::vector<float>& x)
{
	double sum = 0;
	for (float v : x) { sum += static_cast<double>(v) * v; }
	return x.empty() ? 0.0 : std::sqrt(sum / static_cast<double>(x.size()));
}

std::vector<float> channel(const std::vector<SampleFrame>& block, int ch)
{
	std::vector<float> out;
	out.reserve(block.size());
	for (const SampleFrame& frame : block) { out.push_back(ch == 0 ? frame.left() : frame.right()); }
	return out;
}

//! High-frequency content: the rms of the first difference (the 8 kHz tone dominates it).
double highBand(const std::vector<float>& x)
{
	std::vector<float> diff;
	for (std::size_t i = 1; i < x.size(); ++i) { diff.push_back(x[i] - x[i - 1]); }
	return rms(diff);
}

//! Low-frequency content: the rms of a 55-frame moving average (the 8 kHz tone averages away).
double lowBand(const std::vector<float>& x)
{
	constexpr std::size_t N = 55;
	std::vector<float> averaged;
	for (std::size_t i = N; i < x.size(); ++i)
	{
		double sum = 0;
		for (std::size_t j = 0; j < N; ++j) { sum += x[i - j]; }
		averaged.push_back(static_cast<float>(sum / N));
	}
	return rms(averaged);
}

QDomElement withAttributePrefix(const QDomElement& element, const QString& prefix)
{
	const QDomNamedNodeMap attributes = element.attributes();
	for (int i = 0; i < attributes.size(); ++i)
	{
		if (attributes.item(i).nodeName().startsWith(prefix)) { return element; }
	}
	for (QDomElement child = element.firstChildElement(); !child.isNull(); child = child.nextSiblingElement())
	{
		const QDomElement found = withAttributePrefix(child, prefix);
		if (!found.isNull()) { return found; }
	}
	return {};
}

} // namespace

class SlicerTSliceControlsTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();
		NotePlayHandleManager::init();
		ControlRegistry::setReady(true);
		QVERIFY(m_dir.isValid());
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! Neutral slices render the sample untouched, and the settings of an old project (no per-slice
	//! attributes) load neutral again after values were set.
	void neutralSlicesRenderTheSampleAndOldStateLoadsNeutral()
	{
		InstrumentTrack* track = slicerTrack();
		QVERIFY(track != nullptr);
		const std::vector<SampleFrame> source = sourceFrames(rate());
		for (int slice = 0; slice < SliceCount; ++slice)
		{
			// The sample's own level comes through (SlicerT resamples, so frames are not bit-identical to the source).
			const std::vector<SampleFrame> block = render(track, key(track, slice));
			const std::vector<SampleFrame> head(source.begin() + slice * SliceFrames,
				source.begin() + slice * SliceFrames + static_cast<std::ptrdiff_t>(block.size()));
			for (int ch = 0; ch < 2; ++ch)
			{
				const double ratio = rms(channel(block, ch)) / rms(channel(head, ch));
				QVERIFY2(std::fabs(ratio - 1.0) < 0.02,
					qPrintable(QStringLiteral("neutral slice %1 channel %2 is %3 of the sample's level").arg(slice).arg(ch).arg(ratio)));
			}
		}
		// Untouched state writes no per-slice attribute: the save format is what it was.
		QVERIFY(withAttributePrefix(trackXml(track), QStringLiteral("slice")).isNull() == false);  // slice_<n> points
		QVERIFY(withAttributePrefix(trackXml(track), QStringLiteral("sliceGain_")).isNull());

		QVERIFY(setSlice(track, 1, {{QStringLiteral("gain"), -9.0}, {QStringLiteral("pan"), 0.5}}).ok);
		QDomDocument doc;
		track->instrument()->restoreState(slicerSettings(doc, rate()));  // an old project: no attributes
		for (int slice = 0; slice < SliceCount; ++slice)
		{
			const QJsonObject entry = readSlice(track, slice);
			QCOMPARE(entry.value(QStringLiteral("gain")).toDouble(), 0.0);
			QCOMPARE(entry.value(QStringLiteral("pan")).toDouble(), 0.0);
			QCOMPARE(entry.value(QStringLiteral("filter")).toDouble(), 0.0);
			QCOMPARE(entry.value(QStringLiteral("random")).toDouble(), 0.0);
		}
	}

	//! The panel's edits (real key events) reach each slice; the rendered slice sequence shows the
	//! gain, pan and filter of its own slice and nothing leaks into the neighbours or the base key.
	void panelEditsShapeTheRenderedSliceSequence()
	{
		InstrumentTrack* track = slicerTrack();
		QVERIFY(track != nullptr);
		QWidget host;
		gui::PluginView* view = track->instrument()->createView(&host);
		QVERIFY(view != nullptr);
		host.show();
		auto* select = view->findChild<QSpinBox*>(QStringLiteral("slicerTSliceSelect"));
		auto* gain = view->findChild<QDoubleSpinBox*>(QStringLiteral("slicerTSliceGain"));
		auto* pan = view->findChild<QDoubleSpinBox*>(QStringLiteral("slicerTSlicePan"));
		auto* filter = view->findChild<QDoubleSpinBox*>(QStringLiteral("slicerTSliceFilter"));
		QVERIFY2(select != nullptr && gain != nullptr && pan != nullptr && filter != nullptr,
			"SlicerTView has no per-slice panel");
		QCOMPARE(select->maximum(), SliceCount - 1);

		std::vector<std::vector<SampleFrame>> before;
		for (int slice = 0; slice < SliceCount; ++slice) { before.push_back(render(track, key(track, slice))); }
		const std::vector<SampleFrame> wholeBefore = render(track, track->baseNote());

		// Slice 0: -6 dB; slice 1: pan hard right; slice 2: low-pass at its lowest cutoff.
		QCOMPARE(select->value(), 0);
		type(gain, "-6");
		QTest::keyClick(select, Qt::Key_Up);
		QCOMPARE(select->value(), 1);
		QCOMPARE(gain->value(), 0.0);  // the panel shows slice 1's own value
		type(pan, "1");
		QTest::keyClick(select, Qt::Key_Up);
		QCOMPARE(select->value(), 2);
		type(filter, "-1");

		// What the panel did is what the agent reads back: it went through plugin.slice_set.
		QCOMPARE(readSlice(track, 0).value(QStringLiteral("gain")).toDouble(), -6.0);
		QCOMPARE(readSlice(track, 1).value(QStringLiteral("pan")).toDouble(), 1.0);
		QCOMPARE(readSlice(track, 2).value(QStringLiteral("filter")).toDouble(), -1.0);
		QCOMPARE(readSlice(track, 0).value(QStringLiteral("pan")).toDouble(), 0.0);

		// The MIDI slice sequence: keys base+1 .. base+3 one after the other.
		std::vector<std::vector<SampleFrame>> after;
		for (int slice = 0; slice < SliceCount; ++slice) { after.push_back(render(track, key(track, slice))); }

		const double gainRatio = rms(channel(after[0], 0)) / rms(channel(before[0], 0));
		QVERIFY2(std::fabs(gainRatio - std::pow(10.0, -6.0 / 20.0)) < 1e-3, qPrintable(QString::number(gainRatio)));
		QVERIFY(std::fabs(rms(channel(after[0], 1)) / rms(channel(before[0], 1)) - gainRatio) < 1e-3);

		QVERIFY2(rms(channel(after[1], 0)) < 1e-6, "slice 1 panned hard right still sounds on the left");
		QVERIFY(std::fabs(rms(channel(after[1], 1)) - rms(channel(before[1], 1))) < 1e-4);

		const double highRatio = highBand(channel(after[2], 0)) / highBand(channel(before[2], 0));
		QVERIFY2(highRatio < 0.05, qPrintable(QStringLiteral("low-pass left %1 of the 8 kHz").arg(highRatio)));
		QVERIFY(rms(channel(after[2], 0)) > 0.001);  // the 200 Hz part still passes: it is a filter, not a mute

		// The base key plays the whole sample (slice 0 and its controls are not in play there).
		const std::vector<SampleFrame> wholeAfter = render(track, track->baseNote());
		for (std::size_t i = 0; i < wholeAfter.size(); ++i)
		{
			QVERIFY(std::fabs(wholeAfter[i].left() - wholeBefore[i].left()) < 1e-6f);
			QVERIFY(std::fabs(wholeAfter[i].right() - wholeBefore[i].right()) < 1e-6f);
		}

		// One panel edit is one undo step: undoing the filter leaves the gain and pan. Undo rebuilds the
		// instrument (and so its window), so the old view goes first and a new one shows what came back.
		delete view;
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(readSlice(track, 2).value(QStringLiteral("filter")).toDouble(), 0.0);
		QCOMPARE(readSlice(track, 1).value(QStringLiteral("pan")).toDouble(), 1.0);
		QCOMPARE(readSlice(track, 0).value(QStringLiteral("gain")).toDouble(), -6.0);
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QCOMPARE(readSlice(track, 2).value(QStringLiteral("filter")).toDouble(), -1.0);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(readSlice(track, 2).value(QStringLiteral("filter")).toDouble(), 0.0);
		gui::PluginView* reopened = track->instrument()->createView(&host);
		QVERIFY(reopened != nullptr);
		auto* reopenedSelect = reopened->findChild<QSpinBox*>(QStringLiteral("slicerTSliceSelect"));
		auto* reopenedGain = reopened->findChild<QDoubleSpinBox*>(QStringLiteral("slicerTSliceGain"));
		QVERIFY(reopenedSelect != nullptr && reopenedGain != nullptr);
		QCOMPARE(reopenedGain->value(), -6.0);  // slice 0's gain, as the window opens on slice 0
		QTest::keyClick(reopenedSelect, Qt::Key_Up);
		QTest::keyClick(reopenedSelect, Qt::Key_Up);
		QCOMPARE(reopened->findChild<QDoubleSpinBox*>(QStringLiteral("slicerTSliceFilter"))->value(), 0.0);
		delete reopened;
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QCOMPARE(readSlice(track, 2).value(QStringLiteral("filter")).toDouble(), -1.0);

		// A high-pass the other way round: the low tone goes, the high one stays.
		QVERIFY(setSlice(track, 0, {{QStringLiteral("gain"), 0.0}, {QStringLiteral("filter"), 1.0}}).ok);
		const std::vector<SampleFrame> high = render(track, key(track, 0));
		QVERIFY(lowBand(channel(high, 0)) / lowBand(channel(before[0], 0)) < 0.05);
		QVERIFY(highBand(channel(high, 0)) / highBand(channel(before[0], 0)) > 0.5);
	}

	//! The random amount varies repeated triggers of one slice inside its bound and is exactly neutral at 0.
	void randomAmountVariesTriggersWithinItsBound()
	{
		InstrumentTrack* track = slicerTrack();
		QVERIFY(track != nullptr);
		const std::vector<SampleFrame> neutral = render(track, key(track, 0));

		QVERIFY(setSlice(track, 0, {{QStringLiteral("random"), 0.0}}).ok);
		QVERIFY(sameFrames(render(track, key(track, 0)), neutral));
		QVERIFY(sameFrames(render(track, key(track, 0)), neutral));

		QVERIFY(setSlice(track, 0, {{QStringLiteral("random"), 1.0}}).ok);
		std::vector<double> leftRatios;
		for (int trigger = 0; trigger < 8; ++trigger)
		{
			const std::vector<SampleFrame> block = render(track, key(track, 0));
			const double l = rms(channel(block, 0)) / rms(channel(neutral, 0));
			const double r = rms(channel(block, 1)) / rms(channel(neutral, 1));
			// gain within +-6 dB; pan within +-0.5 only ever attenuates one side by at most 0.5
			QVERIFY2(l <= std::pow(10.0, 6.0 / 20.0) + 1e-3 && l >= 0.5 * std::pow(10.0, -6.0 / 20.0) - 1e-3,
				qPrintable(QStringLiteral("left ratio %1").arg(l)));
			QVERIFY2(r <= std::pow(10.0, 6.0 / 20.0) + 1e-3 && r >= 0.5 * std::pow(10.0, -6.0 / 20.0) - 1e-3,
				qPrintable(QStringLiteral("right ratio %1").arg(r)));
			leftRatios.push_back(l);
		}
		const auto [lo, hi] = std::minmax_element(leftRatios.begin(), leftRatios.end());
		QVERIFY2(*hi - *lo > 0.05, "eight triggers at random 1 did not differ");

		// A smaller amount is a smaller bound.
		QVERIFY(setSlice(track, 0, {{QStringLiteral("random"), 0.1}}).ok);
		for (int trigger = 0; trigger < 8; ++trigger)
		{
			const double l = rms(channel(render(track, key(track, 0)), 0)) / rms(channel(neutral, 0));
			QVERIFY2(l <= std::pow(10.0, 0.6 / 20.0) + 1e-3 && l >= 0.95 * std::pow(10.0, -0.6 / 20.0) - 1e-3,
				qPrintable(QStringLiteral("left ratio %1 at random 0.1").arg(l)));
		}
	}

	//! Set through the agent surface, saved, reopened: every slice reads back its own four values and
	//! the render is unchanged.
	void valuesSurviveSaveAndReopenAndReadBack()
	{
		InstrumentTrack* track = slicerTrack();
		QVERIFY(track != nullptr);
		QVERIFY(setSlice(track, 0, {{QStringLiteral("gain"), -3.5}, {QStringLiteral("pan"), -0.25},
			{QStringLiteral("filter"), 0.4}, {QStringLiteral("random"), 0.0}}).ok);
		QVERIFY(setSlice(track, 1, {{QStringLiteral("gain"), 4.0}, {QStringLiteral("pan"), 0.75},
			{QStringLiteral("filter"), -0.5}, {QStringLiteral("random"), 0.0}}).ok);
		QVERIFY(setSlice(track, 2, {{QStringLiteral("gain"), -12.0}, {QStringLiteral("random"), 0.0}}).ok);

		const ControlResult all = run(QStringLiteral("plugin.slice_get"), {{QStringLiteral("target"), control::trackIdOf(track)}});
		QVERIFY2(all.ok, qPrintable(all.errorMessage));
		QCOMPARE(all.result.value(QStringLiteral("slice_count")).toInt(), SliceCount);
		QCOMPARE(all.result.value(QStringLiteral("slices")).toArray().size(), SliceCount);
		QCOMPARE(all.result.value(QStringLiteral("controls")).toArray().size(), 4);
		const QJsonObject second = all.result.value(QStringLiteral("slices")).toArray().at(1).toObject();
		QCOMPARE(second.value(QStringLiteral("key")).toInt(), track->baseNote() + 2);

		std::vector<std::vector<SampleFrame>> rendered;
		for (int slice = 0; slice < SliceCount; ++slice) { rendered.push_back(render(track, key(track, slice))); }

		QVERIFY(withAttributePrefix(trackXml(track), QStringLiteral("sliceGain_")).isNull() == false);

		const QString id = control::trackIdOf(track);
		const QString project = m_dir.filePath(QStringLiteral("slicert-slices.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(project));
		Engine::getSong()->loadProject(project);
		ControlResult error;
		auto* reloaded = dynamic_cast<InstrumentTrack*>(control::resolveTrack(id, &error));
		QVERIFY2(reloaded != nullptr, qPrintable(error.errorMessage));

		const double expected[SliceCount][4] = {{-3.5, -0.25, 0.4, 0.0}, {4.0, 0.75, -0.5, 0.0}, {-12.0, 0.0, 0.0, 0.0}};
		const char* names[4] = {"gain", "pan", "filter", "random"};
		for (int slice = 0; slice < SliceCount; ++slice)
		{
			const QJsonObject entry = readSlice(reloaded, slice);
			for (int c = 0; c < 4; ++c)
			{
				QVERIFY2(std::fabs(entry.value(QString::fromLatin1(names[c])).toDouble() - expected[slice][c]) < 1e-5,
					qPrintable(QStringLiteral("slice %1 %2 = %3 after reopening").arg(slice).arg(QString::fromLatin1(names[c]))
						.arg(entry.value(QString::fromLatin1(names[c])).toDouble())));
			}
			const std::vector<SampleFrame> again = render(reloaded, key(reloaded, slice));
			QVERIFY2(sameFrames(again, rendered[static_cast<std::size_t>(slice)]),
				qPrintable(QStringLiteral("slice %1 renders differently after reopening").arg(slice)));
		}
	}

	//! Refusals write nothing; the commands carry honest contract classes.
	void badCallsAreRefusedAndTheContractIsStated()
	{
		InstrumentTrack* track = slicerTrack();
		QVERIFY(track != nullptr);
		const QString id = control::trackIdOf(track);
		QVERIFY(setSlice(track, 0, {{QStringLiteral("gain"), -2.0}}).ok);
		const int depth = run(QStringLiteral("control.undo_depth")).result.value(QStringLiteral("undo")).toInt(-1);

		// Out of range: the whole call is refused, including the part that was in range.
		const ControlResult tooLoud = setSlice(track, 0, {{QStringLiteral("pan"), 0.5}, {QStringLiteral("gain"), 12.5}});
		QCOMPARE(tooLoud.errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(setSlice(track, 0, {{QStringLiteral("random"), 1.5}}).errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(setSlice(track, 0, {{QStringLiteral("filter"), -1.01}}).errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(setSlice(track, 0, {}).errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(setSlice(track, SliceCount, {{QStringLiteral("gain"), 1.0}}).errorKind, ControlErrorKind::NotFound);
		QCOMPARE(setSlice(track, -1, {{QStringLiteral("gain"), 1.0}}).errorKind, ControlErrorKind::NotFound);
		QCOMPARE(run(QStringLiteral("plugin.slice_get"), {{QStringLiteral("target"), id}, {QStringLiteral("slice"), 7}}).errorKind,
			ControlErrorKind::NotFound);
		QCOMPARE(readSlice(track, 0).value(QStringLiteral("pan")).toDouble(), 0.0);
		QCOMPARE(readSlice(track, 0).value(QStringLiteral("gain")).toDouble(), -2.0);
		QCOMPARE(run(QStringLiteral("control.undo_depth")).result.value(QStringLiteral("undo")).toInt(-1), depth);

		// A track that is not a SlicerT is refused.
		auto* plain = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		QVERIFY(plain != nullptr);
		QVERIFY(!run(QStringLiteral("plugin.slice_get"), {{QStringLiteral("target"), control::trackIdOf(plain)}}).ok);

		// The contract.
		ControlRegistry* registry = ControlRegistry::instance();
		const control::ReversibilityTable& table = control::ReversibilityTable::instance();
		for (const QString& command : {QStringLiteral("plugin.slice_get"), QStringLiteral("plugin.slice_set")})
		{
			const ControlCommand* def = registry->command(command);
			QVERIFY2(def != nullptr, qPrintable(command + " is not registered"));
			QVERIFY(!def->argsSchema.isEmpty());
			QVERIFY(!def->resultSchema.isEmpty());
			QVERIFY(table.lookup(command) != nullptr);
		}
		QVERIFY(!registry->command(QStringLiteral("plugin.slice_get"))->mutating);
		QVERIFY(registry->command(QStringLiteral("plugin.slice_set"))->mutating);
		QCOMPARE(table.lookup(QStringLiteral("plugin.slice_get"))->cls, control::ReversibilityClass::NotMutating);
		QCOMPARE(table.lookup(QStringLiteral("plugin.slice_set"))->cls, control::ReversibilityClass::TrueInverse);
	}

private:
	static int rate() { return static_cast<int>(Engine::audioEngine()->outputSampleRate()); }

	static int key(InstrumentTrack* track, int slice) { return track->baseNote() + 1 + slice; }

	InstrumentTrack* slicerTrack()
	{
		Engine::getSong()->clearProject();
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		if (track == nullptr) { return nullptr; }
		Instrument* instrument = track->loadInstrument(QStringLiteral("slicert"));
		if (instrument == nullptr || instrument->nodeName().toLower() != QStringLiteral("slicert")) { return nullptr; }
		QDomDocument doc;
		instrument->restoreState(slicerSettings(doc, rate()));
		return track;
	}

	//! One period of a note on @a key through the instrument.
	std::vector<SampleFrame> render(InstrumentTrack* track, int key)
	{
		const f_cnt_t fpp = Engine::audioEngine()->framesPerPeriod();
		std::vector<SampleFrame> out(static_cast<std::size_t>(fpp));
		const Note note(TimePos(384), TimePos(0), key);
		NotePlayHandle* handle = NotePlayHandleManager::acquire(track, 0, fpp, note);
		if (handle == nullptr) { return {}; }
		track->instrument()->playNote(handle, std::span<SampleFrame>{out});
		NotePlayHandleManager::release(handle);
		return out;
	}

	static bool sameFrames(const std::vector<SampleFrame>& a, const std::vector<SampleFrame>& b)
	{
		if (a.size() != b.size() || a.empty()) { return false; }
		for (std::size_t i = 0; i < a.size(); ++i)
		{
			if (std::fabs(a[i].left() - b[i].left()) > 1e-5f || std::fabs(a[i].right() - b[i].right()) > 1e-5f) { return false; }
		}
		return true;
	}

	static ControlResult setSlice(InstrumentTrack* track, int slice, QJsonObject values)
	{
		values.insert(QStringLiteral("target"), control::trackIdOf(track));
		values.insert(QStringLiteral("slice"), slice);
		return run(QStringLiteral("plugin.slice_set"), values);
	}

	static QJsonObject readSlice(InstrumentTrack* track, int slice)
	{
		const ControlResult reply = run(QStringLiteral("plugin.slice_get"),
			{{QStringLiteral("target"), control::trackIdOf(track)}, {QStringLiteral("slice"), slice}});
		return reply.ok ? reply.result.value(QStringLiteral("slices")).toArray().at(0).toObject() : QJsonObject{};
	}

	//! Types @a text into a panel spin box the way a user does and commits it with Enter.
	static void type(QDoubleSpinBox* box, const char* text)
	{
		auto* edit = box->findChild<QLineEdit*>();
		box->setFocus();
		edit->selectAll();
		QTest::keyClicks(edit, QString::fromLatin1(text));
		QTest::keyClick(edit, Qt::Key_Return);
	}

	//! The saved state of the track: its XML document element, searched for the SlicerT's attributes.
	static QDomElement trackXml(Track* track)
	{
		QString xml;
		if (!control::captureTrackXml(track, &xml, 1 << 20)) { return {}; }
		static QDomDocument document;
		if (!document.setContent(xml)) { return {}; }
		return document.documentElement();
	}

	QTemporaryDir m_dir;
};

QTEST_MAIN(SlicerTSliceControlsTest)
#include "SlicerTSliceControlsTest.moc"
