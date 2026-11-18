/*
 * SampleClipWarpEditTestSupport.h - warp markers authored over a sample clip's waveform (Q050-063)
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

// Everything here drives the REAL song editor: the clip's own SampleClipView takes the pointer events a
// musician makes, the commands it sends are the registered warp.add / warp.move / warp.remove, and the
// marker map it shows is read back against warp.list. The project render is the real export path.

#ifndef LMMS_TESTS_SAMPLE_CLIP_WARP_EDIT_TEST_SUPPORT_H
#define LMMS_TESTS_SAMPLE_CLIP_WARP_EDIT_TEST_SUPPORT_H

#include <QtTest>

#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QImage>
#include <QJsonArray>
#include <QMenu>
#include <QMouseEvent>

#include <cmath>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleClipView.h"
#include "SampleFrame.h"

namespace lmms::gui
{

namespace
{

constexpr int kRate = 44100;
constexpr int kSeconds = 4;
//! The source: silence with a 2000-frame burst at each of these frames, so the transients are known.
constexpr f_cnt_t kBursts[] = {22050, 44100, 66150};
constexpr int kBurstLength = 2000;

std::shared_ptr<const SampleBuffer> burstBuffer()
{
	std::vector<SampleFrame> data(static_cast<std::size_t>(kSeconds) * kRate, SampleFrame(0.0f, 0.0f));
	for (const f_cnt_t start : kBursts)
	{
		for (int i = 0; i < kBurstLength; ++i) { data[static_cast<std::size_t>(start) + i] = SampleFrame(0.8f, 0.8f); }
	}
	return std::make_shared<const SampleBuffer>(data.data(), data.size(), kRate);
}

QJsonObject clipArgs(const Clip* clip) { return {{QStringLiteral("clip"), control::clipIdOf(clip)}}; }

//! warp.list as the agent reads it: [source_frame, offset_ticks] per marker, in order.
std::vector<std::pair<qint64, qint64>> agentMap(const Clip* clip)
{
	std::vector<std::pair<qint64, qint64>> map;
	const ControlResult list = ControlRegistry::instance()->invoke(QStringLiteral("warp.list"), clipArgs(clip));
	for (const QJsonValue& value : list.result.value(QStringLiteral("markers")).toArray())
	{
		const QJsonObject marker = value.toObject();
		map.emplace_back(static_cast<qint64>(marker.value(QStringLiteral("source_frame")).toDouble()),
			static_cast<qint64>(marker.value(QStringLiteral("offset_ticks")).toDouble()));
	}
	return map;
}

//! The same map as the clip view draws it.
std::vector<std::pair<qint64, qint64>> viewMap(SampleClipView* view)
{
	std::vector<std::pair<qint64, qint64>> map;
	for (const WarpGuide& guide : view->shownWarpGuides()) { map.emplace_back(guide.sourceFrame, guide.offsetTicks); }
	return map;
}

QImage widgetImage(QWidget* widget)
{
	QImage image(widget->size(), QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	widget->render(&image);
	return image;
}

//! How many rows of pixel column @a x carry the guide's orange.
int guideRowsAt(const QImage& image, int x)
{
	int rows = 0;
	if (x < 0 || x >= image.width()) { return 0; }
	for (int y = 0; y < image.height(); ++y)
	{
		const QColor c = image.pixelColor(x, y);
		if (c.red() > 0xe0 && c.green() > 0x90 && c.green() < 0xc8 && c.blue() < 0x40) { ++rows; }
	}
	return rows;
}

//! The context menu that is open now, or null.
QMenu* openMenu()
{
	if (auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget())) { return menu; }
	for (QWidget* top : QApplication::topLevelWidgets())
	{
		auto* candidate = qobject_cast<QMenu*>(top);
		if (candidate != nullptr && candidate->isVisible()) { return candidate; }
	}
	return nullptr;
}

//! The action called @a text in @a menu, or null.
QAction* actionNamed(const QMenu* menu, const QString& text, bool submenu)
{
	for (QAction* action : menu->actions())
	{
		if (action->text() == text && (action->menu() != nullptr) == submenu) { return action; }
	}
	return nullptr;
}

//! Opens the clip's context menu and triggers [submenu] > action; false if either was not there.
bool triggerInSubmenu(QWidget* target, const QString& submenu, const QString& wanted)
{
	bool found = false;
	QTimer::singleShot(0, [&]() {
		QMenu* menu = openMenu();
		if (menu == nullptr) { return; }
		const QAction* entry = actionNamed(menu, submenu, true);
		QAction* action = entry != nullptr ? actionNamed(entry->menu(), wanted, false) : nullptr;
		if (action != nullptr) { found = true; action->trigger(); }
		menu->close();
	});
	QTimer::singleShot(1500, []() {
		if (QWidget* popup = QApplication::activePopupWidget()) { popup->close(); }
	});
	const QPoint at(target->width() / 2, target->height() / 2);
	QContextMenuEvent context(QContextMenuEvent::Mouse, at, target->mapToGlobal(at), Qt::NoModifier);
	QCoreApplication::sendEvent(target, &context);
	return found;
}

void movePointer(QWidget* target, const QPoint& to, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
	QMouseEvent move(QEvent::MouseMove, QPointF(to), QPointF(target->mapToGlobal(to)), Qt::NoButton, Qt::LeftButton, modifiers);
	QCoreApplication::sendEvent(target, &move);
}

// Just enough of the 16-bit PCM WAV format for the renderer's own output.
int dataOffset(const QByteArray& wav)
{
	int offset = 12;
	while (offset + 8 <= wav.size())
	{
		qint32 size = 0;
		std::memcpy(&size, wav.constData() + offset + 4, sizeof(size));
		if (wav.mid(offset, 4) == QByteArrayLiteral("data")) { return offset + 8; }
		offset += 8 + size + (size & 1);
	}
	return -1;
}

//! The frames where a burst starts in the render: the left channel clearly above silence after at least
//! 5000 frames of it.
std::vector<int> burstOnsets(const QByteArray& wav)
{
	std::vector<int> onsets;
	const int offset = dataOffset(wav);
	if (offset < 0) { return onsets; }
	int lastAudible = -100000;
	for (int frame = 0; offset + frame * 4 + 2 <= wav.size(); ++frame)
	{
		qint16 value = 0;
		std::memcpy(&value, wav.constData() + offset + frame * 4, sizeof(value));
		if (std::abs(value) <= 3000) { continue; }
		if (frame - lastAudible > 5000) { onsets.push_back(frame); }
		lastAudible = frame;
	}
	return onsets;
}

} // namespace

} // namespace lmms::gui

#endif // LMMS_TESTS_SAMPLE_CLIP_WARP_EDIT_TEST_SUPPORT_H
