/*
 * TransportReadouts.cpp - the shell header's tempo, position, meter, level and load readouts
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

#include "TransportReadouts.h"

#include <cmath>

#include <QActionGroup>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>
#include <QWheelEvent>

#include "Accessibility.h"
#include "AudioEngine.h"
#include "CommandForm.h"
#include "ControlRegistry.h"
#include "DesignTokens.h"
#include "Engine.h"
#include "MeterModel.h"
#include "MusicalTime.h"
#include "ProjectJournal.h"
#include "RecordOptions.h"
#include "Song.h"
#include "TempoMap.h"

namespace lmms::gui
{

namespace
{

constexpr int kCaptionHeight = 13;

QString signedValue(int v) { return v > 0 ? QStringLiteral("+%1").arg(v) : QString::number(v); }

//! WF-085: the Rec left readout's source, the live record.remaining command unless a test set
//! its own (setRemainingTestSource); read and written on the UI thread only, like the readout.
TransportReadouts::RemainingTestSource g_remainingTestSource;

} // namespace

ReadoutField::ReadoutField(const QString& caption, QWidget* parent)
	: QWidget(parent)
	, m_caption(caption)
{
	setFocusPolicy(Qt::TabFocus);
	setAttribute(Qt::WA_Hover, true);
	a11y::announce(this, caption);
}

void ReadoutField::bind(IntModel* model, std::function<QString(int)> format, int pixelsPerStep)
{
	m_model = model;
	m_format = std::move(format);
	m_pixelsPerStep = std::max(1, pixelsPerStep);
	setCursor(Qt::SizeVerCursor);
	setToolTip(tr("%1: drag or scroll to change, double-click to type").arg(m_caption));
	connect(model, &Model::dataChanged, this, &ReadoutField::refresh);
	refresh();
}

void ReadoutField::bindText(std::function<QString()> text, std::function<bool(const QString&)> typed)
{
	m_text = std::move(text);
	m_typed = std::move(typed);
	m_typedResult = {};
	if (m_typed) { setToolTip(tr("%1: double-click to type").arg(m_caption)); }
	refresh();
}

void ReadoutField::bindTextResult(std::function<QString()> text, std::function<ControlResult(const QString&)> typed)
{
	m_text = std::move(text);
	m_typed = {};
	m_typedResult = std::move(typed);
	if (m_typedResult) { setToolTip(tr("%1: double-click to type").arg(m_caption)); }
	refresh();
}

void ReadoutField::refresh()
{
	QString value;
	if (m_model != nullptr && m_format) { value = m_format(m_model->value()); }
	else if (m_text) { value = m_text(); }
	if (value == m_value) { return; }
	m_value = value;
	setAccessibleDescription(m_value);
	updateGeometry();
	update();
}

QSize ReadoutField::sizeHint() const
{
	const QFontMetrics value(tokens::font(tokens::TextRole::Display));
	const QFontMetrics caption(tokens::font(tokens::TextRole::Caption));
	const int w = std::max(value.horizontalAdvance(m_value.isEmpty() ? QStringLiteral("000") : m_value),
		caption.horizontalAdvance(m_caption.toUpper()) + 2);
	return {w + 2 * tokens::space::md, tokens::size::headerHeight - 2 * tokens::space::sm};
}

void ReadoutField::paintEvent(QPaintEvent*)
{
	const tokens::Palette& pal = tokens::palette();
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
	const bool live = m_model != nullptr || m_typed || m_typedResult;
	p.setPen(Qt::NoPen);
	p.setBrush(m_dragging ? pal.selected : (underMouse() && live) ? pal.controlHover : pal.control);
	p.drawRoundedRect(r, tokens::radius::lg, tokens::radius::lg);
	if (hasFocus())
	{
		p.setPen(QPen(pal.focusRing, tokens::focusRingWidth()));
		p.setBrush(Qt::NoBrush);
		p.drawRoundedRect(r.adjusted(0.75, 0.75, -0.75, -0.75), tokens::radius::lg, tokens::radius::lg);
	}
	const QRect inner = rect().adjusted(tokens::space::md, tokens::space::xs, -tokens::space::md, -tokens::space::xs);
	p.setFont(tokens::font(tokens::TextRole::Caption));
	p.setPen(pal.textMuted);
	p.drawText(QRect(inner.left(), inner.top(), inner.width(), kCaptionHeight), Qt::AlignLeft | Qt::AlignVCenter,
		m_caption.toUpper());
	p.setFont(tokens::font(tokens::TextRole::Display));
	p.setPen(pal.text);
	p.drawText(QRect(inner.left(), inner.top() + kCaptionHeight, inner.width(), inner.height() - kCaptionHeight),
		Qt::AlignLeft | Qt::AlignVCenter, m_value);
}

void ReadoutField::mousePressEvent(QMouseEvent* event)
{
	if (m_model == nullptr || event->button() != Qt::LeftButton) { return; }
	// One journal step for the whole drag, as LcdSpinBox makes it.
	m_model->addJournalCheckPoint();
	m_model->saveJournallingState(false);
	m_pressY = event->pos().y();
	m_pressValue = m_model->value();
	m_dragging = true;
	update();
}

void ReadoutField::mouseMoveEvent(QMouseEvent* event)
{
	if (!m_dragging || m_model == nullptr) { return; }
	const int perStep = event->modifiers().testFlag(Qt::ShiftModifier) ? m_pixelsPerStep * 4 : m_pixelsPerStep;
	m_model->setValue(static_cast<float>(m_pressValue + (m_pressY - event->pos().y()) / perStep));
}

void ReadoutField::mouseReleaseEvent(QMouseEvent*)
{
	if (!m_dragging) { return; }
	m_dragging = false;
	if (m_model != nullptr) { m_model->restoreJournallingState(); }
	update();
}

void ReadoutField::wheelEvent(QWheelEvent* event)
{
	// A wheel event with no vertical turn (a horizontal scroll) changes nothing (review L19 #8).
	if (m_model == nullptr || event->angleDelta().y() == 0) { return; }
	step(event->angleDelta().y() > 0 ? 1 : -1);
	event->accept();
}

void ReadoutField::keyPressEvent(QKeyEvent* event)
{
	if (event->key() == Qt::Key_Up) { step(1); }
	else if (event->key() == Qt::Key_Down) { step(-1); }
	else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) { mouseDoubleClickEvent(nullptr); }
	else { QWidget::keyPressEvent(event); }
}

void ReadoutField::step(int delta)
{
	if (m_model == nullptr) { return; }
	m_model->setValue(static_cast<float>(m_model->value() + delta));
}

void ReadoutField::mouseDoubleClickEvent(QMouseEvent*)
{
	if (m_model == nullptr && !m_typed && !m_typedResult) { return; }
	if (m_editor == nullptr)
	{
		m_editor = new QLineEdit(this);
		m_editor->setFrame(false);
		m_editor->setFont(tokens::font(tokens::TextRole::Display));
		m_editor->setAccessibleName(tr("Type %1").arg(m_caption));
		connect(m_editor, &QLineEdit::editingFinished, this, &ReadoutField::commitTyped);
	}
	m_editor->setGeometry(rect().adjusted(tokens::space::xs, kCaptionHeight, -tokens::space::xs, -tokens::space::xxs));
	m_editor->setText(m_model != nullptr ? QString::number(m_model->value()) : m_value);
	m_editor->selectAll();
	m_editor->show();
	m_editor->setFocus();
}

void ReadoutField::commitTyped()
{
	if (m_editor == nullptr || !m_editor->isVisible()) { return; }
	const QString text = m_editor->text().trimmed();
	m_editor->hide();
	bool accepted = false;
	ControlResult typedResult = ControlResult::success();
	if (m_typedResult)
	{
		typedResult = m_typedResult(text);
		accepted = typedResult.ok;
	}
	else if (m_typed) { accepted = m_typed(text); }
	else if (m_model != nullptr)
	{
		const int v = text.toInt(&accepted);
		if (accepted)
		{
			m_model->addJournalCheckPoint();
			m_model->setValue(static_cast<float>(v));
		}
	}
	refresh();
	setFocus();
	// Typed text the readout refuses says so, rather than the old value quietly coming back (review r4 L19 #15).
	if (!accepted && m_editor->isModified())
	{
		const QString reason = typedResult.errorMessage.isEmpty()
			? tr("%1: \"%2\" was not accepted").arg(m_caption, text) : typedResult.errorMessage;
		QToolTip::showText(mapToGlobal(QPoint(0, height())), reason, this);
	}
}

namespace
{

//! "-14.2 / -1.0" (short-term LUFS / true peak dBTP) while the meter runs; "off" otherwise.
QString loudnessText()
{
	const ControlResult state = ControlRegistry::instance()->invoke(QStringLiteral("meter.get_state"));
	if (!state.ok || !state.result.value(QStringLiteral("armed")).toBool()) { return QObject::tr("off"); }
	const QJsonObject live = state.result.value(QStringLiteral("live")).toObject();
	const auto reading = [&live](const char* key) {
		const QJsonValue value = live.value(QLatin1String(key));
		return value.isDouble() ? QString::number(value.toDouble(), 'f', 1) : QStringLiteral("-");
	};
	return QStringLiteral("%1 / %2").arg(reading("short_term_lufs"), reading("true_peak_dbtp"));
}

//! Typed "n/d" into the meter: both halves as ONE undo step, not one per half (review L19 #5). A meter the
//! tempo map and the control surface would refuse (a numerator past 32, a denominator not a power of two up
//! to 32) is refused here too, rather than clamped by the models into some other meter (review r4 L19 #15).
bool setMeterTyped(MeterModel* meter, const QString& typed)
{
	const QStringList parts = typed.split(QLatin1Char('/'));
	bool okN = false;
	bool okD = false;
	const int n = parts.value(0).toInt(&okN);
	const int d = parts.value(1).toInt(&okD);
	if (parts.size() != 2 || !okN || !okD || n < TempoMapMinNumerator || n > TempoMapMaxNumerator
		|| !isSupportedTempoMapDenominator(d)) { return false; }
	IntModel& numerator = meter->numeratorModel();
	IntModel& denominator = meter->denominatorModel();
	if (numerator.value() == n && denominator.value() == d) { return true; }
	Engine::projectJournal()->addJournalCheckPoint(QVector<JournallingObject*>{&numerator, &denominator});
	numerator.saveJournallingState(false);
	denominator.saveJournallingState(false);
	numerator.setValue(static_cast<float>(n));
	denominator.setValue(static_cast<float>(d));
	numerator.restoreJournallingState();
	denominator.restoreJournallingState();
	return true;
}

} // namespace

//! WF-085: the Rec left readout's words. The estimate comes straight from record.remaining,
//! so the header and the agent surface show the same number whatever the budget.
QString recordRemainingLabel(const ControlResult& state)
{
	const double seconds = state.ok ? state.result.value(QStringLiteral("seconds")).toDouble(-1.0) : -1.0;
	if (seconds < 0.0) { return QObject::tr("-"); }  // the command refused: a volume it could not measure
	const qint64 minutes = static_cast<qint64>(seconds / 60.0);  // the floor: the readout keeps its minute until the next is there
	if (minutes >= 60)
	{
		return QObject::tr("%1h %2m").arg(minutes / 60).arg(minutes % 60);
	}
	return QObject::tr("%1m").arg(minutes);  // "0m" is the spent budget (a zero estimate)
}

QString barBeatText(int ticks)
{
	return musicaltime::barBeatText(ticks);
}

bool ticksFromBarBeat(const QString& text, int* ticks)
{
	return musicaltime::positionFromText(text, ticks, 0, musicaltime::Format::BarsBeats);
}

namespace
{

//! One checkable right-click action on @a widget per value in @a values: each runs @a id with {@a key: value}.
QActionGroup* addChoices(QWidget* widget, const QString& id, const QString& key, const QStringList& values,
	const QStringList& labels)
{
	widget->setContextMenuPolicy(Qt::ActionsContextMenu);
	auto* group = new QActionGroup(widget);
	for (int i = 0; i < values.size(); ++i)
	{
		auto* action = new QAction(labels.value(i, values.at(i)), group);
		action->setCheckable(true);
		action->setProperty("controlCommand", id);
		const QJsonObject args{{key, values.at(i)}};
		QObject::connect(action, &QAction::triggered, widget, [widget, id, args] {
			runCommand(widget, CommandCall{id, args, {}, QString(), false});
		});
		widget->addAction(action);
	}
	return group;
}

void checkChoice(QActionGroup* group, int index)
{
	const QList<QAction*> actions = group->actions();
	for (int i = 0; i < actions.size(); ++i) { actions.at(i)->setChecked(i == index); }
}

} // namespace

void addTimeFormatChoices(QWidget* widget)
{
	QStringList labels;
	for (int i = 0; i < musicaltime::formatNames().size(); ++i)
	{
		labels << QObject::tr("Show %1").arg(musicaltime::formatLabel(static_cast<musicaltime::Format>(i)));
	}
	QActionGroup* group = addChoices(widget, QStringLiteral("transport.time_format"), QStringLiteral("format"),
		musicaltime::formatNames(), labels);
	const auto sync = [group] { checkChoice(group, static_cast<int>(musicaltime::currentFormat())); };
	sync();
	QObject::connect(musicaltime::formatSignal(), &musicaltime::FormatSignal::changed, group, sync);
}

TransportReadouts::TransportReadouts(QWidget* parent)
	: QWidget(parent)
{
	setObjectName(QStringLiteral("transportReadouts"));
	auto* row = new QHBoxLayout(this);
	row->setContentsMargins(0, 0, 0, 0);
	row->setSpacing(tokens::space::xs);
	Song* song = Engine::getSong();
	m_tempo = new ReadoutField(tr("Tempo"), this);
	m_meter = new ReadoutField(tr("Meter"), this);
	m_position = new ReadoutField(tr("Position"), this);
	m_time = new ReadoutField(tr("Time"), this);
	m_recordMode = new ReadoutField(tr("Rec mode"), this);
	m_recordRemaining = new ReadoutField(tr("Rec left"), this);  // WF-085
	m_volume = new ReadoutField(tr("Master"), this);
	m_pitch = new ReadoutField(tr("Transpose"), this);
	m_load = new ReadoutField(tr("CPU"), this);
	m_loudness = new ReadoutField(tr("LUFS"), this);
	for (ReadoutField* f : {m_tempo, m_meter, m_position, m_time, m_recordMode, m_recordRemaining, m_volume,
		m_pitch, m_load, m_loudness}) { row->addWidget(f); }
	// A shell built without an engine (a widget test) shows the readouts inert.
	if (song != nullptr) { bindToSong(song); }
}

void TransportReadouts::bindToSong(Song* song)
{
	m_tempo->bind(&song->tempoModel(), [](int v) { return QStringLiteral("%1").arg(v); }, 3);
	MeterModel* meter = &song->getTimeSigModel();
	m_meter->bindText([meter]() { return QStringLiteral("%1/%2").arg(meter->getNumerator()).arg(meter->getDenominator()); },
		[meter](const QString& typed) { return setMeterTyped(meter, typed); });
	connect(meter, &Model::dataChanged, m_meter, &ReadoutField::refresh);
	// Theme 7: the play position in the time format (bars|beats by default); typed in any of them it jumps
	// there (transport.seek), as typing a position does in any DAW. Right-click picks the format.
	m_position->bindText([song]() { return musicaltime::counterText(song->getPlayPos().getTicks()); },
		[](const QString& typed) {
			tick_t ticks = 0;
			return musicaltime::positionFromText(typed, &ticks)
				&& ControlRegistry::instance()->invoke(QStringLiteral("transport.seek"), {{QStringLiteral("ticks"), ticks}}).ok;
		});
	m_position->setToolTip(tr("Position: double-click to type a bar.beat.sixteenth, ticks (\"768t\") or a time "
		"(\"0:08.5\"); right-click to show ticks or time instead of bars and beats"));
	addTimeFormatChoices(m_position);
	connect(musicaltime::formatSignal(), &musicaltime::FormatSignal::changed, m_position, &ReadoutField::refresh);
	m_time->bindText([song]() {
		const int ms = song->getMilliseconds();
		return QStringLiteral("%1:%2.%3").arg(ms / 60000).arg((ms / 1000) % 60, 2, 10, QLatin1Char('0'))
			.arg((ms % 1000) / 10, 2, 10, QLatin1Char('0'));
	});
	// Theme 7: the record mode, its scope in the tooltip - what a MIDI pass does to the notes already there.
	// Right-click chooses (transport.record_options); typing "overdub" or "replace" does too.
	m_recordMode->bindText([]() { return recordoptions::modeLabel(recordoptions::options().mode); },
		[](const QString& typed) {
			const QString word = typed.trimmed().toLower();
			const int mode = word.isEmpty() ? -1 : word.startsWith(QLatin1Char('r')) ? 1 : word.startsWith(QLatin1Char('o')) ? 0 : -1;
			return mode >= 0 && ControlRegistry::instance()->invoke(QStringLiteral("transport.record_options"),
				{{QStringLiteral("mode"), recordoptions::modeNames().at(mode)}}).ok;
		});
	m_recordModes = addChoices(m_recordMode, QStringLiteral("transport.record_options"), QStringLiteral("mode"),
		recordoptions::modeNames(), {recordoptions::modeChoice(recordoptions::Mode::Overdub),
			recordoptions::modeChoice(recordoptions::Mode::Replace)});
	syncRecordMode();
	// WF-085 (Q050-179): the header shows record.remaining's estimate next to the record mode,
	// so the recording budget is visible while a route is armed and while a pass runs - and the
	// GUI answers record.remaining through the same registered command the agent surface has.
	m_recordRemaining->bindText([]() { return recordRemainingLabel(
		g_remainingTestSource ? g_remainingTestSource()
			: ControlRegistry::instance()->invoke(QStringLiteral("record.remaining"))); });
	m_recordRemaining->setToolTip(tr("Rec left: the recording time the disk's free space still allows "
		"(record.remaining); '-' when a volume cannot be measured"));
	m_volume->bind(&song->masterVolumeModel(), [](int v) { return QStringLiteral("%1%").arg(v); }, 2);
	m_pitch->bind(&song->masterPitchModel(), [](int v) { return signedValue(v); }, 8);
	m_load->bindText([]() { return QStringLiteral("%1%").arg(Engine::audioEngine()->cpuLoad()); });
	// MX-132: the master's loudness (meter.get_state) - short-term LUFS and true peak while the
	// meter is armed; typing "on" or "off" arms or disarms it (meter.arm).
	m_loudness->bindText(loudnessText, [](const QString& typed) {
		const QString word = typed.trimmed().toLower();
		if (word != QStringLiteral("on") && word != QStringLiteral("off")) { return false; }
		return ControlRegistry::instance()->invoke(QStringLiteral("meter.arm"),
			{{QStringLiteral("enabled"), word == QStringLiteral("on")}}).ok;
	});
}

void TransportReadouts::syncRecordMode()
{
	if (m_recordModes == nullptr) { return; }
	const auto mode = static_cast<int>(recordoptions::options().mode);
	if (mode == m_shownRecordMode) { return; }
	m_shownRecordMode = mode;
	m_recordMode->refresh();
	m_recordMode->setToolTip(tr("%1\nRight-click to choose; double-click and type overdub or replace.")
		.arg(recordoptions::modeScope(static_cast<recordoptions::Mode>(mode))));
	checkChoice(m_recordModes, mode);
}

void TransportReadouts::setRemainingTestSource(RemainingTestSource source)
{
	g_remainingTestSource = std::move(source);
}

void TransportReadouts::tick()
{
	if (Engine::getSong() == nullptr) { return; }
	m_position->refresh();
	m_time->refresh();
	m_load->refresh();
	// The loudness reading changes at the meter's own 100 ms block rate; 60 Hz would only
	// re-ask the registry for the same numbers.
	constexpr int LoudnessEvery = 15;
	// The recording budget follows the disk's free space and the armed routes (record.remaining);
	// a second's resolution is enough - the free space does not move at 60 Hz.
	constexpr int RecordRemainingEvery = 60;  // WF-085
	if (++m_ticks % LoudnessEvery == 0)
	{
		m_loudness->refresh();
		syncRecordMode();  // a mode set from the Record menu, a script or an undo
	}
	if (m_recordRemaining != nullptr && m_ticks % RecordRemainingEvery == 0)
	{
		m_recordRemaining->refresh();  // the budget as it changes: a route armed, space consumed
	}
}

} // namespace lmms::gui
