/*
 * PinConnector.h - View for AudioPortsModel
 *
 * Copyright (c) 2025 Dalton Messmer <messmer.dalton/at/gmail.com>
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

#ifndef LMMS_GUI_PIN_CONNECTOR_H
#define LMMS_GUI_PIN_CONNECTOR_H

#include <optional>
#include <utility>
#include <QJsonArray>
#include <QString>
#include <QWidget>

#include "embed.h"
#include "lmms_export.h"
#include "AudioPortsModel.h"
#include "ModelView.h"

class QMenu;
class QScrollArea;
class QToolButton;

namespace lmms
{

namespace gui
{

class SubWindow;

class LMMS_EXPORT PinConnector
	: public QWidget
	, public ModelView
{
	Q_OBJECT

	Q_PROPERTY(QColor backgroundColor MEMBER m_backgroundColor)
	Q_PROPERTY(QColor passthroughLineColor MEMBER m_passthroughLineColor)

public:
	explicit PinConnector(AudioPortsModel* model);
	~PinConnector() override;

	auto sizeHint() const -> QSize override;
	auto minimumSizeHint() const -> QSize override;

	void toggleVisibility();
	void closeWindow();

	/*! Q050-155.2: the device's control address (its chain's owner "ch-<n>"/"trk-<n>" and its "fx-<n>"). Once
	 *  set, a pin click and the Channels menu both run port.set_pins through ControlRegistry - the agent's
	 *  command, one undo step each on the engine's history - and the matrix grows to the host bus's channels (a
	 *  discrete-N mixer channel: N). Without one (a view opened outside a device card) a click writes the
	 *  model directly, as it always did. */
	void setControlAddress(const QString& target, const QString& device);
	//! The Channels menu: one entry per pair of the host bus, each pinning the device onto that pair.
	auto channelsMenu() const -> QMenu* { return m_channelsMenu; }
	//! port.set_pins' list that moves every pin of the device onto the bus's channels from @a firstChannel.
	auto pinsFrom(int firstChannel) const -> QJsonArray;
	//! Runs port.set_pins for one pin (the matrix views call it); false when there is no control address.
	auto dispatchPin(bool output, ch_cnt_t trackChannel, ch_cnt_t processorChannel, bool enabled) -> bool;

protected:
	void paintEvent(QPaintEvent* pe) override;

private:
	class MatrixView;

	auto trackChannelName(const AudioPortsModel& model, ch_cnt_t channel) const -> QString;
	auto getSpacerWidth() const -> int;
	auto getMaximumWindowSize() const -> QSize;
	void updateProperties();

	SubWindow* m_subWindow = nullptr;
	QScrollArea* m_scrollArea = nullptr;
	QWidget* m_spacer = nullptr;

	MatrixView* m_inView = nullptr;
	MatrixView* m_outView = nullptr;

	void rebuildChannelsMenu();

	QString m_target;
	QString m_device;
	int m_busChannels = 2;
	QToolButton* m_channelsButton = nullptr;
	QMenu* m_channelsMenu = nullptr;

	QColor m_backgroundColor;
	QColor m_passthroughLineColor;
};


class PinConnector::MatrixView : public QWidget
{
	Q_OBJECT

	Q_PROPERTY(QColor lineColor MEMBER m_lineColor)
	Q_PROPERTY(QColor enabledColor MEMBER m_enabledColor)
	Q_PROPERTY(QColor disabledColor MEMBER m_disabledColor)

public:
	MatrixView(const PinConnector* view, AudioPortsModel::Matrix& matrix);
	~MatrixView() override = default;

	auto sizeHint() const -> QSize override;
	auto minimumSizeHint() const -> QSize override;
	void paintEvent(QPaintEvent*) override;
	void mouseMoveEvent(QMouseEvent* me) override;
	void mousePressEvent(QMouseEvent* me) override;

	//! Side length of square cells
	static constexpr auto cellSize() -> int
	{
		return s_pinSize + (s_pinInnerMargin * 2) + s_lineThickness;
	}

	static constexpr int BorderWidth = 1;

public slots:
	void updateProperties(const PinConnector* view);

private:
	auto getPin(const QPoint& mousePos) -> std::optional<std::pair<ch_cnt_t, ch_cnt_t>>;
	auto getColor(ch_cnt_t trackChannel, ch_cnt_t processorChannel) -> QColor;

	AudioPortsModel::Matrix* m_matrix = nullptr;
	PinConnector* m_owner = nullptr;

	QColor m_lineColor;
	QColor m_enabledColor;
	QColor m_disabledColor;

	static constexpr int s_pinSize = 13;
	static constexpr int s_pinInnerMargin = 1;
	static constexpr int s_lineThickness = 1;
};

} // namespace gui

} // namespace lmms

#endif // LMMS_GUI_PIN_CONNECTOR_H
