/*
 * SampleEditorDialog.h - a sample clip's source edited over a selection
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
#ifndef LMMS_GUI_SAMPLE_EDITOR_DIALOG_H
#define LMMS_GUI_SAMPLE_EDITOR_DIALOG_H

#include <algorithm>
#include <cstddef>
#include <limits>
#include <optional>
#include <vector>

#include <QDialog>
#include <QJsonObject>
#include <QPointer>
#include <QString>
#include <QWidget>

#include "SampleRangeFields.h"
#include "SampleThumbnail.h"
#include "lmms_export.h"

namespace lmms
{
class ControlRegistry;
class SampleClip;
struct ControlResult;

//! 0.5.0 theme 15: window.sample_editor (src/gui/ControlCommandsSampleEditor.cpp); registered by registerWindowCommands.
LMMS_EXPORT void registerSampleEditorWindowCommands(ControlRegistry& registry);
}

class QAction;
class QComboBox;
class QLabel;
class QMenu;
class QPushButton;

namespace lmms::gui
{

namespace sampleeditor::detail
{

inline auto representableFrameCount(std::size_t sampleSize) -> std::optional<int>
{
	if (sampleSize > static_cast<std::size_t>(std::numeric_limits<int>::max())) { return std::nullopt; }
	return static_cast<int>(sampleSize);
}

} // namespace sampleeditor::detail

/*! The waveform of a sample clip's whole source, with a selection dragged across it (source frames, End
 *  exclusive). 0.5.0 theme 15: the selection's edges are handles - drag one to move that edge - and the
 *  waveform zooms (Ctrl+wheel around the pointer, or the dialog's zoom buttons) and scrolls (the wheel). */
class LMMS_EXPORT SampleSelectionView : public QWidget
{
	Q_OBJECT
public:
	explicit SampleSelectionView(QWidget* parent = nullptr);
	void setClip(SampleClip* clip);
	bool hasUnsupportedFrameCount() const { return m_unsupportedFrameCount; }
	int from() const { return std::min(m_anchor, m_end); }
	int to() const { return std::max(m_anchor, m_end); }
	int frames() const { return m_frames; }
	//! Select [@a from, @a to) - clamped to the source, the ends swapped if reversed.
	void setRange(int from, int to);
	//! The frames the view shows: from viewFrom(), viewFrames() of them.
	int viewFrom() const { return m_viewFrom; }
	int viewFrames() const;
	//! Zoom by @a factor (above 1 zooms in) keeping the frame under @a anchorX in place.
	void zoomBy(double factor, int anchorX);
	void zoomToSelection();
	void showAll();
	QSize sizeHint() const override { return {640, 180}; }

signals:
	//! The selection (or the clip it is over) changed.
	void selectionChanged();

protected:
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void wheelEvent(QWheelEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;

private:
	enum class Edge
	{
		None,
		Start,
		End
	};
	int frameAt(int x) const;
	int xOf(int frame) const;
	//! The frame step a plain arrow takes: about one percent of the visible source, at least one frame.
	int keyboardStep() const;
	QString readout() const;
	//! The selection edge whose handle is under @a x (a target no narrower than the design system's).
	Edge edgeAt(int x) const;
	//! Show @a span frames from @a first, clamped to the source and to a frame a pixel at the most.
	void setView(int first, int span);
	void paintHandle(QPainter& p, int frame, Edge edge) const;
	QPointer<SampleClip> m_clip;
	SampleThumbnail m_thumbnail;
	bool m_unsupportedFrameCount = false;
	int m_frames = 0;
	int m_anchor = 0;
	int m_end = 0;
	int m_viewFrom = 0;
	int m_viewSpan = 0;  //!< 0: the whole source
};

/*! 0.5.0 L05 (ED-156): the sample editor - select a range of the clip's source, then gain, silence,
 *  delete, reverse, fade or crop it. Every button runs sample.edit_range, so an agent's edit and the
 *  editor's are one path and one undo step each. 0.5.0 theme 15: the range is also three typed fields -
 *  Start, End and Length, in the unit the Units menu picks (time, bars|beats or samples) - and the
 *  operations act on exactly what the fields say. window.sample_editor opens it and sets its range. */
class LMMS_EXPORT SampleEditorDialog : public QDialog
{
	Q_OBJECT
public:
	SampleEditorDialog(SampleClip* clip, QWidget* parent = nullptr);
	//! The open sample editor of @a clip, or a new one (shown either way).
	static SampleEditorDialog* openFor(SampleClip* clip, QWidget* parent);

	/*! Q050-106: ask for the two destinations, then run sample.split_stereo on the clip's
	 *  whole source. False when the person cancelled a destination dialog (nothing ran);
	 *  true with the command's outcome in @a result otherwise. Shared by the Sample Editor
	 *  and the clip's Process menu, so both surfaces reach the same registered command. */
	static bool splitStereoToFiles(SampleClip* clip, QWidget* parent, ControlResult* result);
	//! Q050-106: the clip Process menu's "Split Stereo to Two Mono Files..." entry, running
	//! splitStereoToFiles on @a clip (tagged with sample.split_stereo; disabled unless @a enabled).
	static QAction* addSplitStereoAction(QMenu* menu, SampleClip* clip, bool enabled);

	SampleClip* clip() const;  // out of line: QPointer<SampleClip>::data() needs the complete type
	SampleSelectionView* view() const { return m_view; }
	void setRange(int from, int to) { m_view->setRange(from, to); }
	void setUnit(samplerange::Unit unit);
	samplerange::Unit unit() const { return m_unit; }
	//! window.sample_editor's reply: the clip, its source, the range and the view, with the range as text.
	QJsonObject describe() const;
	//! The last operation's outcome as the status line shows it.
	QString status() const;

private:
	QWidget* buildFields();
	QWidget* buildZoom();
	QWidget* buildActions();
	samplerange::Context context() const;
	//! The fields, their limits and the waveform's accessible description follow the selection.
	void syncFields();
	//! "0:01.000 to 0:02.500, 0:01.500 long" in the chosen unit, or that nothing is selected.
	QString rangeText() const;
	void report(const QString& message, bool failed);
	//! Run @a op (the button @a label) on the range the fields say; the status line says what happened.
	void apply(const QString& op, double gainDb, const QString& label);
	//! Q050-106: the whole source to mono, or the two destinations for a stereo split.
	void applyMono();
	void applySplit();
	QPointer<SampleClip> m_clip;
	SampleSelectionView* m_view;
	SampleFrameField* m_start = nullptr;
	SampleFrameField* m_end = nullptr;
	SampleFrameField* m_length = nullptr;
	QComboBox* m_unitBox = nullptr;
	QLabel* m_status = nullptr;
	QPushButton* m_mono = nullptr;
	QPushButton* m_split = nullptr;
	samplerange::Unit m_unit = samplerange::Unit::Time;
	std::vector<QPushButton*> m_actions;  //!< the edit buttons: enabled while there is a selection to edit
};

} // namespace lmms::gui

#endif // LMMS_GUI_SAMPLE_EDITOR_DIALOG_H
