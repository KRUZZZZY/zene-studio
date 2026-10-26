/*
 * CrossfadeEditorTest.cpp - the crossfade editor dialog, driven through its widgets (0.5.0 ED-176)
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

/*! 0.5.0 ED-176: an audio clip's context menu offers the crossfade editor exactly while another clip overlaps it;
 *  the editor shows both sides (the overlap at equal power before any ramp exists, the clips' own ramps after),
 *  typing and picking in its fields then Apply sets both through clip.crossfade as one undo step, and a refusal
 *  leaves the clips as they were with the reason on the status line. */

#include <QtTest>

#include <QApplication>
#include <QComboBox>
#include <QColor>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QPointer>
#include <QPushButton>
#include <QScopeGuard>
#include <QSpinBox>

#include <algorithm>
#include <limits>
#include <vector>

#include "ControlRegistry.h"
#include "CrossfadeEditorDialog.h"
#include "Engine.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "ShellCommandMenus.h"
#include "Song.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

//! An audio clip at @a at, @a ticks long, over two seconds of a constant level.
SampleClip* clipAt(Track* track, int at, int ticks)
{
	std::vector<SampleFrame> frames(88200, SampleFrame(0.25f, 0.25f));
	auto* clip = dynamic_cast<SampleClip*>(track->createClip(TimePos(at)));
	clip->setSampleBuffer(std::make_shared<SampleBuffer>(frames.data(), frames.size(), 44100));
	clip->movePosition(TimePos(at));
	clip->changeLength(TimePos(ticks));
	clip->setAutoResize(false);
	return clip;
}

QAction* menuAction(QMenu* menu, const QString& text)
{
	for (QAction* action : menu->actions())
	{
		if (action->text() == text) { return action; }
	}
	return nullptr;
}

template<typename T>
T* named(QWidget* dialog, const QString& name)
{
	for (auto* candidate : dialog->findChildren<T*>())
	{
		if (candidate->accessibleName() == name) { return candidate; }
	}
	return nullptr;
}

QPushButton* button(QWidget* dialog, const QString& text)
{
	for (auto* candidate : dialog->findChildren<QPushButton*>())
	{
		if (candidate->text() == text) { return candidate; }
	}
	return nullptr;
}

void type(QSpinBox* into, int value)
{
	into->setFocus();
	into->selectAll();
	QTest::keyClicks(into, QString::number(value));
	QTest::keyClick(into, Qt::Key_Enter);
}

void pick(QComboBox* box, const QString& label)
{
	box->setFocus();
	const int index = box->findText(label);
	QVERIFY2(index >= 0, qPrintable(label));
	box->setCurrentIndex(index);
}

} // namespace

class CrossfadeEditorTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
		m_track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, Engine::getSong()));
		QVERIFY(m_track != nullptr);
		m_first = clipAt(m_track, 0, 192);
		m_second = clipAt(m_track, 144, 192);  // overlaps the first over ticks 144..192
		m_alone = clipAt(m_track, 960, 96);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void theMenuOffersTheEditorWhereClipsOverlap()
	{
		QCOMPARE(CrossfadeEditorDialog::overlapping(m_first), m_second);
		QCOMPARE(CrossfadeEditorDialog::overlapping(m_second), m_first);
		QVERIFY(CrossfadeEditorDialog::overlapping(m_alone) == nullptr);
		QMenu overlapped;
		ShellCommandMenus::populateClip(&overlapped, m_second);
		QAction* editor = menuAction(&overlapped, QStringLiteral("Crossfade Editor..."));
		QVERIFY(editor != nullptr);
		QVERIFY(editor->isEnabled());
		QCOMPARE(editor->property("controlCommand").toString(), QStringLiteral("clip.crossfade"));
		QMenu alone;
		ShellCommandMenus::populateClip(&alone, m_alone);
		QVERIFY2(!menuAction(&alone, QStringLiteral("Crossfade Editor..."))->isEnabled(), "a clip nothing overlaps offered the editor");

		editor->trigger();  // opens the editor on the pair, in start order
		CrossfadeEditorDialog* open = nullptr;
		for (QWidget* window : QApplication::topLevelWidgets())
		{
			if (auto* dialog = qobject_cast<CrossfadeEditorDialog*>(window)) { open = dialog; }
		}
		QVERIFY(open != nullptr);
		QCOMPARE(open->outClip(), m_first);
		QCOMPARE(open->inClip(), m_second);
		open->close();
	}

	void reopeningAnExistingEditorPreservesPendingControls()
	{
		CrossfadeEditorDialog* open = CrossfadeEditorDialog::openFor(m_first, m_second, nullptr);
		QVERIFY(open != nullptr);
		const QPointer<CrossfadeEditorDialog> guard(open);
		const auto close = qScopeGuard([&] { if (guard) { guard->close(); } });
		const CrossfadeSide pending{12, FadeShape::Exponential};
		open->setOutSide(pending);

		CrossfadeEditorDialog* reopened = CrossfadeEditorDialog::openFor(m_second, m_first, nullptr);

		QCOMPARE(reopened, open);
		QCOMPARE(reopened->outSide().ticks, pending.ticks);
		QVERIFY(reopened->outSide().shape == pending.shape);
	}

	void reopeningBeforeDeferredDeleteCreatesNewEditor()
	{
		CrossfadeEditorDialog* closing = CrossfadeEditorDialog::openFor(m_first, m_second, nullptr);
		QVERIFY(closing != nullptr);
		const QPointer<CrossfadeEditorDialog> closingGuard(closing);
		QVERIFY(closing->isVisible());

		closing->close();
		QVERIFY(!closing->isVisible());
		QVERIFY(!closingGuard.isNull());  // close() has queued deletion, but it has not run yet.

		CrossfadeEditorDialog* reopened = CrossfadeEditorDialog::openFor(m_second, m_first, nullptr);
		QVERIFY(reopened != nullptr);
		QVERIFY(reopened != closingGuard.data());
		QVERIFY(reopened->isVisible());
		const QPointer<CrossfadeEditorDialog> reopenedGuard(reopened);
		const auto closeReopened = qScopeGuard([&] { if (reopenedGuard) { reopenedGuard->close(); } });
	}

	void paintingUsesWideBoundsAtBothIntLimits()
	{
		CrossfadeCurveView view;
		view.resize(420, 140);
		view.show();
		const auto hasColorNear = [&](const QImage& image, const QRect& area, const QColor& color) {
			for (int y = area.top(); y <= area.bottom(); ++y)
			{
				for (int x = area.left(); x <= area.right(); ++x)
				{
					if (image.pixelColor(x, y) == color) { return true; }
				}
			}
			return false;
		};

		view.setRamps(500, {10, FadeShape::EqualPower}, 480, {30, FadeShape::EqualPower});
		QImage normal(view.size(), QImage::Format_ARGB32_Premultiplied);
		normal.fill(Qt::transparent);
		view.render(&normal);
		QVERIFY(hasColorNear(normal, QRect(180, 20, 60, 50), view.palette().highlight().color()));

		const int limit = std::numeric_limits<int>::max();
		view.setRamps(limit - 100, {10, FadeShape::EqualPower}, limit - 50, {100, FadeShape::EqualPower});
		QImage nearLimit(view.size(), QImage::Format_ARGB32_Premultiplied);
		nearLimit.fill(Qt::transparent);
		view.render(&nearLimit);
		const bool upperCurveVisible = hasColorNear(nearLimit, QRect(260, 20, 60, 50), view.palette().highlight().color());

		const int lowerLimit = std::numeric_limits<int>::min();
		view.setRamps(lowerLimit + 50, {100, FadeShape::EqualPower}, lowerLimit + 70,
			{20, FadeShape::EqualPower});
		QImage belowLimit(view.size(), QImage::Format_ARGB32_Premultiplied);
		belowLimit.fill(Qt::transparent);
		view.render(&belowLimit);
		const bool lowerCurveVisible = hasColorNear(belowLimit, QRect(125, 20, 55, 50), view.palette().text().color());
		QVERIFY2(upperCurveVisible && lowerCurveVisible,
			qPrintable(QStringLiteral("upper curve visible: %1; lower curve visible: %2")
				.arg(upperCurveVisible).arg(lowerCurveVisible)));
	}

	void theEditorTracksClipMovesAndResizes()
	{
		CrossfadeEditorDialog dialog(m_first, m_second);
		dialog.setAttribute(Qt::WA_DeleteOnClose, false);
		const TimePos originalStart = m_second->startPosition();
		const TimePos originalLength = m_first->length();
		const auto restoreClips = qScopeGuard([&] {
			m_second->movePosition(originalStart);
			m_first->changeLength(originalLength);
		});
		dialog.show();
		QVERIFY(QTest::qWaitForWindowExposed(&dialog));
		QLabel* summary = nullptr;
		for (QLabel* candidate : dialog.findChildren<QLabel*>())
		{
			if (candidate->text().contains(QStringLiteral("overlap by"))) { summary = candidate; }
		}
		QVERIFY(summary != nullptr);
		QWidget* curves = named<QWidget>(&dialog, QStringLiteral("Crossfade curves"));
		QVERIFY(curves != nullptr);

		m_second->movePosition(TimePos(168));
		QCOMPARE(dialog.overlap(), 24);
		QVERIFY(summary->text().contains(QStringLiteral("24 ticks")));
		QVERIFY(curves->accessibleDescription().contains(QStringLiteral("over 24 ticks")));
		QCOMPARE(dialog.outSide().ticks, 24);
		QCOMPARE(dialog.inSide().ticks, 24);

		m_first->changeLength(TimePos(160));
		QCOMPARE(dialog.overlap(), 0);
		QVERIFY(summary->text().contains(QStringLiteral("0 ticks")));
		QVERIFY(curves->accessibleDescription().contains(QStringLiteral("over 1 ticks")));
	}

	void unrelatedClipChangesPreservePendingControls()
	{
		CrossfadeEditorDialog dialog(m_first, m_second);
		const QString originalName = m_first->name();
		const auto restoreName = qScopeGuard([&] { m_first->setName(originalName); });
		const CrossfadeSide pending{12, FadeShape::Exponential};
		dialog.setOutSide(pending);

		m_first->setName(QStringLiteral("Crossfade editor test rename"));

		QCOMPARE(dialog.outSide().ticks, pending.ticks);
		QVERIFY(dialog.outSide().shape == pending.shape);
	}

	void applyingAfterAnExternalFadeEditKeepsThatEdit()
	{
		CrossfadeEditorDialog dialog(m_first, m_second);
		const ClipEdits originalOut = m_first->clipEdits();
		const ClipEdits originalIn = m_second->clipEdits();
		const auto restoreFades = qScopeGuard([&] {
			m_first->setClipEdits(originalOut);
			m_second->setClipEdits(originalIn);
		});
		ClipEdits external = m_first->clipEdits();
		external.fadeOutTicks = 18;
		external.fadeOutShape = FadeShape::Exponential;
		m_first->setClipEdits(external);
		QCOMPARE(dialog.outSide().ticks, 18);
		QVERIFY(dialog.outSide().shape == FadeShape::Exponential);

		QVERIFY(dialog.apply());
		QCOMPARE(m_first->clipEdits().fadeOutTicks, 18);
		QVERIFY(m_first->clipEdits().fadeOutShape == FadeShape::Exponential);
	}

	void theEditorShowsAndSetsBothSides()
	{
		CrossfadeEditorDialog dialog(m_first, m_second);
		dialog.setAttribute(Qt::WA_DeleteOnClose, false);
		dialog.show();
		QVERIFY(QTest::qWaitForWindowExposed(&dialog));
		QCOMPARE(dialog.overlap(), 48);
		// No ramp yet: each side shows the overlap at equal power, what clip.crossfade does by default.
		QCOMPARE(dialog.outSide().ticks, 48);
		QCOMPARE(dialog.inSide().ticks, 48);
		QVERIFY(dialog.outSide().shape == FadeShape::EqualPower);

		type(named<QSpinBox>(&dialog, QStringLiteral("Outgoing clip - fade out length")), 24);
		type(named<QSpinBox>(&dialog, QStringLiteral("Incoming clip - fade in length")), 36);
		pick(named<QComboBox>(&dialog, QStringLiteral("Outgoing clip - fade out shape")), QStringLiteral("Exponential"));
		pick(named<QComboBox>(&dialog, QStringLiteral("Incoming clip - fade in shape")), QStringLiteral("S-Curve"));
		QPushButton* apply = button(&dialog, QStringLiteral("Apply"));
		QVERIFY(apply != nullptr);
		QCOMPARE(apply->property("controlCommand").toString(), QStringLiteral("clip.crossfade"));
		apply->click();
		QVERIFY2(dialog.status().contains(QStringLiteral("applied")), qPrintable(dialog.status()));
		QCOMPARE(m_first->clipEdits().fadeOutTicks, 24);
		QVERIFY(m_first->clipEdits().fadeOutShape == FadeShape::Exponential);
		QCOMPARE(m_second->clipEdits().fadeInTicks, 36);
		QVERIFY(m_second->clipEdits().fadeInShape == FadeShape::SCurve);

		// A second editor on the pair reads the clips' own ramps back.
		CrossfadeEditorDialog again(m_first, m_second);
		QCOMPARE(again.outSide().ticks, 24);
		QVERIFY(again.inSide().shape == FadeShape::SCurve);

		// Both sides were one step: one undo takes both back. clip.crossfade's undo is the TRACK checkpoint, which
		// rebuilds the track's clips, so they are read back from the track (and the open editor's clips are gone).
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("control.undo"), {}).ok);
		rereadClips();
		QCOMPARE(m_first->clipEdits().fadeOutTicks, 0);
		QCOMPARE(m_second->clipEdits().fadeInTicks, 0);
		QVERIFY(dialog.outClip() == nullptr && !dialog.apply());
	}

	void aRefusalLeavesTheClipsAsTheyWere()
	{
		CrossfadeEditorDialog dialog(m_first, m_second);
		dialog.setAttribute(Qt::WA_DeleteOnClose, false);
		dialog.show();
		QVERIFY(QTest::qWaitForWindowExposed(&dialog));
		ClipEdits tail = m_second->clipEdits();
		tail.fadeOutTicks = 10;
		m_second->setClipEdits(tail);
		// 190 in + the second clip's own 10-tick fade-out exceed its 192 ticks: clip.crossfade refuses.
		type(named<QSpinBox>(&dialog, QStringLiteral("Incoming clip - fade in length")), 190);
		QVERIFY(!dialog.apply());
		QVERIFY2(dialog.status().contains(QStringLiteral("not applied")), qPrintable(dialog.status()));
		QCOMPARE(m_second->clipEdits().fadeInTicks, 0);
		QCOMPARE(m_first->clipEdits().fadeOutTicks, 0);
	}

private:
	//! The track's three clips again, by start position.
	void rereadClips()
	{
		std::vector<SampleClip*> clips;
		for (Clip* clip : m_track->getClips()) { clips.push_back(dynamic_cast<SampleClip*>(clip)); }
		std::sort(clips.begin(), clips.end(), [](const SampleClip* a, const SampleClip* b) { return a->startPosition() < b->startPosition(); });
		QCOMPARE(clips.size(), std::size_t{3});
		m_first = clips[0];
		m_second = clips[1];
		m_alone = clips[2];
	}

	SampleTrack* m_track = nullptr;
	SampleClip* m_first = nullptr;
	SampleClip* m_second = nullptr;
	SampleClip* m_alone = nullptr;
};

QTEST_MAIN(CrossfadeEditorTest)
#include "CrossfadeEditorTest.moc"
