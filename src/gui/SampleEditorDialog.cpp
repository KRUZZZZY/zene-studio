/*
 * SampleEditorDialog.cpp - a sample clip's source edited over a selection
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

#include "SampleEditorDialog.h"

#include <algorithm>
#include <functional>

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include "AccessibleWidgets.h"
#include "CommandForm.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "SampleClip.h"

namespace lmms::gui
{

namespace
{

//! A row of controls with no margins of its own.
QHBoxLayout* rowLayout(QWidget* row)
{
	auto* layout = new QHBoxLayout(row);
	layout->setContentsMargins(0, 0, 0, 0);
	return layout;
}

/*! Q050-106: the two destinations of a stereo split. A small, in-app dialog rather
 *  than two stacked native file pickers: the pair belongs together (two channels,
 *  two files), and its fields are ordinary line edits a GUI test can fill in. */
class StereoDestinationDialog : public QDialog
{
public:
	StereoDestinationDialog(const QString& left, const QString& right, QWidget* parent) :
		QDialog(parent)
	{
		setObjectName(QStringLiteral("stereoSplitDialog"));
		setWindowTitle(tr("Split Stereo to Two Mono Files"));
		auto* layout = new QVBoxLayout(this);
		auto* form = new QFormLayout;
		m_left = new QLineEdit(left, this);
		m_left->setObjectName(QStringLiteral("leftPath"));
		m_left->setAccessibleName(tr("Left channel file"));
		m_right = new QLineEdit(right, this);
		m_right->setObjectName(QStringLiteral("rightPath"));
		m_right->setAccessibleName(tr("Right channel file"));
		form->addRow(tr("&Left channel file"), m_left);
		form->addRow(tr("&Right channel file"), m_right);
		layout->addLayout(form);
		auto* note = new QLabel(tr("Both files are float32 WAV at the source rate and length. "
			"The clip itself is not changed."), this);
		note->setWordWrap(true);
		layout->addWidget(note);
		auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
		connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
		connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
		layout->addWidget(buttons);
	}

	QString leftPath() const { return m_left->text().trimmed(); }
	QString rightPath() const { return m_right->text().trimmed(); }

private:
	QLineEdit* m_left = nullptr;
	QLineEdit* m_right = nullptr;
};

} // namespace

SampleEditorDialog::SampleEditorDialog(SampleClip* clip, QWidget* parent) :
	QDialog(parent),
	m_clip(clip),
	m_view(new SampleSelectionView(this)),
	m_unit(samplerange::unitFor(musicaltime::currentFormat()))
{
	setWindowTitle(QObject::tr("Sample Editor - %1").arg(clip->name().isEmpty() ? QObject::tr("audio") : clip->name()));
	setAttribute(Qt::WA_DeleteOnClose);
	setProperty("controlCommand", QStringLiteral("sample.edit_range"));
	auto* layout = new QVBoxLayout(this);
	layout->addWidget(m_view, 1);
	layout->addWidget(buildFields());
	layout->addWidget(buildZoom());
	layout->addWidget(buildActions());
	m_status = new QLabel(this);
	m_status->setAccessibleName(tr("Sample editor status"));
	m_status->setWordWrap(true);
	layout->addWidget(m_status);
	connect(m_view, &SampleSelectionView::selectionChanged, this, &SampleEditorDialog::syncFields);
	// The fields read in the musical-time format (theme 7) until the Units menu picks another unit.
	connect(musicaltime::formatSignal(), &musicaltime::FormatSignal::changed, this,
		[this] { setUnit(samplerange::unitFor(musicaltime::currentFormat())); });
	m_view->setClip(clip);
	// The whole-source buttons follow the source layout, not the selection.
	syncFields();
	if (m_view->hasUnsupportedFrameCount())
	{
		report(tr("This sample is too long for the sample editor; its range cannot be selected or edited."), true);
	}
}

SampleClip* SampleEditorDialog::clip() const
{
	return m_clip.data();
}

SampleEditorDialog* SampleEditorDialog::openFor(SampleClip* clip, QWidget* parent)
{
	for (QWidget* window : QApplication::topLevelWidgets())
	{
		auto* open = qobject_cast<SampleEditorDialog*>(window);
		if (open != nullptr && open->clip() == clip)
		{
			open->show();
			open->raise();
			return open;
		}
	}
	auto* dialog = new SampleEditorDialog(clip, parent);
	dialog->show();
	return dialog;
}

QWidget* SampleEditorDialog::buildFields()
{
	auto* row = new QWidget(this);
	QHBoxLayout* layout = rowLayout(row);
	const struct { SampleFrameField** field; bool length; const char* label; const char* name; const char* tip; } fields[] = {
		{&m_start, false, "&Start", "Range start", "Where the range starts in the clip's source; type a value or step it with Up and Down"},
		{&m_end, false, "&End", "Range end", "Where the range ends: the first frame after it"},
		{&m_length, true, "&Length", "Range length", "How long the range is; a new length moves its end"}};
	for (const auto& entry : fields)
	{
		auto* field = new SampleFrameField(entry.length, row);
		auto* label = new QLabel(tr(entry.label), row);
		label->setBuddy(field);
		field->setAccessibleName(tr(entry.name));
		field->setToolTip(tr(entry.tip));
		field->setProperty("controlCommand", QStringLiteral("window.sample_editor"));
		layout->addWidget(label);
		layout->addWidget(field, 1);
		*entry.field = field;
	}
	// Each field moves its own edge; a start past the end (or an end before the start) takes the other with it.
	connect(m_start, qOverload<int>(&QSpinBox::valueChanged), this, [this](int v) { m_view->setRange(v, std::max(v, m_view->to())); });
	connect(m_end, qOverload<int>(&QSpinBox::valueChanged), this, [this](int v) { m_view->setRange(std::min(m_view->from(), v), v); });
	connect(m_length, qOverload<int>(&QSpinBox::valueChanged), this,
		[this](int v) { m_view->setRange(m_view->from(), std::min(m_view->frames(), m_view->from() + v)); });
	m_unitBox = new QComboBox(row);
	for (int i = 0; i < samplerange::unitNames().size(); ++i)
	{
		m_unitBox->addItem(samplerange::unitLabel(static_cast<samplerange::Unit>(i)), samplerange::unitNames().at(i));
	}
	m_unitBox->setCurrentIndex(static_cast<int>(m_unit));
	m_unitBox->setAccessibleName(tr("Range units"));
	m_unitBox->setToolTip(tr("The unit the range fields read in: the time from the source's start, bars and beats at the "
		"song's tempo from where the clip starts, or sample frames. A typed suffix (s, smp, bars) overrides it."));
	auto* unitLabel = new QLabel(tr("&Units"), row);
	unitLabel->setBuddy(m_unitBox);
	connect(m_unitBox, qOverload<int>(&QComboBox::currentIndexChanged), this,
		[this](int index) { setUnit(static_cast<samplerange::Unit>(index)); });
	layout->addWidget(unitLabel);
	layout->addWidget(m_unitBox);
	return row;
}

QWidget* SampleEditorDialog::buildZoom()
{
	auto* row = new QWidget(this);
	QHBoxLayout* layout = rowLayout(row);
	const struct { const char* label; const char* tip; std::function<void()> act; } zooms[] = {
		{"Zoom &In", "Show half as much of the waveform (Ctrl+wheel zooms around the pointer)",
			[this] { m_view->zoomBy(2.0, m_view->width() / 2); }},
		{"Zoom &Out", "Show twice as much of the waveform", [this] { m_view->zoomBy(0.5, m_view->width() / 2); }},
		{"Zoom to Selectio&n", "Fit the selected range to the waveform", [this] { m_view->zoomToSelection(); }},
		{"Show &All", "Show the whole source", [this] { m_view->showAll(); }}};
	for (const auto& zoom : zooms)
	{
		auto* button = new QPushButton(tr(zoom.label), row);
		button->setToolTip(tr(zoom.tip));
		button->setProperty("controlCommand", QStringLiteral("window.sample_editor"));
		connect(button, &QPushButton::clicked, this, zoom.act);
		layout->addWidget(button);
	}
	layout->addStretch(1);
	return row;
}

QWidget* SampleEditorDialog::buildActions()
{
	auto* row = new QWidget(this);
	QHBoxLayout* layout = rowLayout(row);
	// Each button says in its tooltip that it edits only the selected range, and what it does there.
	const struct { const char* label; const char* op; double gain; const char* tip; } actions[] = {
		{"-3 dB", "gain", -3.0, "Lower the selected range by 3 dB"}, {"+3 dB", "gain", 3.0, "Raise the selected range by 3 dB"},
		{"Silence", "silence", 0.0, "Silence the selected range"},
		{"Delete", "delete", 0.0, "Delete the selected range from the audio; what follows it closes up"},
		{"Reverse", "reverse", 0.0, "Reverse the selected range"}, {"Fade In", "fade_in", 0.0, "Fade the selected range in (linear)"},
		{"Fade Out", "fade_out", 0.0, "Fade the selected range out (linear)"},
		{"Crop", "crop", 0.0, "Keep only the selected range of the audio"}};
	for (const auto& action : actions)
	{
		auto* button = new QPushButton(QObject::tr(action.label), row);
		button->setToolTip(QObject::tr(action.tip));
		button->setProperty("controlCommand", QStringLiteral("sample.edit_range"));
		const QString op = QLatin1String(action.op);
		const double gain = action.gain;
		const QString label = button->text();
		connect(button, &QPushButton::clicked, this, [this, op, gain, label] { apply(op, gain, label); });
		layout->addWidget(button);
		m_actions.push_back(button);
	}
	// Q050-106: the whole-source operations. The range buttons above edit the selection;
	// these two act on the full source, as their tooltips say.
	m_mono = new QPushButton(tr("To Mono"), row);
	m_mono->setToolTip(tr("Convert the whole source to mono: every frame becomes (L + R) / 2 on both sides (one undo step)"));
	m_mono->setProperty("controlCommand", QStringLiteral("sample.edit_range"));
	connect(m_mono, &QPushButton::clicked, this, &SampleEditorDialog::applyMono);
	layout->addWidget(m_mono);
	m_split = new QPushButton(tr("Split Stereo..."), row);
	m_split->setToolTip(tr("Write the left and right channels as two mono WAV files at the source rate; the clip is unchanged"));
	m_split->setProperty("controlCommand", QStringLiteral("sample.split_stereo"));
	connect(m_split, &QPushButton::clicked, this, &SampleEditorDialog::applySplit);
	layout->addWidget(m_split);
	return row;
}

samplerange::Context SampleEditorDialog::context() const
{
	samplerange::Context context;
	if (m_clip.isNull()) { return context; }
	context.sampleRate = std::max(1, m_clip->sample().sampleRate());
	context.origin = m_clip->startPosition().getTicks();
	return context;
}

void SampleEditorDialog::setUnit(samplerange::Unit unit)
{
	m_unit = unit;
	const QSignalBlocker blocker(m_unitBox);
	m_unitBox->setCurrentIndex(static_cast<int>(unit));
	syncFields();
}

void SampleEditorDialog::syncFields()
{
	const int from = m_view->from();
	const int to = m_view->to();
	const samplerange::Context ctx = context();
	m_length->setLengthStart(from);
	const std::pair<SampleFrameField*, int> values[] = {{m_start, from}, {m_end, to}, {m_length, to - from}};
	for (const auto& [field, value] : values)
	{
		// The fields show the selection; only the user's own edits move it.
		const QSignalBlocker blocker(field);
		field->setRange(0, m_view->frames());
		field->setValue(value);
		field->setFormat(ctx, m_unit);
	}
	m_view->setAccessibleDescription(rangeText());
	// A button with no selection to edit would do nothing: it is enabled only while there is one.
	for (QPushButton* button : m_actions) { button->setEnabled(to > from); }
	// Q050-106: the whole-source pair needs audio in stereo; a mono source has nothing to
	// split and nothing to downmix.
	const bool stereo = !m_clip.isNull() && m_view->frames() > 0
		&& m_clip->sample().channels() == DEFAULT_CHANNELS;
	if (m_mono != nullptr) { m_mono->setEnabled(stereo); }
	if (m_split != nullptr) { m_split->setEnabled(stereo); }
}

QString SampleEditorDialog::rangeText() const
{
	if (m_view->hasUnsupportedFrameCount()) { return tr("This sample is too long for the sample editor."); }
	const int from = m_view->from();
	const int to = m_view->to();
	if (to <= from) { return tr("Nothing selected"); }
	const samplerange::Context ctx = context();
	return tr("%1 to %2, %3 long").arg(samplerange::frameText(from, ctx, m_unit), samplerange::frameText(to, ctx, m_unit),
		samplerange::frameText(to - from, ctx, m_unit, true, from));
}

QJsonObject SampleEditorDialog::describe() const
{
	const int from = m_view->from();
	const int to = m_view->to();
	const int channels = m_clip.isNull() ? 0 : m_clip->sample().channels();
	return {{QStringLiteral("clip"), m_clip.isNull() ? QString() : control::clipIdOf(m_clip.data())},
		{QStringLiteral("frames"), m_view->frames()}, {QStringLiteral("sample_rate"), context().sampleRate},
		{QStringLiteral("channels"), m_clip.isNull() ? QJsonValue(QJsonValue::Null) : QJsonValue(channels)},
		{QStringLiteral("layout"), m_clip.isNull() ? QJsonValue(QJsonValue::Null)
			: QJsonValue(channels == 1 ? QStringLiteral("mono") : QStringLiteral("stereo"))},
		{QStringLiteral("from"), from}, {QStringLiteral("to"), to}, {QStringLiteral("length"), to - from},
		{QStringLiteral("view_from"), m_view->viewFrom()}, {QStringLiteral("view_to"), m_view->viewFrom() + m_view->viewFrames()},
		{QStringLiteral("unit"), samplerange::unitNames().at(static_cast<int>(m_unit))},
		{QStringLiteral("start_text"), m_start->text()}, {QStringLiteral("end_text"), m_end->text()},
		{QStringLiteral("length_text"), m_length->text()}, {QStringLiteral("status"), status()}};
}

QString SampleEditorDialog::status() const
{
	return m_status->text();
}

void SampleEditorDialog::report(const QString& message, bool failed)
{
	m_status->setText(message);
	a11y::announceStatus(m_status, message, failed ? a11y::Politeness::Assertive : a11y::Politeness::Polite);
}

void SampleEditorDialog::apply(const QString& op, double gainDb, const QString& label)
{
	const int from = m_view->from();
	const int to = m_view->to();
	if (m_clip.isNull() || to <= from) { return; }
	const QString range = rangeText();
	const ControlResult result = runCommand(this, CommandCall{QStringLiteral("sample.edit_range"),
		{{QStringLiteral("clip"), control::clipIdOf(m_clip)}, {QStringLiteral("from"), from}, {QStringLiteral("to"), to},
		{QStringLiteral("op"), op}, {QStringLiteral("gain_db"), gainDb}}, {}, {}, false});
	if (!result.ok)
	{
		report(tr("%1 was not applied: %2").arg(label, result.errorMessage), true);  // the selection stays as it was
		return;
	}
	m_view->setClip(m_clip);  // the source changed: draw it again (the same length keeps the selection)
	// A crop leaves the range as the whole source; a delete closes it up, leaving the place it was.
	if (op == QLatin1String("crop")) { m_view->setRange(0, m_view->frames()); }
	else if (op == QLatin1String("delete")) { m_view->setRange(from, from); }
	report(tr("%1 applied to %2. Undo (Ctrl+Z) reverts it.").arg(label, range), false);
}

void SampleEditorDialog::applyMono()
{
	if (m_clip.isNull()) { return; }
	const int frames = m_view->frames();
	if (frames <= 0) { return; }
	const ControlResult result = runCommand(this, CommandCall{QStringLiteral("sample.edit_range"),
		{{QStringLiteral("clip"), control::clipIdOf(m_clip)}, {QStringLiteral("from"), 0},
			{QStringLiteral("to"), frames}, {QStringLiteral("op"), QStringLiteral("mono")}}, {}, {}, false});
	if (!result.ok)
	{
		report(tr("Mono was not applied: %1").arg(result.errorMessage), true);
		return;
	}
	m_view->setClip(m_clip);
	m_view->setRange(0, m_view->frames());
	report(tr("Mono applied to the whole source with (L + R) / 2. Undo (Ctrl+Z) reverts it."), false);
}

void SampleEditorDialog::applySplit()
{
	if (m_clip.isNull()) { return; }
	ControlResult result;
	if (!splitStereoToFiles(m_clip, this, &result)) { return; }
	if (!result.ok)
	{
		report(tr("The stereo split was not applied: %1").arg(result.errorMessage), true);
		return;
	}
	report(tr("Wrote %1 and %2. The clip is unchanged.").arg(
		result.result.value(QStringLiteral("left_path")).toString(),
		result.result.value(QStringLiteral("right_path")).toString()), false);
}

bool SampleEditorDialog::splitStereoToFiles(SampleClip* clip, QWidget* parent, ControlResult* result)
{
	if (clip == nullptr || result == nullptr) { return false; }
	const QString source = clip->sampleFile();
	QString base = source.isEmpty() ? clip->name() : QFileInfo(source).completeBaseName();
	if (base.isEmpty()) { base = QStringLiteral("sample"); }
	const QString directory = source.isEmpty() ? QString() : QFileInfo(source).absolutePath();
	const auto join = [](const QString& directory, const QString& name) {
		return directory.isEmpty() ? name : directory + QLatin1Char('/') + name;
	};
	StereoDestinationDialog dialog(
		join(directory, base + QStringLiteral(" - left.wav")),
		join(directory, base + QStringLiteral(" - right.wav")), parent);
	if (dialog.exec() != QDialog::Accepted) { return false; }
	*result = runCommand(parent, CommandCall{QStringLiteral("sample.split_stereo"),
		{{QStringLiteral("clip"), control::clipIdOf(clip)}, {QStringLiteral("left_path"), dialog.leftPath()},
			{QStringLiteral("right_path"), dialog.rightPath()}}, {}, {}, false});
	return true;
}

QAction* SampleEditorDialog::addSplitStereoAction(QMenu* menu, SampleClip* clip, bool enabled)
{
	QAction* split = menu->addAction(tr("Split Stereo to Two Mono Files..."));
	split->setProperty("controlCommand", QStringLiteral("sample.split_stereo"));
	split->setEnabled(enabled);
	QPointer<SampleClip> target = clip;
	QObject::connect(split, &QAction::triggered, menu, [target, menu] {
		if (target) { ControlResult ignored; splitStereoToFiles(target, menu->parentWidget(), &ignored); }
	});
	return split;
}

} // namespace lmms::gui
